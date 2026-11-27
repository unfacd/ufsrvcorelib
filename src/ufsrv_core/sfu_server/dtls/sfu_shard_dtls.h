/**
 * @file sfu_shard_dtls.h
 * @brief The shard's DTLS adapter: records in, records out, per peer.
 *
 * The shard thread owns every call. An SSL object is created on the peer's
 * first DTLS record and driven until the handshake completes; at that point the
 * peer certificate's SHA-256 is compared with the fingerprint signalling
 * supplied, the negotiated SRTP profile and the RFC 5764 exporter output are
 * recorded, and only then is the association marked ready.
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

#ifndef UFSRVCORELIB_SFU_SHARD_DTLS_H
#define UFSRVCORELIB_SFU_SHARD_DTLS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#include "model/packet_type.h"
#include "model/shard_type.h"

/**
 * Feed one DTLS datagram to the peer's handshake and flush whatever OpenSSL
 * wrote. Records are sent to @p source, the address the datagram came from,
 * which for a live peer is the consented tuple.
 *
 * @return true when the peer is ready for SRTP, false otherwise.
 */
bool SfuShardHandleDtls(SfuShard *shard, SfuPeer *peer, SfuPacket *packet, const struct sockaddr_storage *source,
                        socklen_t source_length);

/** Drive the handshake retransmission timer for one peer. */
void SfuShardDtlsTick(SfuShard *shard, SfuPeer *peer, uint64_t now_ms);

/** Release the peer's SSL object and zero its key material. Safe when empty. */
void SfuShardDtlsReset(SfuShard *shard, SfuPeer *peer);

#endif /* UFSRVCORELIB_SFU_SHARD_DTLS_H */
