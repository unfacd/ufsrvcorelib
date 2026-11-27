/**
 * @file outbound_type.h
 * @brief One outbound-SSRC index entry: which subscription receives this SSRC.
 *
 * The index is what turns a subscriber's RTCP `media_ssrc` into the peer and
 * subscription that own it, without scanning peers. Entries hold indices, not
 * pointers, so moving or re-laying-out the peer array cannot invalidate them.
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

#ifndef UFSRVCORELIB_OUTBOUND_TYPE_H
#define UFSRVCORELIB_OUTBOUND_TYPE_H

#include <stdint.h>

/** One outbound SSRC and the subscription that owns it. */
typedef struct SfuOutboundEntry {
  uint32_t out_ssrc;          ///< SSRC the subscriber receives
  uint32_t peer_slot_plus_one;///< subscriber slot + 1; zero marks an empty entry
  uint8_t  subscription_index;///< index into the subscriber's subscription array
  uint8_t  reserved[3];       ///< keeps the entry at 12 bytes and initialisation simple
} SfuOutboundEntry;

#endif /* UFSRVCORELIB_OUTBOUND_TYPE_H */
