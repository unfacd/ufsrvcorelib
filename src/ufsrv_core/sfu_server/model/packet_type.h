/**
 * @file packet_type.h
 * @brief One media datagram: a slab slot minted by its owning shard.
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

#ifndef UFSRVCORELIB_PACKET_TYPE_H
#define UFSRVCORELIB_PACKET_TYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SFU_PACKET_HEADROOM 128u  /* SRTP tag, DTLS record, ICE framing */
#define SFU_PACKET_CAPACITY 1500u

typedef struct SfuPacket {
  uint16_t home_shard;  /* slab that owns it: the pool it returns to */
  uint16_t length;
  uint16_t flags;
  uint16_t reserved;
  uint32_t link;        /* inlined slab free link */
  uint32_t guard;       /* inlined slab guard trailer, adjacent to the link */
  uint8_t  data[SFU_PACKET_HEADROOM + SFU_PACKET_CAPACITY];
} SfuPacket;

/** Return the payload offset, including any CSRCs and header extensions. Zero means invalid. */
static inline size_t
SfuRtpPayloadOffset(const uint8_t *rtp, size_t length)
{
  if (length < 12u || (rtp[0] >> 6) != 2u) return 0;
  size_t header = 12u + 4u * (rtp[0] & 0x0fu);
  if (header > length) return 0;

  if (rtp[0] & 0x10u) {
    if (length - header < 4u) return 0;
    const size_t extension_words = ((size_t)rtp[header + 2u] << 8) | rtp[header + 3u];
    header += 4u + 4u * extension_words;
    if (header > length) return 0;
  }
  return header;
}

/** Return the plaintext RTP payload size, excluding CSRCs, extensions and padding. */
static inline bool
SfuRtpPayloadSize(const uint8_t *rtp, size_t length, size_t *payload_size)
{
  const size_t header = SfuRtpPayloadOffset(rtp, length);
  if (!header) return false;

  size_t payload = length - header;
  if (rtp[0] & 0x20u) {
    const uint8_t padding = rtp[length - 1u];
    if (!padding || padding > payload) return false;
    payload -= padding;
  }
  *payload_size = payload;
  return true;
}

#endif /* UFSRVCORELIB_PACKET_TYPE_H */
