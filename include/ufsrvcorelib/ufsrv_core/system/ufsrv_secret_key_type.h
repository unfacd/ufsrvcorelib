/**
 * @file ufsrv_secret_key_type.h
 * @brief ufsrv_secret_key_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 1/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_UFSRV_SECRET_KEY_TYPE_H
#define UFSRVCORELIB_UFSRV_SECRET_KEY_TYPE_H

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

typedef struct UfsrvSecretKey UfsrvSecretKey;

#define SECRET_CREDENTIAL_ID    CONFIG_DEFAULT_UFSRV_SECRET_KEY
#define SECRET_KEY_BYTES        32  ///< binary key
#define SECRET_KEY_BYTES_SERIALISED ((SECRET_KEY_BYTES * 2) + 1) ///< includes '\0'
#define SECRET_NONCE_BYTES      12
#define SECRET_TAG_BYTES        16
#define SECRET_PREFIX           "enc:v1:"
#define SECRET_PREFIX_LEN       (sizeof(SECRET_PREFIX) - 1)

struct UfsrvSecretKey
{
  char secret_key_serialised[SECRET_KEY_BYTES_SERIALISED];
  unsigned char secret_key_raw[SECRET_KEY_BYTES];
};

#endif //UFSRVCORELIB_UFSRV_SECRET_KEY_TYPE_H
