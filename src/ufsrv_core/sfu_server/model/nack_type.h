/**
 * @file nack_type.h
 * @brief The retention ring one publisher track keeps so a subscriber can ask for a packet again.
 *
 * Entries hold slab indices and never pointers: a retained packet is a slot of
 * the shard's own pool, so a ring costs the pool what it retains and nothing
 * else. The ring is indexed by @c sequence & (capacity - 1) and every hit is
 * checked against the stored sequence, which makes a wrap read as a miss rather
 * than a stale packet.
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

#ifndef UFSRVCORELIB_NACK_TYPE_H
#define UFSRVCORELIB_NACK_TYPE_H

#include <stdint.h>

/** Slots retained per video track: ~800 KB of the shard pool at the current slot size. */
#define SFU_NACK_VIDEO_SLOTS 512u
/** Slots retained per audio track: ~100 KB, enough for a second of 20 ms packets. */
#define SFU_NACK_AUDIO_SLOTS 64u
/** Video capacity granted once the pool is at SFU_NACK_PRESSURE_PCT. */
#define SFU_NACK_PRESSURE_SLOTS 256u
/** Pool utilisation at which new video rings are halved. */
#define SFU_NACK_PRESSURE_PCT 80u
/** Duplicate-burst cooldown: one sequence is not re-answered inside this window. */
#define SFU_NACK_DUP_COOLDOWN_NS (20ull * 1000ull * 1000ull)
/** Fixed window the per-subscription retransmit budget is spent over. */
#define SFU_NACK_RATE_WINDOW_MS 1000u

/** One retained packet: the pool slot that holds it and the sequence it carries. */
typedef struct SfuNackSlot {
  uint64_t serial;       ///< unique retention instance on this shard
  uint32_t slot_plus_one; ///< slot index in the packet pool + 1; 0 when empty
  uint16_t sequence;      ///< publisher sequence stored here, the stale-hit guard
  uint16_t length;        ///< plaintext bytes in the slot
} SfuNackSlot;

/** One ring, plus its link on the shard's descriptor free list. */
typedef struct SfuNackRing {
  SfuNackSlot *slots;    ///< capacity entries, inside the shard's ring pool
  uint32_t     capacity; ///< entries, a power of two; 0 when the ring is on the free list
  uint32_t     stored;   ///< slots currently retained
  uint32_t     next_free;///< descriptor free-list link, index + 1
} SfuNackRing;

#endif /* UFSRVCORELIB_NACK_TYPE_H */
