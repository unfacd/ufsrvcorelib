/**
 * @file sfu_shard_nack.c
 * @brief Retention rings and the retransmission path that answers a subscriber NACK.
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
#include <string.h>
#include <sys/socket.h>

#include <srtp3/srtp.h>

#include <uflib/slab/inlined_slab.h>

#include "model/counters_type.h"
#include "sfu_shard_nack.h"
#include "sfu_shard_room.h"
#include "sfu_shard_subscription.h"
#include "srtp/sfu_rtp_rewrite.h"

/** Ring index for one sequence; capacity is a power of two. */
static inline SfuNackSlot *
sRingSlot(const SfuNackRing *ring, uint16_t sequence)
{
  return &ring->slots[sequence & (ring->capacity - 1u)];
}

/** Pool slot behind one ring entry. */
static inline SfuPacket *
sPoolSlot(const SfuShard *shard, uint32_t slot_plus_one)
{
  return &shard->spec->slab[slot_plus_one - 1u];
}

static bool
sPoolPressured(const SfuShard *shard)
{
  const InlinedSlab *slab = &shard->packets.slab;
  return slab->leased * 100u >= slab->capacity * SFU_NACK_PRESSURE_PCT;
}

static void
sRefuse(SfuShard *shard, SfuPublisherTrack *track)
{
  if (track->ring_denied) return;
  track->ring_denied = true;
  shard->spec->counters->nack_retention_refused++;
}

void
SfuShardNackAttach(SfuShard *shard, SfuPublisherTrack *track)
{
  if (!shard || !track || track->ring) return;

  const bool video = track->kind != SFU_TRACK_AUDIO;
  uint32_t   capacity = video ? SFU_NACK_VIDEO_SLOTS : SFU_NACK_AUDIO_SLOTS;
  if (video && sPoolPressured(shard)) capacity = SFU_NACK_PRESSURE_SLOTS;

  if (!shard->nack.free_head || !shard->nack.rings) {
    sRefuse(shard, track);
    return;
  }
  const InlinedSlab *slab = &shard->packets.slab;
  if (slab->capacity - slab->leased < capacity) {
    sRefuse(shard, track);
    return;
  }

  SfuNackRing *ring = &shard->nack.rings[shard->nack.free_head - 1u];
  shard->nack.free_head = ring->next_free;
  ring->next_free = 0;
  ring->stored = 0;
  ring->capacity = capacity;
  memset(ring->slots, 0, SFU_NACK_VIDEO_SLOTS * sizeof(SfuNackSlot));
  track->ring = ring;
  track->ring_denied = false;
}

void
SfuShardNackRelease(SfuShard *shard, SfuPublisherTrack *track)
{
  if (!shard || !track || !track->ring) return;

  SfuNackRing *ring = track->ring;
  for (uint32_t index = 0; index < ring->capacity; index++) {
    SfuNackSlot *entry = &ring->slots[index];
    if (!entry->slot_plus_one) continue;
    InlinedSlabPut(&shard->packets.slab, sPoolSlot(shard, entry->slot_plus_one));
  }
  memset(ring->slots, 0, SFU_NACK_VIDEO_SLOTS * sizeof(SfuNackSlot));
  ring->capacity = 0;
  ring->stored = 0;
  ring->next_free = shard->nack.free_head;
  shard->nack.free_head = (uint32_t)(ring - shard->nack.rings) + 1u;

  track->ring = NULL;
  track->ring_denied = false;
}

uint64_t
SfuShardNackRetain(SfuShard *shard, SfuPublisherTrack *track, const uint8_t *rtp, size_t length, uint16_t sequence)
{
  if (!shard || !track || !rtp) return 0;
  if (!track->ring) {
    SfuShardNackAttach(shard, track);
    if (!track->ring) return 0;
  }
  if (!length || length > SFU_PACKET_CAPACITY) {
    shard->spec->counters->drops_truncated++;
    return 0;
  }

  SfuPacket *replacement = InlinedSlabTake(&shard->packets.slab);
  if (!replacement) {
    shard->spec->counters->drops_slab++;
    return 0;
  }

  SfuNackRing *ring = track->ring;
  SfuNackSlot *entry = sRingSlot(ring, sequence);
  if (entry->slot_plus_one) {
    InlinedSlabPut(&shard->packets.slab, sPoolSlot(shard, entry->slot_plus_one));
  } else {
    ring->stored++;
  }

  uint8_t *copy = replacement->data + SFU_PACKET_HEADROOM;
  memcpy(copy, rtp, length);
  replacement->length = (uint16_t)length;

  entry->serial = ++shard->nack.serial;
  entry->slot_plus_one = (uint32_t)(replacement - shard->spec->slab) + 1u;
  entry->sequence = sequence;
  entry->length = (uint16_t)length;
  return entry->serial;
}

static bool
sBudgetSpent(SfuShard *shard, SfuSubscription *subscription, uint16_t out_sequence, uint64_t now_ns)
{
  const uint32_t limit = shard->nack.retransmit_per_second;
  const uint32_t now_ms = (uint32_t)(now_ns / 1000000ull);

  if (!subscription->nack_window_ms || now_ms - subscription->nack_window_ms >= SFU_NACK_RATE_WINDOW_MS) {
    subscription->nack_window_ms = now_ms;
    subscription->nack_window_count = 0;
  }
  if (subscription->nack_window_count >= limit) return true;
  if (subscription->last_nacked_seq == out_sequence && now_ns - subscription->last_nack_ns < SFU_NACK_DUP_COOLDOWN_NS)
    return true;
  return false;
}

