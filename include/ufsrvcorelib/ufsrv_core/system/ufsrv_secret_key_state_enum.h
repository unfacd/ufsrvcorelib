/**
 * @file ufsrv_secret_key_state_enum.h
 * @brief Result states for secret-key provisioning.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_CORE_SYSTEM_UFSRV_SECRET_KEY_STATE_ENUM_H
#define UFSRV_CORE_SYSTEM_UFSRV_SECRET_KEY_STATE_ENUM_H

/*!
 * Outcome of UfsrvRuntimeProvideSecretKey().
 */
typedef enum {
  UFSRV_SECRET_KEY_STATE_OK = 0,          ///< key loaded successfully
  UFSRV_SECRET_KEY_STATE_NO_PATH,         ///< .secret_key_path is empty
  UFSRV_SECRET_KEY_STATE_FILE_NOT_FOUND,  ///< key file cannot be opened
  UFSRV_SECRET_KEY_STATE_EMPTY            ///< key file is empty
} UfsrvSecretKeyState;

#endif /* UFSRV_CORE_SYSTEM_UFSRV_SECRET_KEY_STATE_ENUM_H */
