/**
 * @file sfu_shard_srtp.c
 * @brief Install DTLS-SRTP sessions and move media through them.
 *
 * RFC 5764 §4.2 lays the exporter output out as
 *
 *     client_write_key | server_write_key | client_write_salt | server_write_salt
 *
 * with lengths taken from the negotiated profile (AES-CM-128/SHA1-80: 16 + 14,
 * AEAD AES-128-GCM: 16 + 12). The server decrypts with the client-write half
 * and encrypts with the server-write half. libsrtp's v3 policy API takes key
 * and salt separately, so no reassembly buffer is needed; the exporter output
 * is zeroed as soon as both sessions own their copies.
 *
 * The media path is in place: unprotect into the packet slot the datagram
 * arrived in, learn the SSRC, protect the echo into the same slot. Nothing on
 * this path allocates.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <arpa/inet.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

#include <srtp3/srtp.h>

#include <uflib/slab/inlined_slab.h>

#include "sfu_server_priv.h"
#include "sfu_shard_nack.h"
#include "sfu_shard_room.h"
#include "srtp/sfu_shard_srtp.h"
#include "srtp/sfu_rtp_rewrite.h"
#include "sfu_shard_track.h"

#define SFU_SRTP_KEY_LEN 16u
#define SFU_SRTP_MAX_SALT_LEN 14u
#define SFU_SRTP_MAX_MATERIAL (2u * (SFU_SRTP_KEY_LEN + SFU_SRTP_MAX_SALT_LEN))

/** Map the OpenSSL negotiated profile to its libsrtp profile and salt length. */
static bool
sProfile(uint16_t srtp_profile, srtp_profile_t *profile, size_t *salt_length)
{
  switch (srtp_profile) {
    case 0x0001: /* SRTP_AES128_CM_SHA1_80 */
      *profile = srtp_profile_aes128_cm_sha1_80;
      *salt_length = 14;
      return true;
    case 0x0007: /* SRTP_AEAD_AES_128_GCM */
      *profile = srtp_profile_aead_aes_128_gcm;
      *salt_length = 12;
      return true;
    default:
      return false;
  }
}

static bool
sCreateSession(srtp_profile_t profile, srtp_ssrc_type_t ssrc_type, bool allow_repeat_tx, const uint8_t *key,
               const uint8_t *salt, size_t salt_length, srtp_t *session)
{
  srtp_policy_t policy = NULL;
  if (srtp_policy_create(&policy) != srtp_err_status_ok) return false;

  const srtp_ssrc_t ssrc = {.type = ssrc_type, .value = 0};
  bool ok = srtp_policy_set_ssrc(policy, ssrc) == srtp_err_status_ok &&
            srtp_policy_set_profile(policy, profile) == srtp_err_status_ok &&
            /* Applies to RTP and SRTCP both; without it a replayed datagram is
             * indistinguishable from a fresh one. */
            srtp_policy_set_window_size(policy, SFU_SRTP_REPLAY_WINDOW) == srtp_err_status_ok &&
            srtp_policy_set_allow_repeat_tx(policy, allow_repeat_tx) == srtp_err_status_ok &&
            srtp_policy_add_key(policy, key, SFU_SRTP_KEY_LEN, salt, salt_length, NULL, 0) == srtp_err_status_ok &&
            srtp_policy_validate(policy) == srtp_err_status_ok &&
            srtp_create(session, policy) == srtp_err_status_ok;

  srtp_policy_destroy(policy);
  return ok;
}

bool
SfuShardSrtpStart(SfuShard *shard, SfuPeer *peer)
{
  SfuCounters *counters = shard->spec->counters;

  srtp_profile_t profile;
  size_t         salt_length = 0;
  if (!sProfile(peer->dtls.srtp_profile, &profile, &salt_length)) {
    counters->srtp_failures++;
    return false;
  }

  const size_t material_length = 2u * (SFU_SRTP_KEY_LEN + salt_length);
  if (material_length > sizeof(peer->dtls.key_material)) {
    counters->srtp_failures++;
    return false;
  }

  const uint8_t *client_key = peer->dtls.key_material;
  const uint8_t *server_key = client_key + SFU_SRTP_KEY_LEN;
  const uint8_t *client_salt = server_key + SFU_SRTP_KEY_LEN;
  const uint8_t *server_salt = client_salt + salt_length;

  /* A NACK retransmit re-sends a sequence the outbound session has already
   * protected; without repeat transmission libsrtp refuses the older index. */
  if (!sCreateSession(profile, ssrc_any_inbound, false, client_key, client_salt, salt_length, &peer->inbound) ||
      !sCreateSession(profile, ssrc_any_outbound, true, server_key, server_salt, salt_length, &peer->outbound)) {
    if (peer->inbound) {
      srtp_dealloc(peer->inbound);
      peer->inbound = NULL;
    }
    counters->srtp_failures++;
    return false;
  }

  /* The sessions own their copies now; the exporter output has no further use. */
  memset(peer->dtls.key_material, 0, sizeof(peer->dtls.key_material));
  counters->srtp_sessions++;
  return true;
}

