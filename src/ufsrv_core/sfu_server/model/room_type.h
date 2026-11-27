/**
 * @file room_type.h
 * @brief One conference room, identified by the id signalling assigned to it.
 *
 * RoomCreate arrives from the signalling server with the room id; the room
 * itself is transient media state owned by the shard that was assigned the
 * room. The member list is a snapshot: replacing it is the only mutation
 * visible to a fan-out, so a reader never sees a half-built list.
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

#ifndef UFSRVCORELIB_ROOM_TYPE_H
#define UFSRVCORELIB_ROOM_TYPE_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief One conference room, owned by the shard that was assigned it.
 *
 * Allocated when the room is created and freed when the last member leaves, so
 * an absent room costs nothing. The member array is part of the same allocation.
 */
typedef struct SfuRoom {
  uint64_t          speaker_generation;
  uint64_t          challenger_generation;
  uint64_t          challenger_since_ms;
  uint32_t          speaker_ssrc;
  uint64_t          room_id;         ///< assigned by the signalling server
  struct SfuPeer ** members;         ///< members; never mutated in place
  size_t            member_count;    ///< live members
  size_t            member_capacity; ///< peer_capacity, the shard's ceiling
} SfuRoom;

#endif /* UFSRVCORELIB_ROOM_TYPE_H */
