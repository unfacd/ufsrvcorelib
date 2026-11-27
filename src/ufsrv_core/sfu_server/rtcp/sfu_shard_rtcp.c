/**
 * @file sfu_shard_rtcp.c
 * @brief SRTCP unprotect, compound walk and the counters that make it visible.
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

#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#include <srtp3/srtp.h>

#include "sfu_server_priv.h"
#include "rtcp/sfu_rtcp_parse.h"
#include "rtcp/sfu_shard_rtcp.h"
#include "sfu_shard_nack.h"
#include "sfu_shard_room.h"
#include "sfu_shard_subscription.h"
#include "sfu_shard_track.h"

#define SFU_RTCP_SR_BYTES 28u        /* header + sender info, no report blocks */
#define SFU_RTCP_MAX_TRAILER 20u     /* SRTCP index plus the largest tag we negotiate */

static size_t sPutSdes(uint8_t *out, size_t capacity, const SfuShard *shard, uint32_t sender_ssrc);
static void sPut32(uint8_t *out, uint32_t value);

/** What the visitor needs: the peer whose tracks an SR may update, a clock, and
 * whether this compound carried SDES or relayed anything. */
typedef struct SfuRtcpContext {
  SfuShard *shard;
  SfuPeer * peer;
  uint64_t  arrival_ns;
  uint64_t  now_ms;
  bool      sdes_seen;
  bool      sr_relayed;
} SfuRtcpContext;

