/**
 * @file sfu_shard_subscription.h
 * @brief The shard's outbound-SSRC index and its allocator.
 *
 * RTCP arrives from a subscriber carrying the SSRC the subscriber receives, not
 * the publisher's. This index is the one-probe translation: outbound SSRC to
 * (peer slot, subscription index). It is fixed-size and allocated with the
 * shard, because the alternative is scanning peers on a path a client controls
 * the packet rate of.
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

#ifndef UFSRVCORELIB_SFU_SHARD_SUBSCRIPTION_H
#define UFSRVCORELIB_SFU_SHARD_SUBSCRIPTION_H

#include <stdbool.h>
#include <stdint.h>

#include "model/shard_type.h"

/** Upper bound the allocator will probe for a free SSRC before giving up. */
#define SFU_OUTBOUND_ALLOCATE_ATTEMPTS 1024u

/**
 * @brief Initialise the index over a caller-owned, zeroed entry array.
 *
 * @param shard     Shard whose subscriptions group is being initialised.
 * @param entries   Zeroed array of @p capacity entries.
 * @param capacity  Power-of-two entry count, sized at onboarding.
 * @param seed      Seed for the SSRC counter; the counter starts non-zero.
 */
void SfuShardSubscriptionsInit(SfuShard *shard, SfuOutboundEntry *entries, size_t capacity, uint64_t seed);

/**
 * @brief Next outbound SSRC the shard may assign; advances the cursor.
 *
 * Skips values already present in the outbound index or in the publisher SSRC
 * index, so an assigned value cannot be ambiguous in a counter or a log.
 *
 * @return A free SSRC, or 0 when the attempts budget is exhausted.
 */
uint32_t SfuShardOutboundAllocate(SfuShard *shard);

/**
 * @brief Bind @p out_ssrc to a subscription.
 *
 * @return False when the index is full, or the SSRC already belongs to a
 *         different subscription. Re-binding the same pair is idempotent.
 */
bool SfuShardOutboundInsert(SfuShard *shard, uint32_t out_ssrc, uint32_t slot, uint8_t subscription_index);

/**
 * @brief Resolve an outbound SSRC to its subscription.
 *
 * @param slot                Receives the subscriber's peer slot on a hit.
 * @param subscription_index  Receives the subscription index on a hit.
 * @return False when the SSRC is not bound.
 */
bool SfuShardOutboundLookup(const SfuShard *shard, uint32_t out_ssrc, uint32_t *slot, uint8_t *subscription_index);

/** @brief Drop the binding for @p out_ssrc. False when it was not bound. */
bool SfuShardOutboundRemove(SfuShard *shard, uint32_t out_ssrc);

/** @brief Drop every binding held by @p slot, as leave and release require. */
void SfuShardOutboundRemovePeer(SfuShard *shard, uint32_t slot);

#endif /* UFSRVCORELIB_SFU_SHARD_SUBSCRIPTION_H */
