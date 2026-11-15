/**
 * @file configfile_processor_priv.h
 * @brief Private internals for the configfile_processor module.
 *
 * This header is colocated with the implementation and never installed.
 * It exposes the opaque struct definition and internal helpers for
 * testing via direct inclusion from the test suite.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_PRIV_H
#define UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_PRIV_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "configfile_processor.h"
#include "config_defs_ufsrvcorelib.h"

/* ── Compile-time defaults ──────────────────────────────────────────────── */

#define PRIV_CONFIG_DEFAULT_MAX_SCOPE_NAME_SZ   64
#define PRIV_CONFIG_DEFAULT_MAX_KEY_NAME_SZ    128
#define PRIV_CONFIG_DEFAULT_ERROR_MESSAGE_SZ   256
#define PRIV_CONFIG_DEFAULT_MAX_NESTED_DEPTH     4

/* ── Opaque struct definition ───────────────────────────────────────────── */

struct ConfFileOpaque {
  lua_State *   L;                                                    ///< Lua VM — owned; lua_close()'d on destroy
  ConfFileError last_error;                                           ///< Error from most recent operation
  int           config_ref;                                           ///< LUA_REFNIL if unset; registry ref to config table */
  char          error_message[PRIV_CONFIG_DEFAULT_ERROR_MESSAGE_SZ];  ///< Human-readable detail
};

#endif /* UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_PRIV_H */
