/**
 * @file sfu_shard_nack.h
 * @brief Retention rings and the retransmission path that answers a subscriber NACK.
 *
 * A track keeps one plaintext copy of each of its last packets, indexed by the
 * publisher's sequence. A NACK names the SSRC the subscriber receives, so the
 * answer resolves the outbound index first, maps the sequence back through the
 * subscription's offset, and protects the copy with the subscriber's own
 * session. The publisher is never involved.
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

#ifndef UFSRVCORELIB_SFU_SHARD_NACK_H
#define UFSRVCORELIB_SFU_SHARD_NACK_H

#include <stddef.h>
#include <stdint.h>

#include "model/shard_type.h"
#include "rtcp/sfu_rtcp_parse.h"

/**
 * @brief Give @p track a retention ring, if the shard can afford one.
 *
 * Idempotent. Capacity follows the track kind and is halved for video once the
 * packet pool is at SFU_NACK_PRESSURE_PCT; a refused grant is counted once on
 * the track and leaves it without retention.
 */
void SfuShardNackAttach(SfuShard *shard, SfuPublisherTrack *track);

/** Return every retained slot and the ring itself. Idempotent; NULL is a no-op. */
void SfuShardNackRelease(SfuShard *shard, SfuPublisherTrack *track);

/**
 * @brief Retain one authenticated plaintext RTP packet on @p track.
 *
 * @param shard    Shard that owns the pool.
 * @param track    Publisher track; a track without a ring is granted one first.
 * @param rtp      Plaintext RTP packet, header included.
 * @param length   Bytes to copy; a packet larger than a slot is not retained.
 * @param sequence Publisher sequence, host order; the ring index and stale guard.
 * @return Identity of the retained plaintext, or zero when retention failed.
 */
uint64_t SfuShardNackRetain(SfuShard *shard, SfuPublisherTrack *track, const uint8_t *rtp, size_t length, uint16_t sequence);

/**
 * @brief Answer one NACK event from @p subscriber.
 *
 * @param shard      Shard holding both peers.
 * @param subscriber Peer that sent the feedback.
 * @param event      Parsed NACK, its FCI expanded here.
 * @param now_ns     Monotonic arrival time, for the budget and the cooldown.
 * @return true when at least one named sequence was not retained; the caller
 *         turns that into a keyframe request, since only retention knows.
 */
bool SfuShardNackAnswer(SfuShard *shard, SfuPeer *subscriber, const SfuRtcpEvent *event, uint64_t now_ns);

#endif /* UFSRVCORELIB_SFU_SHARD_NACK_H */
