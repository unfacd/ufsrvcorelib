//
// Created by ayman on 27/11/2026.
//

/**
 * @file sfu_disconnect_reason_enum.h
 * @brief sfu_disconnect_reason_enum
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

#ifndef UFSRVCORELIB_SFU_DISCONNECT_REASON_ENUM_H
#define UFSRVCORELIB_SFU_DISCONNECT_REASON_ENUM_H

typedef enum {
  SFU_DISC_NONE        = 0,
  SFU_DISC_ICE_TIMEOUT = 1,
  SFU_DISC_DTLS_CLOSED = 2,
  SFU_DISC_ADMIN_KICK  = 3,
} SfuDisconnectReasonEnum;

#endif //UFSRVCORELIB_SFU_DISCONNECT_REASON_ENUM_H
