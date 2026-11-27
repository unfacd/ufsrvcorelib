/**
 * @file sfu_shard_subscription.c
 * @brief Outbound-SSRC index: open addressing, backward-shift delete, and the allocator.
 *
 * The index answers one question on the RTCP path: given the SSRC a subscriber
 * put in an RTCP packet, which peer and which of its subscriptions own it. Keys
 * are the SSRCs we handed out, values are indices, and the load ceiling matches
 * the publisher index (75 %) so a probe chain stays short.
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

#include <string.h>

#include "sfu_server_priv.h"
#include "sfu_shard_room.h"
#include "sfu_shard_subscription.h"

/** Same avalanche the publisher index uses; keys are SSRCs in both cases. */
static uint32_t
sOutboundHash(uint32_t ssrc)
{
  ssrc ^= ssrc >> 16;
  ssrc *= 0x7feb352du;
  ssrc ^= ssrc >> 15;
  ssrc *= 0x846ca68bu;
  ssrc ^= ssrc >> 16;
  return ssrc;
}

void
SfuShardSubscriptionsInit(SfuShard *shard, SfuOutboundEntry *entries, size_t capacity, uint64_t seed)
{
  if (!shard || !entries || capacity == 0 || (capacity & (capacity - 1u)) != 0) return;
  memset(entries, 0, capacity * sizeof(*entries));
  shard->subscriptions.index = entries;
  shard->subscriptions.mask = capacity - 1u;
  shard->subscriptions.count = 0;
  /* Non-zero, so a freshly initialised shard can never "allocate" SSRC 0. */
  shard->subscriptions.next_ssrc = (uint32_t)(seed ^ (seed >> 32)) | 1u;
}

uint32_t
SfuShardOutboundAllocate(SfuShard *shard)
{
  if (!shard || !shard->subscriptions.index) return 0;

  const size_t mask = shard->subscriptions.mask;
  for (unsigned attempt = 0; attempt < SFU_OUTBOUND_ALLOCATE_ATTEMPTS; attempt++) {
    uint32_t candidate = shard->subscriptions.next_ssrc++;
    if (!candidate) continue; /* zero is never a valid SSRC */
    uint32_t publisher_slot = 0;
    if (SfuShardSsrcLookup(shard, candidate, &publisher_slot)) continue;
    if (SfuShardOutboundLookup(shard, candidate, NULL, NULL)) continue;

    /* Cheap sanity: the index has room for one more binding at 75 % load. */
    if ((shard->subscriptions.count + 1u) * 4u > (mask + 1u) * 3u) return 0;
    return candidate;
  }
  return 0;
}

bool
SfuShardOutboundInsert(SfuShard *shard, uint32_t out_ssrc, uint32_t slot, uint8_t subscription_index)
{
  if (!shard || !out_ssrc || !shard->subscriptions.index) return false;

  /* An existing binding is answered before the load test, so a re-bind at the
   * ceiling is idempotent rather than refused. */
  uint32_t bound_slot = 0;
  uint8_t  bound_subscription = 0;
  if (SfuShardOutboundLookup(shard, out_ssrc, &bound_slot, &bound_subscription)) {
    return bound_slot == slot && bound_subscription == subscription_index;
  }
  if ((shard->subscriptions.count + 1u) * 4u > (shard->subscriptions.mask + 1u) * 3u) return false;

  const size_t mask = shard->subscriptions.mask;
  size_t       index = sOutboundHash(out_ssrc) & mask;
  for (size_t probe = 0; probe <= mask; probe++) {
    SfuOutboundEntry *entry = &shard->subscriptions.index[index];
    if (!entry->peer_slot_plus_one) {
      entry->out_ssrc = out_ssrc;
      entry->peer_slot_plus_one = slot + 1u;
      entry->subscription_index = subscription_index;
      shard->subscriptions.count++;
      return true;
    }
    index = (index + 1) & mask;
  }
  return false;
}

bool
SfuShardOutboundLookup(const SfuShard *shard, uint32_t out_ssrc, uint32_t *slot, uint8_t *subscription_index)
{
  if (!shard || !out_ssrc || !shard->subscriptions.index) return false;

  const size_t mask = shard->subscriptions.mask;
  size_t       index = sOutboundHash(out_ssrc) & mask;
  for (size_t probe = 0; probe <= mask; probe++) {
    const SfuOutboundEntry *entry = &shard->subscriptions.index[index];
    if (!entry->peer_slot_plus_one) return false;
    if (entry->out_ssrc == out_ssrc) {
      if (slot) *slot = entry->peer_slot_plus_one - 1u;
      if (subscription_index) *subscription_index = entry->subscription_index;
      return true;
    }
    index = (index + 1) & mask;
  }
  return false;
}

bool
SfuShardOutboundRemove(SfuShard *shard, uint32_t out_ssrc)
{
  if (!shard || !out_ssrc || !shard->subscriptions.index) return false;

  const size_t mask = shard->subscriptions.mask;
  size_t       index = sOutboundHash(out_ssrc) & mask;
  for (size_t probe = 0; probe <= mask; probe++) {
    SfuOutboundEntry *entry = &shard->subscriptions.index[index];
    if (!entry->peer_slot_plus_one) return false;
    if (entry->out_ssrc == out_ssrc) {
      for (size_t next = (index + 1) & mask; shard->subscriptions.index[next].peer_slot_plus_one;
           next = (next + 1) & mask) {
        SfuOutboundEntry *following = &shard->subscriptions.index[next];
        const size_t home = sOutboundHash(following->out_ssrc) & mask;
        if (((index - home) & mask) >= ((next - home) & mask)) continue;
        *entry = *following;
        entry = following;
        index = next;
      }
      memset(entry, 0, sizeof(*entry));
      shard->subscriptions.count--;
      return true;
    }
    index = (index + 1) & mask;
  }
  return false;
}

void
SfuShardOutboundRemovePeer(SfuShard *shard, uint32_t slot)
{
  if (!shard || !shard->subscriptions.index) return;

  const size_t   mask = shard->subscriptions.mask;
  const uint32_t wanted = slot + 1u;
  size_t         index = 0;
  while (index <= mask) {
    const SfuOutboundEntry *entry = &shard->subscriptions.index[index];
    if (!entry->peer_slot_plus_one || entry->peer_slot_plus_one != wanted) {
      index++;
      continue;
    }
    /* Backward-shift deletion can move a neighbour into this slot, so the same
     * index is inspected again rather than advanced. */
    const uint32_t out_ssrc = entry->out_ssrc;
    (void)SfuShardOutboundRemove(shard, out_ssrc);
  }
}