void
SfuShardSrtpStop(SfuShard *shard, SfuPeer *peer)
{
  if (peer->inbound) {
    srtp_dealloc(peer->inbound);
    peer->inbound = NULL;
    shard->spec->counters->srtp_sessions--;
  }
  if (peer->outbound) {
    srtp_dealloc(peer->outbound);
    peer->outbound = NULL;
  }
  memset(peer->dtls.key_material, 0, sizeof(peer->dtls.key_material));
}


bool
SfuShardSrtpOutboundUsed(const SfuPeer *peer, uint32_t ssrc)
{
  uint32_t roc;
  return peer->outbound && srtp_stream_get_roc(peer->outbound, ssrc, &roc) == srtp_err_status_ok;
}

/* Parse RFC 8285 extension elements. The media section supplies both the
 * local identifier and MID value. A recognized identifier must name exactly
 * one publish section, even when several sections share that identifier. */
static bool
sReadMid(const SfuPeerConfigSnapshot *config, const uint8_t *rtp, size_t length, size_t *matched)
{
  *matched = config->media_count;
  if (!(rtp[0] & 0x10u)) return true;
  const size_t base = 12u + 4u * (rtp[0] & 0x0fu);
  const size_t end = SfuRtpPayloadOffset(rtp, length);
  const uint16_t profile = (uint16_t)((rtp[base] << 8) | rtp[base + 1u]);
  const bool one_byte = profile == 0xbedeu;
  if (!one_byte && (profile & 0xfff0u) != 0x1000u) return true;

  for (size_t pos = base + 4u; pos < end;) {
    const uint8_t element = rtp[pos++];
    if (!element) continue;
    const uint8_t id = one_byte ? element >> 4 : element;
    if (one_byte && (id == 0u || id == 15u)) break;
    const bool missing_length = !one_byte && pos == end;
    const size_t bytes = missing_length ? 0 : one_byte ? (size_t)(element & 0x0fu) + 1u : rtp[pos++];
    if (missing_length || bytes > end - pos) {
      for (size_t i = 0; i < config->media_count; ++i) {
        if (config->media[i].audio_level_extension_id == id) return true;
      }
      return false;
    }

    bool is_mid = false;
    size_t section_index = config->media_count;
    for (size_t i = 0; i < config->media_count; ++i) {
      const SfuPeerConfigSection *section = &config->media[i];
      if (section->direction != SFU_PEER_CONFIG_PUBLISH || section->mid_extension_id != id) continue;
      is_mid = true;
      if (strlen(section->mid) == bytes && memcmp(section->mid, rtp + pos, bytes) == 0) section_index = i;
    }
    if (is_mid) {
      if (*matched != config->media_count || section_index == config->media_count) return false;
      *matched = section_index;
    }
    pos += bytes;
  }
  return true;
}

/* A malformed or duplicate level is not a sample. RTP framing has already
 * been checked by SfuRtpPayloadSize before this reads the extension block. */
static int
sReadAudioLevel(uint8_t extension_id, const uint8_t *rtp, size_t length, uint8_t *form)
{
  if (!extension_id || !(rtp[0] & 0x10u)) return -1;
  const size_t base = 12u + 4u * (rtp[0] & 0x0fu);
  const size_t end = SfuRtpPayloadOffset(rtp, length);
  const uint16_t profile = (uint16_t)((rtp[base] << 8) | rtp[base + 1u]);
  const bool one_byte = profile == 0xbedeu;
  if (!one_byte && (profile & 0xfff0u) != 0x1000u) return -1;
  *form = one_byte ? 1 : 2;
  int level = -1;
  for (size_t pos = base + 4u; pos < end;) {
    const uint8_t element = rtp[pos++];
    if (!element) continue;
    const uint8_t id = one_byte ? element >> 4 : element;
    if (one_byte && (id == 0 || id == 15)) break;
    if (!one_byte && pos == end) return -1;
    const size_t bytes = one_byte ? (size_t)(element & 0x0fu) + 1u : rtp[pos++];
    if (bytes > end - pos) return -1;
    if (id == extension_id) {
      if (bytes != 1 || level >= 0) return -1;
      level = rtp[pos] & 0x7f;
    }
    pos += bytes;
  }
  return level;
}