static uint64_t
sMonotonicNs(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

/**
 * @brief Build, protect and send one PLI toward @p publisher, stamping the throttle.
 *
 * The throttle is stamped only after the datagram leaves: a refused protect or a
 * failed send must not silence the next request for a whole window.
 */
static bool
sSendPli(SfuShard *shard, SfuPeer *publisher, SfuPublisherTrack *track, uint32_t publisher_ssrc, uint64_t now_ms)
{
  SfuCounters *counters = shard->spec->counters;

  /* Compound, not reduced-size: RFC 3550 asks a compound to open with SR/RR,
   * so an empty receiver report precedes the PLI. */
  uint8_t pli[8u + 12u + 64u + SFU_RTCP_MAX_TRAILER] = {0};
  pli[0] = 0x80; /* V=2, RC=0: an empty receiver report */
  pli[1] = 201u;
  pli[2] = 0;
  pli[3] = 1; /* 8 bytes */

  pli[8] = 0x81; /* V=2, FMT 1 (PLI) */
  pli[9] = 206u;
  pli[10] = 0;
  pli[11] = 2; /* 12 bytes */

  const uint32_t local_ssrc = htonl(shard->rtcp.local_ssrc);
  const uint32_t target_ssrc = htonl(publisher_ssrc);
  memcpy(pli + 4, &local_ssrc, sizeof(local_ssrc));
  memcpy(pli + 12, &local_ssrc, sizeof(local_ssrc));
  memcpy(pli + 16, &target_ssrc, sizeof(target_ssrc));

  size_t length = 8u + 12u;
  const size_t sdes = sPutSdes(pli + length, sizeof(pli) - SFU_RTCP_MAX_TRAILER - length, shard, shard->rtcp.local_ssrc);
  if (!sdes) {
    counters->drops_send++;
    return false;
  }
  length += sdes;
  if (!SfuSrtcpProtect(publisher->outbound, publisher->dtls.srtp_profile, pli, sizeof(pli), &length)) {
    counters->srtp_protect_failures++;
    return false;
  }
  if (sendto(shard->fd, pli, length, 0, (const struct sockaddr *)&publisher->address, publisher->address_length) < 0) {
    counters->drops_send++;
    return false;
  }
  counters->tx_rtcp++;
  counters->rtcp_pli_forwarded++;
  track->last_pli_ms = now_ms;
  return true;
}

/**
 * @brief One keyframe request per publisher per throttle window.
 *
 * A request inside the window is suppressed and counted, never queued: the PLI
 * that opened the window is already in flight and its keyframe reaches the new
 * subscriber too, so fifty joins must not become fifty PLIs. A suppressed
 * request deliberately does not stamp the throttle, or joins trickling in just
 * inside every window would starve each later request.
 */
static void
sRequestKeyframe(SfuShard *shard, SfuPeer *publisher, SfuPublisherTrack *track, uint32_t publisher_ssrc,
                 uint64_t now_ms)
{
  if (track->last_pli_ms && now_ms - track->last_pli_ms < shard->rtcp.pli_min_ms) {
    shard->spec->counters->rtcp_pli_suppressed++;
    return;
  }
  (void)sSendPli(shard, publisher, track, publisher_ssrc, now_ms);
}

/**
 * @brief Forward one PLI (or a translated FIR) to the publisher of a track.
 *
 * The subscriber names the SSRC it receives, so the outbound index is the only
 * correct translation. Everything is refused with a counter rather than
 * guessed: an unknown outbound SSRC, an inactive subscription, a subscriber
 * asking about its own track, a publisher whose track is gone, or a publisher
 * without a nominated tuple. Throttling is keyed by publisher, so N subscribers
 * become one PLI per window.
 */
/**
 * @brief Resolve the publisher and track behind an outbound SSRC a subscriber named.
 *
 * The subscriber names the SSRC it receives, so the outbound index is the only
 * correct translation. Everything is refused with a counter rather than
 * guessed: an unknown outbound SSRC, an inactive subscription, a subscriber
 * asking about its own track, a publisher whose track is gone, or a publisher
 * without a nominated tuple.
 *
 * @param shard         Shard holding both peers.
 * @param subscriber    Peer that named @p media_ssrc.
 * @param media_ssrc    The SSRC the subscriber receives.
 * @param publisher_out Set to the publisher when the resolution succeeds.
 * @return The publisher's track, or NULL with `rtcp_pli_dropped` counted.
 */
static SfuPublisherTrack *
sResolvePublisherTrack(SfuShard *shard, SfuPeer *subscriber, uint32_t media_ssrc, SfuPeer **publisher_out)
{
  SfuCounters *counters = shard->spec->counters;

  uint32_t slot = 0;
  uint8_t  subscription_index = 0;
  if (!SfuShardOutboundLookup(shard, media_ssrc, &slot, &subscription_index) ||
      subscription_index >= SFU_PEER_SUBS_MAX || &shard->peers.slots[slot] != subscriber) {
    counters->rtcp_pli_dropped++;
    return NULL;
  }
  const SfuSubscription *subscription = &subscriber->subscriptions[subscription_index];
  if (!subscription->active) {
    counters->rtcp_pli_dropped++;
    return NULL;
  }

  uint32_t publisher_slot = 0;
  if (!SfuShardSsrcLookup(shard, subscription->publisher_ssrc, &publisher_slot)) {
    counters->rtcp_pli_dropped++;
    return NULL;
  }
  SfuPeer *publisher = &shard->peers.slots[publisher_slot];
  if (publisher == subscriber || publisher->state != SFU_PEER_STATE_CONNECTED || !publisher->outbound ||
      !publisher->address_length) {
    counters->rtcp_pli_dropped++;
    return NULL;
  }
  SfuPublisherTrack *track = SfuPeerTrackFor(publisher, subscription->publisher_ssrc);
  if (!track) {
    counters->rtcp_pli_dropped++;
    return NULL;
  }
  if (publisher_out) *publisher_out = publisher;
  return track;
}

static void
sForwardPli(SfuShard *shard, SfuPeer *subscriber, uint32_t media_ssrc, uint64_t now_ms)
{
  SfuPeer *publisher = NULL;
  SfuPublisherTrack *track = sResolvePublisherTrack(shard, subscriber, media_ssrc, &publisher);
  if (!track) return;
  sRequestKeyframe(shard, publisher, track, track->ssrc, now_ms);
}

/**
 * @brief Ask the publisher of a freshly bound subscription for a keyframe.
 *
 * Called from the subscribe path, so the publisher and its track come from the
 * binding being created rather than from the outbound index, which is still
 * being written. The request carries the publisher's own SSRC and obeys the
 * same per-publisher window as a subscriber-driven PLI.
 *
 * @param shard           Shard applying the subscription.
 * @param publisher       Peer whose track is being subscribed to.
 * @param publisher_ssrc  The SSRC the subscriber asked for.
 */
void
SfuShardPliOnJoin(SfuShard *shard, SfuPeer *publisher, uint32_t publisher_ssrc)
{
  if (!shard || !publisher || !publisher_ssrc) return;
  SfuCounters *counters = shard->spec->counters;

  SfuPublisherTrack *track = SfuPeerTrackFor(publisher, publisher_ssrc);
  if (!track) {
    counters->rtcp_pli_dropped++;
    return;
  }
  /* Audio codecs are frame-independent: there is no keyframe to ask for. */
  if (track->kind == SFU_TRACK_AUDIO) return;
  counters->pli_on_join++;

  if (publisher->state != SFU_PEER_STATE_CONNECTED || !publisher->outbound || !publisher->address_length ||
      !publisher->room) {
    counters->rtcp_pli_dropped++;
    return;
  }
  sRequestKeyframe(shard, publisher, track, publisher_ssrc, sMonotonicNs() / 1000000u);
}

/**
 * @brief Ask for a keyframe after a subscriber's NACK missed the retention ring.
 *
 * A miss means the subscriber lost a packet the ring can no longer produce, and
 * only a fresh keyframe restores decodability. The request shares the
 * per-publisher window with PLI-on-join and subscriber-driven PLI, so one lossy
 * viewer cannot storm the room's encoder. Audio is skipped: frames are
 * independent and a keyframe request has no meaning.
 */
static void
sRequestKeyframeOnLoss(SfuShard *shard, SfuPeer *subscriber, uint32_t media_ssrc, uint64_t now_ms)
{
  SfuPeer *publisher = NULL;
  SfuPublisherTrack *track = sResolvePublisherTrack(shard, subscriber, media_ssrc, &publisher);
  if (!track) return;
  if (track->kind == SFU_TRACK_AUDIO) return;
  shard->spec->counters->pli_on_loss++;
  sRequestKeyframe(shard, publisher, track, track->ssrc, now_ms);
}

/**
 * @brief Relay one publisher SR to every subscriber of that track.
 *
 * Report blocks are stripped (RC 0, six words), the sender SSRC becomes the
 * subscriber's outbound SSRC, and the RTP timestamp carries that subscription's
 * offset so the NTP/RTP pair stays valid when stage 5 starts offsetting
 * timestamps. The NTP timestamp remains the publisher's capture clock. The
 * packet count, octet count and SDES chunk describe the subscriber's stream.
 */
static void
sRelaySenderReport(SfuShard *shard, SfuPeer *publisher, const SfuRtcpEvent *event, SfuRtcpContext *context)
{
  SfuCounters *counters = shard->spec->counters;
  SfuRoom *    room = publisher->room;
  if (!room || event->packet_length < SFU_RTCP_SR_BYTES) return;

  for (size_t member_index = 0; member_index < room->member_count; member_index++) {
    SfuPeer *member = room->members[member_index];
    if (!member || member == publisher || !member->outbound || !member->address_length) continue;

    for (size_t index = 0; index < SFU_PEER_SUBS_MAX; index++) {
      const SfuSubscription *subscription = &member->subscriptions[index];
      if (!subscription->active || subscription->publisher_ssrc != event->sender_ssrc) continue;

      uint8_t relay[SFU_RTCP_SR_BYTES + 64u + SFU_RTCP_MAX_TRAILER];
      memcpy(relay, event->packet, SFU_RTCP_SR_BYTES);
      relay[0] = (uint8_t)(relay[0] & 0xC0u); /* V stays, P and RC are cleared */
      relay[2] = 0;
      relay[3] = 6; /* 28 bytes */
      const uint32_t relay_ssrc = htonl(subscription->out_ssrc);
      memcpy(relay + 4, &relay_ssrc, sizeof(relay_ssrc));
      uint32_t timestamp = 0;
      memcpy(&timestamp, relay + 16, sizeof(timestamp));
      timestamp = htonl(ntohl(timestamp) + subscription->ts_offset);
      memcpy(relay + 16, &timestamp, sizeof(timestamp));
      sPut32(relay + 20, subscription->sent_packets);
      sPut32(relay + 24, subscription->sent_octets);

      size_t length = SFU_RTCP_SR_BYTES;
      const size_t sdes =
          sPutSdes(relay + length, sizeof(relay) - SFU_RTCP_MAX_TRAILER - length, shard, subscription->out_ssrc);
      if (!sdes) {
        counters->drops_send++;
        continue;
      }
      length += sdes;
      if (!SfuSrtcpProtect(member->outbound, member->dtls.srtp_profile, relay, sizeof(relay), &length)) {
        counters->srtp_protect_failures++;
        continue;
      }
      if (sendto(shard->fd, relay, length, 0, (const struct sockaddr *)&member->address, member->address_length) >= 0) {
        counters->tx_rtcp++;
        counters->sr_relayed++;
        context->sr_relayed = true;
      } else {
        counters->drops_send++;
      }
    }
  }
}

/** One packet of the compound; every branch is a counter, none allocates. */
static bool
sVisitRtcpPacket(void *context, const SfuRtcpEvent *event)
{
  SfuRtcpContext *rtcp = (SfuRtcpContext *)context;
  SfuCounters *   counters = rtcp->shard->spec->counters;

  switch (event->kind) {
  case SFU_RTCP_KIND_SR: {
    counters->sr_received++;
    /* Cache the timing pair the RR's DLSR needs, then relay to subscribers. */
    SfuPublisherTrack *track = rtcp->peer->room
                                   ? SfuShardPublishedTrack(rtcp->shard, rtcp->peer, event->sender_ssrc)
                                   : SfuPeerTrackFor(rtcp->peer, event->sender_ssrc);
    if (!track) break;
    track->last_sr_ntp_mid32 = event->ntp_mid32;
    track->last_sr_rx_ns = rtcp->arrival_ns;
    track->sr_seen = true;
    sRelaySenderReport(rtcp->shard, rtcp->peer, event, rtcp);
    break;
  }
  case SFU_RTCP_KIND_RR: counters->rr_received++; break;
  case SFU_RTCP_KIND_SDES: rtcp->sdes_seen = true; break; /* not relayed, by decision */
  case SFU_RTCP_KIND_BYE: counters->rtcp_bye_received++; break;
  case SFU_RTCP_KIND_NACK:
    counters->rtcp_nack_received++;
    counters->nack_requests += event->nack_requests;
    /* A miss is the one loss signal that means the subscriber can no longer
     * decode: ask once per event, through the same per-publisher window. */
    if (SfuShardNackAnswer(rtcp->shard, rtcp->peer, event, rtcp->arrival_ns)) {
      sRequestKeyframeOnLoss(rtcp->shard, rtcp->peer, event->media_ssrc, rtcp->now_ms);
    }
    break;
  case SFU_RTCP_KIND_PLI:
    counters->rtcp_pli_received++;
    sForwardPli(rtcp->shard, rtcp->peer, event->media_ssrc, rtcp->now_ms);
    break;
  case SFU_RTCP_KIND_FIR:
    /* FIR's target lives in its FCI; one PLI needs neither ITS sequence number
     * nor FIR state at the publisher, so the request is translated. */
    counters->rtcp_fir_received++;
    sForwardPli(rtcp->shard, rtcp->peer, event->media_ssrc, rtcp->now_ms);
    break;
  case SFU_RTCP_KIND_UNKNOWN:
  default: counters->rtcp_unknown_type++; break;
  }
  return true;
}

/** xorshift32: interval randomisation needs no cryptographic quality. */
static uint32_t
sNextJitter(SfuShard *shard)
{
  uint32_t state = shard->rtcp.jitter_state;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  shard->rtcp.jitter_state = state;
  return state;
}

/** Big-endian writers for the numeric fields of a report block. */
static void
sPut16(uint8_t *out, uint16_t value)
{
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
}

/**
 * @brief Write one SDES packet carrying the shard's CNAME.
 *
 * A compound RTCP packet opens with SR/RR and must carry an SDES CNAME for its
 * sender (RFC 3550 §6.1). The chunk carries the report sender's SSRC, a CNAME,
 * the list terminator and padding to a 32-bit boundary.
 *
 * @return Bytes written, or 0 when @p capacity is too small.
 */
static size_t
sPutSdes(uint8_t *out, size_t capacity, const SfuShard *shard, uint32_t sender_ssrc)
{
  const size_t cname_length = shard->rtcp.cname_length;
  const size_t body = 4u + 2u + cname_length + 1u; /* ssrc, item header, value, END */
  const size_t padded = (body + 3u) & ~(size_t)3u;
  const size_t total = 4u + padded;
  if (!out || total > capacity || cname_length > 255u) return 0;

  memset(out, 0, total);
  out[0] = 0x81; /* V=2, one chunk */
  out[1] = 202u; /* SDES */
  const uint16_t words = (uint16_t)((total / 4u) - 1u);
  out[2] = (uint8_t)(words >> 8);
  out[3] = (uint8_t)words;

  const uint32_t ssrc = htonl(sender_ssrc);
  memcpy(out + 4, &ssrc, sizeof(ssrc));
  out[8] = 1; /* CNAME */
  out[9] = (uint8_t)cname_length;
  memcpy(out + 10, shard->rtcp.cname, cname_length);
  out[10 + cname_length] = 0; /* END */
  return total;
}

static void
sPut32(uint8_t *out, uint32_t value)
{
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
}

/**
 * @brief Build and send one receiver report for @p track toward @p peer.
 *
 * The report is 32 bytes: the header, the shard's own SSRC, and one report
 * block about the publisher's stream. Fraction lost covers the interval since
 * the previous report; the cumulative count is clamped to 24 bits, signed, as
 * the field is.
 */
static void
sSendReceiverReport(SfuShard *shard, SfuPeer *peer, SfuPublisherTrack *track, uint64_t now_ns)
{
  SfuCounters *counters = shard->spec->counters;
  if (!peer->outbound || !peer->address_length) return;

  const uint32_t expected = track->max_seq_ext - track->base_seq + 1u;
  const int64_t  lost_total = (int64_t)expected - (int64_t)track->packets_received;
  int32_t cumulative = 0;
  if (lost_total > 0x7FFFFF) {
    cumulative = 0x7FFFFF;
  } else if (lost_total < -0x800000) {
    cumulative = -0x800000;
  } else {
    cumulative = (int32_t)lost_total;
  }

  const uint32_t expected_interval = expected - (uint32_t)track->expected_prior;
  const uint32_t received_interval = (uint32_t)track->packets_received - track->received_prior;
  const int32_t  lost_interval = (int32_t)expected_interval - (int32_t)received_interval;
  const uint8_t  fraction = expected_interval && lost_interval > 0
                                 ? (uint8_t)((lost_interval * 256) / (int32_t)expected_interval)
                                 : 0u;

  uint32_t dlsr = 0;
  if (track->sr_seen) {
    const uint64_t age_ns = track->last_sr_rx_ns && now_ns > track->last_sr_rx_ns ? now_ns - track->last_sr_rx_ns : 0;
    const uint64_t delay = (age_ns * 65536u) / 1000000000u;
    dlsr = delay > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)delay;
  }

  /* The buffer must hold the plaintext and the SRTCP trailer: passing the
   * plaintext length as the capacity makes the helper refuse, correctly. */
  uint8_t  report[32u + 64u + SFU_RTCP_MAX_TRAILER] = {0};
  report[0] = 0x81; /* V=2, RC=1 */
  report[1] = 201u; /* RR: the standard RTCP payload type, not the muxed 73 */
  sPut16(report + 2, 7u);
  const uint32_t local_ssrc = htonl(shard->rtcp.local_ssrc);
  memcpy(report + 4, &local_ssrc, sizeof(local_ssrc));

  const uint32_t report_ssrc = htonl(track->ssrc);
  memcpy(report + 8, &report_ssrc, sizeof(report_ssrc));
  report[12] = fraction;
  report[13] = (uint8_t)((uint32_t)cumulative >> 16);
  report[14] = (uint8_t)((uint32_t)cumulative >> 8);
  report[15] = (uint8_t)(uint32_t)cumulative;
  sPut32(report + 16, track->max_seq_ext);
  sPut32(report + 20, track->jitter_q4 >> 4);
  sPut32(report + 24, track->sr_seen ? track->last_sr_ntp_mid32 : 0u);
  sPut32(report + 28, dlsr);

  size_t length = 32u;
  const size_t sdes = sPutSdes(report + length, sizeof(report) - SFU_RTCP_MAX_TRAILER - length, shard, shard->rtcp.local_ssrc);
  if (!sdes) {
    counters->drops_send++;
    return;
  }
  length += sdes;
  if (!SfuSrtcpProtect(peer->outbound, peer->dtls.srtp_profile, report, sizeof(report), &length)) {
    counters->srtp_protect_failures++;
    return;
  }
  if (sendto(shard->fd, report, length, 0, (const struct sockaddr *)&peer->address, peer->address_length) >= 0) {
    counters->tx_rtcp++;
    counters->rr_sent++;
    track->expected_prior = (int32_t)expected;
    track->received_prior = (uint32_t)track->packets_received;
  } else {
    counters->drops_send++;
  }
}

