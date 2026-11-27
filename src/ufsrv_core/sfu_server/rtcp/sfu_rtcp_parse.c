/**
 * @file sfu_rtcp_parse.c
 * @brief Compound walker for decrypted RTCP, with the padding and mux rules.
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

#include <arpa/inet.h>
#include <string.h>

#include "rtcp/sfu_rtcp_parse.h"

/** Big-endian readers: RTCP headers and feedback bodies are network order. */
static uint16_t
sBe16(const uint8_t *bytes)
{
  return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t
sBe32(const uint8_t *bytes)
{
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

/** The wire carries either the muxed value (64-95) or the plain one (192-223). */
static uint8_t
sRealPayloadType(uint8_t wire)
{
  return wire >= 192u ? wire : (uint8_t)(wire + 128u);
}

size_t
SfuRtcpParseCompound(const uint8_t *data, size_t length, SfuRtcpVisitor visit, void *context,
                     bool *malformed)
{
  if (malformed) *malformed = false;
  if (!data) {
    if (malformed) *malformed = true;
    return 0;
  }
  if (!length) return 0;
  if (length < 8) { /* the smallest legal packet is a headerless RR at 8 bytes */
    if (malformed) *malformed = true;
    return 0;
  }

  size_t packets = 0;
  size_t offset = 0;
  while (offset + 4u <= length) {
    const uint8_t version = (uint8_t)(data[offset] >> 6);
    const bool    padded = (data[offset] & 0x20u) != 0;
    const uint8_t rc_or_fmt = (uint8_t)(data[offset] & 0x1fu);
    const uint8_t payload_type = sRealPayloadType(data[offset + 1u]);
    const size_t  packet_bytes = ((size_t)sBe16(data + offset + 2u) + 1u) * 4u;

    /* A length that overruns the compound is the one thing an attacker fully
     * controls, so it is checked before any body field is read. */
    if (version != 2u || packet_bytes < 8u || packet_bytes > length - offset) {
      if (malformed) *malformed = true;
      return packets;
    }

    size_t packet_length = packet_bytes;
    if (padded) {
      /* Padding belongs to the last packet only; anywhere else it would shift
       * the next header without a length telling us so. */
      if (offset + packet_bytes != length) {
        if (malformed) *malformed = true;
        return packets;
      }
      const uint8_t padding = data[offset + packet_bytes - 1u];
      if (padding == 0 || padding > packet_bytes - 4u) {
        if (malformed) *malformed = true;
        return packets;
      }
      packet_length -= padding;
    }

    SfuRtcpEvent event = {.fmt = rc_or_fmt, .packet = data + offset, .packet_length = packet_length};
    /* SSRCs are opaque identifiers the engine compares against values it stored
     * with memcpy, so they are copied rather than converted; lengths and NTP
     * timestamps are numbers and stay big-endian reads. */
    if (packet_length >= 8u) {
      memcpy(&event.sender_ssrc, data + offset + 4u, sizeof(event.sender_ssrc));
      event.sender_ssrc = ntohl(event.sender_ssrc);
    }
    if (packet_length >= 12u) {
      memcpy(&event.media_ssrc, data + offset + 8u, sizeof(event.media_ssrc));
      event.media_ssrc = ntohl(event.media_ssrc);
    }

    switch (payload_type) {
    case SFU_RTCP_PT_SR:
      event.kind = SFU_RTCP_KIND_SR;
      event.media_ssrc = 0; /* for SR bytes 8-15 are the NTP timestamp */
      if (packet_length >= 16u) {
        const uint32_t seconds = sBe32(data + offset + 8u);
        const uint32_t fraction = sBe32(data + offset + 12u);
        event.ntp_mid32 = (seconds << 16) | (fraction >> 16);
      }
      break;
    case SFU_RTCP_PT_RR:
      event.kind = SFU_RTCP_KIND_RR;
      break;
    case SFU_RTCP_PT_SDES:
      event.kind = SFU_RTCP_KIND_SDES;
      break;
    case SFU_RTCP_PT_BYE:
      event.kind = SFU_RTCP_KIND_BYE;
      break;
    case SFU_RTCP_PT_RTPFB:
      if (rc_or_fmt != SFU_RTCP_FMT_NACK) {
        event.kind = SFU_RTCP_KIND_UNKNOWN;
        break;
      }
      event.kind = SFU_RTCP_KIND_NACK;
      if (packet_length >= 12u) {
        const size_t fci_bytes = packet_length - 12u;
        if (fci_bytes % 4u) { /* a partial PID/BLP cannot be expanded */
          if (malformed) *malformed = true;
          return packets;
        }
        const uint8_t *fci = data + offset + 12u;
        const size_t   entries = fci_bytes / 4u;
        if (entries > 0xFFu) {
          if (malformed) *malformed = true;
          return packets;
        }
        event.nack_entries = (uint8_t)entries;
        for (size_t entry = 0; entry < entries; entry++) {
          const uint16_t bitmask = sBe16(fci + entry * 4u + 2u);
          event.nack_requests = (uint16_t)(event.nack_requests + 1u);
          for (unsigned bit = 0; bit < 16; bit++) {
            if (bitmask & (1u << bit)) event.nack_requests++;
          }
        }
      }
      break;
    case SFU_RTCP_PT_PSFB:
      if (rc_or_fmt == SFU_RTCP_FMT_PLI) {
        event.kind = SFU_RTCP_KIND_PLI;
      } else if (rc_or_fmt == SFU_RTCP_FMT_FIR && packet_length >= 20u) {
        /* FIR's target is in the FCI, behind its own sequence number. */
        event.kind = SFU_RTCP_KIND_FIR;
        memcpy(&event.media_ssrc, data + offset + 12u, sizeof(event.media_ssrc));
        event.media_ssrc = ntohl(event.media_ssrc);
        event.fir_sequence = data[offset + 16u];
      } else {
        event.kind = SFU_RTCP_KIND_UNKNOWN;
      }
      break;
    default:
      event.kind = SFU_RTCP_KIND_UNKNOWN;
      break;
    }

    packets++;
    if (visit && !visit(context, &event)) return packets;

    offset += packet_bytes;
    if (offset >= length) break;
  }

  /* Trailing bytes too short to hold a header mean the compound lied. */
  if (offset != length) {
    if (malformed) *malformed = true;
  }
  return packets;
}