static void
sUpdateAudioLevel(SfuCounters *counters, SfuPublisherTrack *track, const uint8_t *rtp, size_t length, uint64_t now_ns)
{
  uint8_t form = 0;
  const int level = sReadAudioLevel(track->audio_level.extension_id, rtp, length, &form);
  if (level < 0 || (!track->audio_level.allow_mixed && track->audio_level.form &&
                    track->audio_level.form != form)) return;
  track->audio_level.form = form;
  const uint64_t update = ++counters->audio_level_updates;
  if (!track->audio_level.generation) {
    track->audio_level.generation = update;
    track->audio_level.average = level;
  } else {
    const double decay = exp(-(double)(now_ns - track->audio_level.last_sample_ns) / 250000000.0);
    track->audio_level.average = decay * track->audio_level.average + (1.0 - decay) * level;
  }
  track->audio_level.last_sample_ns = now_ns;
  if (level != 127) track->audio_level.last_voice_ns = now_ns;
}

static SfuPublisherTrack *
sNegotiatedTrack(SfuShard *shard, SfuPeer *peer, const uint8_t *rtp, size_t length, uint32_t ssrc)
{
  if (!ssrc) return NULL;
  const SfuPeerConfigSnapshot *config = peer->config;
  size_t mid_index;
  if (!sReadMid(config, rtp, length, &mid_index)) return NULL;
  for (size_t i = 0; i < config->media_count; ++i) {
    const SfuPeerConfigSection *section = &config->media[i];
    if (section->bound_ssrc != ssrc) continue;
    if ((mid_index != config->media_count && mid_index != i) ||
        section->payload_type != (rtp[1] & 0x7fu)) return NULL;
    return SfuShardPublishedTrack(shard, peer, ssrc);
  }

  if (mid_index == config->media_count || config->media[mid_index].payload_type != (rtp[1] & 0x7fu)) return NULL;
  return SfuShardBindPublication(shard, peer, mid_index, ssrc);
}

/**
 * Forward one plaintext RTP packet to every subscriber of the publisher's
 * track. Each subscriber gets its own slab slot because the SSRC, sequence and
 * SRTP context differ per recipient; the inbound slot stays untouched so the
 * publisher's own stream is unaffected.
 */
static void
sFanOut(SfuShard *shard, SfuPeer *publisher, const uint8_t *plaintext, size_t length, size_t payload_size, uint32_t ssrc,
        uint16_t sequence, uint64_t retained_serial)
{
  SfuCounters *counters = shard->spec->counters;
  SfuRoom *    room = publisher->room;
  if (!room) return;

  for (size_t index = 0; index < room->member_count; index++) {
    SfuPeer *subscriber = room->members[index];
    if (!subscriber || subscriber == publisher) continue;
    if (subscriber->state != SFU_PEER_STATE_CONNECTED || subscriber->dtls.state != SFU_DTLS_READY ||
        !subscriber->outbound) {
      counters->fanout_skipped++;
      continue;
    }

    SfuSubscription *subscription = SfuShardSubscriptionFor(subscriber, ssrc);
    if (!subscription) {
      counters->fanout_skipped++;
      continue;
    }

    SfuPacket *slot = InlinedSlabTake(&shard->packets.slab);
    if (!slot) {
      counters->fanout_slab_drops++;
      continue;
    }

    SfuRtpMapping mapping = {0};
    if (subscription->sent_mapping) {
      const SfuPeerConfigSection *section = &subscriber->config->media[subscription->subscriber_media_index];
      mapping = (SfuRtpMapping){.mid = section->mid, .payload_type = section->payload_type,
                                .mid_extension_id = section->mid_extension_id};
    }
    uint8_t *out = slot->data + SFU_PACKET_HEADROOM;
    const uint16_t out_sequence = (uint16_t)(sequence + subscription->seq_offset);
    const size_t rewritten = SfuRtpRewrite(out, plaintext, length, subscription, out_sequence, &mapping);

    size_t protected_length = SFU_PACKET_CAPACITY;
    if (rewritten && srtp_protect(subscriber->outbound, out, rewritten, out, &protected_length, 0) == srtp_err_status_ok) {
      /* A retry must reproduce the original protected packet, even when a
       * revision changed the mapping or the original sendto failed. */
      if (retained_serial && subscription->sent_mapping) {
        SfuRtpSentMapping *sent = &subscription->sent_mapping[sequence & (SFU_NACK_VIDEO_SLOTS - 1u)];
        *sent = (SfuRtpSentMapping){.serial = retained_serial, .payload_type = mapping.payload_type,
                                    .mid_extension_id = mapping.mid_extension_id};
      }
      if (sendto(shard->fd, out, protected_length, 0, (const struct sockaddr *)&subscriber->address,
                 subscriber->address_length) >= 0) {
        counters->tx_datagrams++;
        subscription->sent_packets++;
        subscription->sent_octets += (uint32_t)payload_size;
        counters->media_fanned_out++;
        InlinedSlabPut(&shard->packets.slab, slot);
        continue;
      }
    }
    counters->drops_send++;
    InlinedSlabPut(&shard->packets.slab, slot);
  }
}

