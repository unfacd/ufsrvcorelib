/**
 * @file sfu_shard_ice.h
 * @brief The shard's STUN/ICE responder: the layer between UDP and DTLS.
 *
 * The responder owns the ICE half of a peer's state: it authenticates binding
 * requests, binds the peer's 4-tuple only after the MESSAGE-INTEGRITY check,
 * enforces the rebind policy, records consent and answers with a binding
 * success response. It never allocates: request and response share the packet
 * slab slot they arrived in.
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

#ifndef UFSRVCORELIB_SFU_SHARD_ICE_H
#define UFSRVCORELIB_SFU_SHARD_ICE_H

#include <ufsrvcorelib/ufsrv_core/sfu_server/events/sfu_disconnect_reason_enum.h>

#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>

#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <ufsrvcorelib/ufsrv_core/sfu_server/model/command_type.h>

#include "model/packet_type.h"
#include "model/shard_type.h"

typedef enum SfuIceOutcome {
  SFU_ICE_DROPPED = 0, ///< nothing to send
  SFU_ICE_REPLY        ///< the packet slot now holds a binding success response
} SfuIceOutcome;

/**
 * Provision one peer from a signalling command: allocate a free slot, copy the
 * credentials and insert the ufrag into the shard index. Rejects duplicates and
 * exhausted capacity, counting both as peer drops.
 */
bool SfuShardRegisterPeer(SfuShard *shard, const SfuCommand *command);

/** Generate the random portions of an ICE username fragment and password. */
bool SfuIceGenerateCredentials(char *ufrag, char *password);

/** Prepare a new ICE generation on the owning shard. */
void SfuShardRestartIce(SfuShard *shard, const SfuCommand *command, BufferDescriptor *reply);

/**
 * Handle one datagram already classified as STUN. On SFU_ICE_REPLY the request
 * buffer holds the response and @p reply_length says how many bytes to send.
 *
 * Anything that fails parsing, FINGERPRINT, USERNAME resolution or
 * MESSAGE-INTEGRITY is dropped without a reply: silence, not an error response,
 * so the responder is neither an oracle nor an amplifier.
 */
SfuIceOutcome SfuShardHandleStun(SfuShard *shard, SfuPacket *packet, const struct sockaddr_storage *source,
                                 socklen_t source_length, size_t *reply_length);

/**
 * Run one peer through consent, restart and idle expiry.
 *
 * @return true when the slot was released and must no longer be touched.
 */
bool SfuShardPeerExpire(SfuShard *shard, SfuPeer *peer, size_t slot, uint64_t now_ms);

/** Zero the ICE half of a peer record; called when a slot is allocated. */
void SfuShardPeerResetIce(SfuPeer *peer);

/** Resolve a local ICE ufrag to its peer. NULL when unregistered. */
SfuPeer *SfuShardPeerByUfrag(SfuShard *shard, const char *ufrag, size_t ufrag_length);

/**
 * Release one peer unconditionally: its room membership, SSRCs, flow entry,
 * ufrag, DTLS and SRTP state, then the slot itself.
 */
void SfuShardPeerRelease(SfuShard *shard, SfuPeer *peer, size_t slot, SfuDisconnectReasonEnum reason);

/** Release a peer by ufrag; used by the signalling LEAVE command. */
void SfuShardLeavePeer(SfuShard *shard, const char *ufrag, size_t ufrag_length);

#endif /* UFSRVCORELIB_SFU_SHARD_ICE_H */
