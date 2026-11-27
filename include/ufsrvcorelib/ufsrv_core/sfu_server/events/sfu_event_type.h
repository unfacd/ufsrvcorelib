//
// Created by ayman on 27/11/2026.
//

/**
 * @file sfu_event_type.h
 * @brief sfu_event_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 27/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_SFU_EVENT_TYPE_H
#define UFSRVCORELIB_SFU_EVENT_TYPE_H

#include <stdint.h>

#include "sfu_event_type_enum.h"

#define SFU_MAX_ID_LEN 64

/** Wire detail values for publication events. */
typedef enum {
  SFU_EVT_MEDIA_AUDIO = 1,
  SFU_EVT_MEDIA_VIDEO = 2,
} SfuEventMediaKind;

/** Wire detail values for an unsuccessful ICE restart. */
typedef enum {
  SFU_EVT_ICE_RESTART_TIMEOUT = 1,
  SFU_EVT_ICE_RESTART_TRANSPORT_FAILED = 2,
} SfuEventIceRestartFailure;

/** Wire detail values for delivery-gap control events. */
typedef enum {
  SFU_EVT_GAP_QUEUE_FULL = 1,
  SFU_EVT_GAP_ADAPTER_LOSS = 2,
  SFU_EVT_GAP_SHUTDOWN_ABANDONED = 3,
} SfuEventGapScope;

/**
 * in-memory struct: naturally aligned for fast, safe access inside  lock-free ring buffer and JSON encoder without unaligned-memory penalties.
 */
typedef struct {
  uint32_t seq;
  uint64_t timestamp_ms;
  SfuEventTypeEnum  type;
  uint8_t  detail;        ///< Type-specific wire detail.
  uint8_t  room_id_len;
  uint8_t  peer_id_len;
  char     room_id[SFU_MAX_ID_LEN];
  char     peer_id[SFU_MAX_ID_LEN];
  union {
    struct {
      uint8_t id_len;
      char id[SFU_MAX_ID_LEN];
    } publication;
    uint64_t ice_generation; ///< Nonzero, scoped to the peer association.
    uint64_t dropped_count;  ///< Nonzero loss delta in the selected scope.
  } data;
} SfuEvent;

#endif //UFSRVCORELIB_SFU_EVENT_TYPE_H
