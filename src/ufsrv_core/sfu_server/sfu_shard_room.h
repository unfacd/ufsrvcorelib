/**
 * @file sfu_shard_room.h
 * @brief Rooms, membership and the publisher SSRC index.
 *
 * A room is owned by the shard the signalling control plane placed it on. The
 * member list is the only mutable part the media path reads, and it is changed
 * only by the shard thread itself, between receive turns. The SSRC index maps a
 * publisher's SSRC to its peer slot so an inbound RTP packet can be attributed
 * without scanning peers.
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

#ifndef UFSRVCORELIB_SFU_SHARD_ROOM_H
#define UFSRVCORELIB_SFU_SHARD_ROOM_H

#include <stdbool.h>
#include <stdint.h>

#include <ufsrvcorelib/ufsrv_core/sfu_server/model/command_type.h>

#include "model/shard_type.h"
#include "sfu_peer_config_priv.h"

SfuPeerConfigResult SfuShardApplyPeerConfig(SfuShard *shard, const SfuPeerConfigSnapshot *config,
                                           BufferDescriptor *ice);
SfuPeer *SfuShardPeerByKey(SfuShard *shard, const char *key, size_t length);

/**
 * @brief Create a room on this shard.
 *
 * @param shard   Shard to create it on; shard-thread only.
 * @param room_id Signalling-assigned id; duplicates and zero are rejected, as is
 *                a room when the bounded index is at rooms.max.
 */
void SfuShardCreateRoom(SfuShard *shard, uint64_t room_id);

enum { SFU_SPEAKER_SWEEP_MS = 50 };
void SfuShardSpeakerSweep(SfuShard *shard, uint64_t now_ms);
void SfuShardDescribeRoom(const SfuShard *shard, uint64_t room_id, BufferDescriptor *reply);

/**
 * @brief Attach a registered peer to a room.
 *
 * @param shard         Shard holding both.
 * @param room_id       Room to join.
 * @param ufrag         Peer's ICE ufrag, the shard's identity for it.
 * @param ufrag_length  Length of @p ufrag.
 *
 * A peer may belong to one room only; the room's member array is sized for the
 * shard's whole peer capacity, so a join cannot overrun it.
 */
void SfuShardJoinRoom(SfuShard *shard, uint64_t room_id, const char *ufrag, size_t ufrag_length);

/**
 * @brief Publish one SSRC from a peer that is already a member of the room.
 *
 * @param shard        Shard holding the peer and the room.
 * @param room_id      Room the peer belongs to.
 * @param ufrag        Publisher's ufrag.
 * @param ufrag_length Length of @p ufrag.
 * @param ssrc         SSRC being published; zero is rejected.
 * @param kind         Audio or video.
 * @param clock_rate   RTP units per second, from the endpoint's rtpmap; 0 uses
 *                     the kind's default (90 kHz video, 48 kHz audio).
 */
void SfuShardAddTrack(SfuShard *shard, uint64_t room_id, const char *ufrag, size_t ufrag_length, uint32_t ssrc,
                      SfuTrackKind kind, uint32_t clock_rate);

/**
 * Map one publisher track (by SSRC) onto an outbound SSRC for @p subscriber.
 * Both peers must be members of the same room and the publisher must have
 * registered that SSRC with ADD_TRACK.
 */
void SfuShardSubscribe(SfuShard *shard, uint64_t room_id, const char *subscriber_ufrag, size_t ufrag_length,
                       uint32_t publisher_ssrc, uint32_t out_ssrc, uint16_t seq_offset);

/** Bind an authenticated publisher's unbound negotiated section and its pending subscriptions. */
SfuPublisherTrack *SfuShardBindPublication(SfuShard *shard, SfuPeer *publisher, size_t media_index, uint32_t ssrc);

/** Resolve a published track owned by the given authenticated peer. */
SfuPublisherTrack *SfuShardPublishedTrack(const SfuShard *shard, SfuPeer *publisher, uint32_t ssrc);

/** Find the subscription of @p subscriber for @p publisher_ssrc. NULL if none. */
SfuSubscription *SfuShardSubscriptionFor(SfuPeer *subscriber, uint32_t publisher_ssrc);

/**
 * @brief Remove a peer from its room and drop every SSRC it published.
 *
 * The room is released when it loses its last member, so an empty room is never
 * kept alive by the index.
 *
 * @param shard Shard holding the peer.
 * @param peer  Peer being detached; NULL and a peer outside a room are no-ops.
 */
void SfuShardRoomDetachPeer(SfuShard *shard, SfuPeer *peer);

/** Insert one publisher SSRC into the shard index. False when full or duplicate. */
bool SfuShardSsrcInsert(SfuShard *shard, uint32_t ssrc, uint32_t slot);

/** Remove every SSRC published by @p slot from the shard index. */
void SfuShardSsrcRemovePeer(SfuShard *shard, uint32_t slot);

/** Resolve a publisher SSRC to its peer slot. */
bool SfuShardSsrcLookup(const SfuShard *shard, uint32_t ssrc, uint32_t *slot);

#endif /* UFSRVCORELIB_SFU_SHARD_ROOM_H */
