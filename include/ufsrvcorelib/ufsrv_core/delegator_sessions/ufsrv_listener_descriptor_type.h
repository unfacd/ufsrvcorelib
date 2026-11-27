/**
 * @file ufsrv_listner_descriptor_type.h
 * @brief ufsrv_listner_descriptor_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 22/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_UFSRV_LISTENER_DESCRIPTOR_TYPE_H
#define UFSRVCORELIB_UFSRV_LISTENER_DESCRIPTOR_TYPE_H

#include <stddef.h>
#include <stdint.h>

typedef struct Protocol Protocol;

typedef struct {
  const char *name;
  const char *bind_address;
  uint16_t port;
  const Protocol *protocol; ///< Resolved by the host for runtime use.
  const char *protocol_key; ///< Configured key, owned by the config store.
} UfsrvListenerDescriptor;

#endif //UFSRVCORELIB_UFSRV_LISTENER_DESCRIPTOR_TYPE_H
