/**
 * @file sfu_shard_srtp.h
 * @brief The media plane: DTLS-exported keys, libsrtp sessions, SRTP in/out.
 *
 * Sessions are installed once, at the end of the DTLS handshake, from the RFC
 * 5764 exporter output. Incoming RTP is unprotected in place, its SSRC learned,
 * and echoed back protected with the server-write keys — the first media path
 * the engine has, and the gate for the room fan-out stage.
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

#ifndef UFSRVCORELIB_SFU_SHARD_SRTP_H
#define UFSRVCORELIB_SFU_SHARD_SRTP_H

#include <stdbool.h>
#include <stddef.h>

#include "model/packet_type.h"
#include "model/shard_type.h"

/**
 * Install both libsrtp sessions from the peer's exported keying material.
 * Called once, at the end of the DTLS handshake. False means the negotiation
 * cannot be served (unknown profile, session creation failure).
 */
bool SfuShardSrtpStart(SfuShard *shard, SfuPeer *peer);

/** Release both sessions and zero the exporter material. */
void SfuShardSrtpStop(SfuShard *shard, SfuPeer *peer);

typedef enum SfuMediaOutcome {
  SFU_MEDIA_DROPPED = 0, ///< not forwarded
  SFU_MEDIA_ECHO         ///< the packet slot holds a protected reply
} SfuMediaOutcome;

/**
 * Unprotect one RTP datagram and echo it back protected with the server-write
 * keys. @p reply_length receives the protected length on SFU_MEDIA_ECHO.
 */
SfuMediaOutcome SfuShardHandleSrtp(SfuShard *shard, SfuPeer *peer, SfuPacket *packet, size_t *reply_length);

/** Whether this outbound session already has SRTP state for a negotiated SSRC. */
bool SfuShardSrtpOutboundUsed(const SfuPeer *peer, uint32_t ssrc);

/** Replay window applied to both the RTP and the SRTCP policy. RFC 3711 minimum. */
#define SFU_SRTP_REPLAY_WINDOW 64u

/**
 * @brief Bytes SRTCP appends: the 4-byte index plus the profile's tag.
 *
 * @param srtp_profile OpenSSL-negotiated profile id from the DTLS handshake.
 * @return 14 for AES-CM-80, 20 for AEAD AES-128-GCM, 0 for an unsupported id.
 */
size_t SfuSrtcpTrailerLength(uint16_t srtp_profile);

/**
 * @brief Protect one RTCP packet in place with SRTCP.
 *
 * @param session       Peer's outbound SRTP context.
 * @param srtp_profile  Negotiated profile, which fixes the trailer length.
 * @param buffer        RTCP plaintext, overwritten by the protected packet.
 * @param capacity      Bytes available at @p buffer; must hold plaintext + trailer.
 * @param length        In: plaintext length. Out: protected length on success.
 * @return False when the profile is unknown, the capacity is short, or libsrtp
 *         refuses the packet. @p length is left untouched on failure.
 */
bool SfuSrtcpProtect(srtp_t session, uint16_t srtp_profile, uint8_t *buffer, size_t capacity, size_t *length);

#endif /* UFSRVCORELIB_SFU_SHARD_SRTP_H */