/** Monotonic nanoseconds: the jitter estimator's arrival clock. */
static uint64_t
sMonotonicNs(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

size_t
SfuSrtcpTrailerLength(uint16_t srtp_profile)
{
  switch (srtp_profile) {
  case 0x0001: return 4u + 10u; /* SRTP_AES128_CM_SHA1_80 tag */
  case 0x0007: return 4u + 16u; /* SRTP_AEAD_AES_128_GCM tag */
  default: return 0;
  }
}

bool
SfuSrtcpProtect(srtp_t session, uint16_t srtp_profile, uint8_t *buffer, size_t capacity, size_t *length)
{
  if (!session || !buffer || !length) return false;
  const size_t trailer = SfuSrtcpTrailerLength(srtp_profile);
  if (!trailer || *length > capacity || capacity - *length < trailer) return false;

  size_t protected_length = capacity; /* the out-parameter is a capacity, not an input length */
  if (srtp_protect_rtcp(session, buffer, *length, buffer, &protected_length, 0) != srtp_err_status_ok) {
    return false;
  }
  *length = protected_length;
  return true;
}

SfuMediaOutcome
SfuShardHandleSrtp(SfuShard *shard, SfuPeer *peer, SfuPacket *packet, size_t *reply_length)
{
  SfuCounters *counters = shard->spec->counters;
  /* One arrival stamp per datagram: jitter needs it, and the shard's coarse
   * millisecond clock is too blunt for a 90 kHz video track. */
  const uint64_t arrival_ns = sMonotonicNs();

  if (!peer->inbound || !peer->outbound) {
    counters->srtp_failures++;
    return SFU_MEDIA_DROPPED;
  }

  uint8_t *buffer = packet->data + SFU_PACKET_HEADROOM;
  /* The length out-parameters are buffer capacities, not input lengths: protect
   * appends the tag, so it needs the slot's remaining capacity to write into. */
  size_t plaintext_capacity = SFU_PACKET_CAPACITY;
  if (srtp_unprotect(peer->inbound, buffer, packet->length, buffer, &plaintext_capacity) != srtp_err_status_ok) {
    counters->srtp_unprotect_failures++;
    return SFU_MEDIA_DROPPED;
  }
  counters->srtp_unprotect_ok++;
  const size_t length = plaintext_capacity;

  size_t payload_size = 0;
  if (!SfuRtpPayloadSize(buffer, length, &payload_size)) {
    counters->srtp_bad_packets++;
    return SFU_MEDIA_DROPPED;
  }

  uint32_t ssrc = 0;
  memcpy(&ssrc, buffer + 8, sizeof(ssrc));
  ssrc = ntohl(ssrc);
  SfuPublisherTrack *track;
  if (peer->config) track = sNegotiatedTrack(shard, peer, buffer, length, ssrc);
  else if (peer->room) track = SfuShardPublishedTrack(shard, peer, ssrc);
  else track = SfuPeerTrackAppend(peer, ssrc, SFU_TRACK_AUDIO);
  if (peer->room && !track) {
    counters->srtp_bad_packets++;
    return SFU_MEDIA_DROPPED;
  }
  const uint16_t sequence = (uint16_t)((buffer[2] << 8) | buffer[3]);
  uint64_t retained_serial = 0;
  if (track) {
    const uint32_t timestamp = ((uint32_t)buffer[4] << 24) | ((uint32_t)buffer[5] << 16) |
                               ((uint32_t)buffer[6] << 8) | (uint32_t)buffer[7];
    SfuTrackOnRtp(track, sequence, timestamp, arrival_ns);
    if (track->kind == SFU_TRACK_AUDIO) sUpdateAudioLevel(counters, track, buffer, length, arrival_ns);
    retained_serial = SfuShardNackRetain(shard, track, buffer, length, sequence);
  }

  /* A room publisher fans out to its subscribers; a lone peer gets its own
   * media back, which is what the local probe and the single-peer tests use. */
  if (peer->room) {
    sFanOut(shard, peer, buffer, length, payload_size, ssrc, sequence, retained_serial);
    return SFU_MEDIA_DROPPED;
  }

  size_t protected_length = SFU_PACKET_CAPACITY;
  if (srtp_protect(peer->outbound, buffer, length, buffer, &protected_length, 0) != srtp_err_status_ok) {
    counters->srtp_protect_failures++;
    return SFU_MEDIA_DROPPED;
  }
  counters->srtp_protect_ok++;
  counters->media_echoed++;

  *reply_length = protected_length;
  return SFU_MEDIA_ECHO;
}