void
SfuShardRtcpSweep(SfuShard *shard, uint64_t now_ms)
{
  if (!shard || now_ms < shard->rtcp.next_rr_ms) return;

  /* RFC 3550 asks for a randomised interval so shards do not synchronise. */
  shard->rtcp.next_rr_ms = now_ms + shard->rtcp.interval_ms + (sNextJitter(shard) % (shard->rtcp.interval_ms / 2u + 1u));
  const uint64_t now_ns = now_ms * 1000000ull;

  for (size_t slot = 0; slot < shard->spec->peer_capacity; slot++) {
    SfuPeer *peer = &shard->peers.slots[slot];
    if (peer->state != SFU_PEER_STATE_CONNECTED) continue;
    for (size_t index = 0; index < peer->track_count; index++) {
      SfuPublisherTrack *track = &peer->tracks[index];
      if (!track->have_seq) continue;

      /* A publisher silent for too long is counted once, never synthesised. */
      if (!track->sr_seen && !track->sr_missing_counted && track->first_rtp_ns &&
          now_ns > track->first_rtp_ns &&
          now_ns - track->first_rtp_ns >= (uint64_t)SFU_RTCP_SR_MISSING_AFTER_MS * 1000000ull) {
        shard->spec->counters->sr_missing++;
        track->sr_missing_counted = true;
      }
      sSendReceiverReport(shard, peer, track, now_ns);
    }
  }
}