/** Copy, rewrite and protect one retained packet toward @p subscriber. */
static bool
sRetransmit(SfuShard *shard, SfuPeer *subscriber, SfuSubscription *subscription, const SfuNackSlot *entry,
            uint16_t out_sequence)
{
  SfuCounters *counters = shard->spec->counters;
  const SfuPacket *retained = sPoolSlot(shard, entry->slot_plus_one);
  const uint8_t *plaintext = retained->data + SFU_PACKET_HEADROOM;
  const size_t length = entry->length;
  size_t payload_size = 0;
  if (!SfuRtpPayloadSize(plaintext, length, &payload_size)) return false;

  SfuRtpMapping mapping = {0};
  if (subscription->sent_mapping) {
    const SfuRtpSentMapping *sent = &subscription->sent_mapping[entry->sequence & (SFU_NACK_VIDEO_SLOTS - 1u)];
    mapping = (SfuRtpMapping){.mid = subscriber->config->media[subscription->subscriber_media_index].mid,
                              .payload_type = sent->payload_type, .mid_extension_id = sent->mid_extension_id};
  }

  SfuPacket *scratch = InlinedSlabTake(&shard->packets.slab);
  if (!scratch) {
    counters->drops_slab++;
    return false;
  }

  uint8_t *out = scratch->data + SFU_PACKET_HEADROOM;
  const size_t rewritten = SfuRtpRewrite(out, plaintext, length, subscription, out_sequence, &mapping);
  if (!rewritten) {
    InlinedSlabPut(&shard->packets.slab, scratch);
    return false;
  }

  size_t protected_length = SFU_PACKET_CAPACITY;
  if (srtp_protect(subscriber->outbound, out, rewritten, out, &protected_length, 0) != srtp_err_status_ok) {
    counters->srtp_protect_failures++;
    InlinedSlabPut(&shard->packets.slab, scratch);
    return false;
  }
  const ssize_t sent = sendto(shard->fd, out, protected_length, 0, (const struct sockaddr *)&subscriber->address,
                              subscriber->address_length);
  InlinedSlabPut(&shard->packets.slab, scratch);

  if (sent < 0) {
    counters->drops_send++;
    return false;
  }
  counters->tx_datagrams++;
  subscription->sent_packets++;
  subscription->sent_octets += (uint32_t)payload_size;
  counters->nack_retransmits++;
  counters->nack_bytes_retransmitted += rewritten;
  return true;
}

static bool
sAnswerSequence(SfuShard *shard, SfuPeer *subscriber, SfuSubscription *subscription, SfuNackRing *ring,
                uint16_t out_sequence, uint64_t now_ns)
{
  SfuCounters *counters = shard->spec->counters;

  if (sBudgetSpent(shard, subscription, out_sequence, now_ns)) {
    counters->nack_rate_limited++;
    return false;
  }

  const uint16_t in_sequence = (uint16_t)(out_sequence - subscription->seq_offset);
  const SfuNackSlot *entry = sRingSlot(ring, in_sequence);
  if (!entry->slot_plus_one || entry->sequence != in_sequence ||
      (subscription->sent_mapping &&
       subscription->sent_mapping[in_sequence & (SFU_NACK_VIDEO_SLOTS - 1u)].serial != entry->serial)) {
    counters->nack_misses++;
    return true;
  }

  if (!sRetransmit(shard, subscriber, subscription, entry, out_sequence)) return false;

  subscription->nack_window_count++;
  subscription->last_nacked_seq = out_sequence;
  subscription->last_nack_ns = now_ns;
  return false;
}

bool
SfuShardNackAnswer(SfuShard *shard, SfuPeer *subscriber, const SfuRtcpEvent *event, uint64_t now_ns)
{
  SfuCounters *counters = shard->spec->counters;

  uint32_t slot = 0;
  uint8_t  subscription_index = 0;
  if (!SfuShardOutboundLookup(shard, event->media_ssrc, &slot, &subscription_index) ||
      slot >= shard->peers.capacity || &shard->peers.slots[slot] != subscriber) {
    counters->nack_dropped += event->nack_requests;
    return false;
  }

  SfuSubscription *subscription = &subscriber->subscriptions[subscription_index];
  if (!subscription->active || !subscriber->outbound || !subscriber->address_length) {
    counters->nack_dropped += event->nack_requests;
    return false;
  }

  uint32_t publisher_slot = 0;
  if (!SfuShardSsrcLookup(shard, subscription->publisher_ssrc, &publisher_slot) ||
      publisher_slot >= shard->peers.capacity) {
    counters->nack_dropped += event->nack_requests;
    return false;
  }
  SfuPeer *publisher = &shard->peers.slots[publisher_slot];
  if (publisher == subscriber || (subscription->publisher && subscription->publisher != publisher)) {
    counters->nack_dropped += event->nack_requests;
    return false;
  }

  SfuPublisherTrack *track = SfuPeerTrackFor(publisher, subscription->publisher_ssrc);
  if (!track || !track->ring || !track->ring->capacity) {
    counters->nack_dropped += event->nack_requests;
    return false;
  }
  if (!shard->nack.retransmit_per_second) {
    counters->nack_dropped += event->nack_requests;
    return false;
  }

  const uint8_t *fci = event->packet + 12;
  bool           missed = false;
  for (uint8_t index = 0; index < event->nack_entries; index++) {
    const uint16_t pid = (uint16_t)((fci[index * 4u] << 8) | fci[index * 4u + 1u]);
    const uint16_t blp = (uint16_t)((fci[index * 4u + 2u] << 8) | fci[index * 4u + 3u]);

    missed |= sAnswerSequence(shard, subscriber, subscription, track->ring, pid, now_ns);
    for (unsigned bit = 0; bit < 16; bit++) {
      if (!(blp & (1u << bit))) continue;
      missed |= sAnswerSequence(shard, subscriber, subscription, track->ring, (uint16_t)(pid + bit + 1u), now_ns);
    }
  }
  return missed;
}
