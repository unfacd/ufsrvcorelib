/**
 * @file sfu_rtcp_parse.h
 * @brief The RTCP compound walker: no sockets, no sessions, no shard.
 *
 * RTCP arrives as a compound of packets, each with a four-byte common header.
 * This module walks that compound, applies the padding rules and the
 * reduced-size allowance, and reports each packet to a visitor without knowing
 * what the shard will do with it. Keeping it free of engine types is what makes
 * the awkward cases - truncation, padding, feedback-first compounds, the muxed
 * payload type - testable with a byte fixture.
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

#ifndef UFSRVCORELIB_SFU_RTCP_PARSE_H
#define UFSRVCORELIB_SFU_RTCP_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Real RTCP payload types, after the RTP/RTCP mux offset is removed. */
#define SFU_RTCP_PT_SR    200u
#define SFU_RTCP_PT_RR    201u
#define SFU_RTCP_PT_SDES  202u
#define SFU_RTCP_PT_BYE   203u
#define SFU_RTCP_PT_RTPFB 205u
#define SFU_RTCP_PT_PSFB  206u

/** Feedback format identifiers that matter here (RFC 4585, RFC 5104). */
#define SFU_RTCP_FMT_NACK 1u
#define SFU_RTCP_FMT_PLI  1u
#define SFU_RTCP_FMT_FIR  4u

/** One packet of a compound, as the shard needs to see it. */
typedef enum SfuRtcpKind {
  SFU_RTCP_KIND_SR = 0,
  SFU_RTCP_KIND_RR,
  SFU_RTCP_KIND_SDES,
  SFU_RTCP_KIND_BYE,
  SFU_RTCP_KIND_NACK,
  SFU_RTCP_KIND_PLI,
  SFU_RTCP_KIND_FIR,
  SFU_RTCP_KIND_UNKNOWN
} SfuRtcpKind;

/** What the walker extracted from one packet; fields not relevant to the kind are zero. */
typedef struct SfuRtcpEvent {
  SfuRtcpKind kind;
  uint8_t     fmt;           ///< RC or FMT from the common header
  uint32_t    sender_ssrc;   ///< SSRC in bytes 4-7, copied as bytes (opaque id)
  uint32_t    media_ssrc;    ///< target SSRC (bytes 8-11); FIR uses its FCI target
  uint32_t    ntp_mid32;     ///< SR only: middle 32 bits of the NTP timestamp
  uint8_t     fir_sequence;  ///< FIR only: the sequence number in the FCI
  uint8_t     nack_entries;  ///< NACK only: FCI entries (PID + bitmask each)
  uint16_t    nack_requests; ///< NACK only: sequences named, PID plus BLP bits
  const uint8_t *packet;     ///< the packet in place, padding excluded
  size_t      packet_length; ///< its length; the relay rewrites from this view
} SfuRtcpEvent;

/** Return false to stop the walk early (the shard may not care about the rest). */
typedef bool (*SfuRtcpVisitor)(void *context, const SfuRtcpEvent *event);

/**
 * @brief Walk one decrypted RTCP compound.
 *
 * Accepts both wire forms of the payload type: the muxed 64-95 range and the
 * plain 192-223 range, because a peer that does not mux must still be refused
 * by policy rather than misparsed. A compound that starts with feedback is
 * accepted (reduced-size RTCP); the first-packet SR/RR rule is deliberately not
 * enforced here.
 *
 * @param data       Decrypted compound.
 * @param length     Bytes available.
 * @param visit      Called once per packet; may be NULL to count only.
 * @param context    Opaque pointer handed to @p visit.
 * @param malformed  Set true when a header lies about its length, the version is
 *                   not 2, or padding is misplaced. False on a clean walk.
 *
 * @return Number of packets visited, which may be short of the compound when
 *         @p visit asked to stop or the walk hit a malformed packet.
 */
size_t SfuRtcpParseCompound(const uint8_t *data, size_t length, SfuRtcpVisitor visit, void *context,
                            bool *malformed);

#endif /* UFSRVCORELIB_SFU_RTCP_PARSE_H */
