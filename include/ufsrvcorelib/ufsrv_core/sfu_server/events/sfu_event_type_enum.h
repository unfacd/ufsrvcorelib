//
// Created by ayman on 27/11/2026.
//

/**
 * @file sfu_event_type_enum.h
 * @brief sfu_event_type_enum
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

#ifndef UFSRVCORELIB_SFU_EVENT_TYPE_ENUM_H
#define UFSRVCORELIB_SFU_EVENT_TYPE_ENUM_H

typedef enum {
  SFU_EVT_PEER_CONNECTED    = 1,
  SFU_EVT_PEER_DISCONNECTED = 2,
  SFU_EVT_DOMINANT_SPEAKER  = 3,
  SFU_EVT_PUBLICATION_ADDED        = 4,
  SFU_EVT_PUBLICATION_REMOVED      = 5,
  SFU_EVT_ICE_RESTART_SUCCEEDED    = 6,
  SFU_EVT_ICE_RESTART_FAILED       = 7,
  SFU_EVT_ROOM_IDLE                = 8,
  SFU_EVT_EVENT_DELIVERY_GAP       = 9,
} SfuEventTypeEnum;

#endif //UFSRVCORELIB_SFU_EVENT_TYPE_ENUM_H