SfuMediaOutcome
SfuShardHandleRtcp(SfuShard *shard, SfuPeer *peer, SfuPacket *packet)
{
  SfuCounters *counters = shard->spec->counters;

  if (!peer->inbound) {
    /* RTCP before the DTLS handshake completed: authenticated RTCP cannot exist
     * yet, and this is a different event from a failed auth. */
    counters->rtcp_before_srtp++;
    return SFU_MEDIA_DROPPED;
  }

  uint8_t *buffer = packet->data + SFU_PACKET_HEADROOM;
  /* SRTCP removes the 4-byte index and the profile's tag; the capacity is what
   * the slot can hold, as on the RTP path. */
  size_t plaintext_capacity = SFU_PACKET_CAPACITY;
  const srtp_err_status_t status =
      srtp_unprotect_rtcp(peer->inbound, buffer, packet->length, buffer, &plaintext_capacity);
  switch (status) {
  case srtp_err_status_ok: break;
  case srtp_err_status_replay_old:
  case srtp_err_status_replay_fail: counters->rtcp_replay++; return SFU_MEDIA_DROPPED;
  default: counters->rtcp_auth_fail++; return SFU_MEDIA_DROPPED;
  }

  if (plaintext_capacity < 8u) {
    counters->rtcp_malformed++;
    return SFU_MEDIA_DROPPED;
  }

  const uint64_t  arrival_ns = sMonotonicNs();
  SfuRtcpContext context = {.shard = shard, .peer = peer, .arrival_ns = arrival_ns, .now_ms = arrival_ns / 1000000u};
  bool            malformed = false;
  const size_t    packets =
      SfuRtcpParseCompound(buffer, plaintext_capacity, sVisitRtcpPacket, &context, &malformed);
  counters->rtcp_packets_parsed += packets;
  if (malformed) counters->rtcp_malformed++;
  /* An SR relayed from a compound without SDES: the peer's CNAME never reached
   * this subscriber, which matters only if that peer binds by CNAME. */
  if (context.sr_relayed && !context.sdes_seen) counters->sr_without_sdes++;
  return SFU_MEDIA_DROPPED; /* nothing to echo: the relay sends on its own */
}
