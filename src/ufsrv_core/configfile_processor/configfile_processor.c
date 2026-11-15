/**
 * @file configfile_processor.c
 * @brief Implementation of the namespaced Lua config-file processor.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <standard_defs.h>

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>
#include "configfile_processor_priv.h"

/* Lua type tags for sLookupValue  */

#define LUA_TAG_STRING  0
#define LUA_TAG_INTEGER 1
#define LUA_TAG_NUMBER  2
#define LUA_TAG_BOOL    3
#define LUA_TAG_TABLE   4
#define LUA_TAG_ARRAY   5

/*  Internal helpers */

static void
sSetError(ConfFile cp, ConfFileError code, const char *fmt, ...)
{
  if (!cp) return;

  cp->last_error = code;
  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cp->error_message, sizeof(cp->error_message), fmt, ap);
    va_end(ap);
  }
  else {
    cp->error_message[0] = '\0';
  }
}

static int
sPushScope(ConfFile cp, const char *scope_name)
{
  if (!cp || !cp->L) return 0;
  if (!scope_name || !*scope_name) return 0;

  lua_State *L = cp->L;
  lua_getglobal(L, scope_name);

  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  return 1;
}

/**
 * Push the value at scope.key onto the Lua stack.
 *
 * Supports dot-path keys for one level of nesting
 * (e.g. "database.host" → scope.database.host).
 *
 * On success the value is at stack top; on failure the stack is clean.
 * Returns the Lua type tag, or -1 on error (last_error is set).
 */
static int
sLookupValue(ConfFile cp, const char *scope_name, const char *key)
{
  lua_State *L = cp->L;

  /* Push the scope table */
  if (!sPushScope(cp, scope_name)) {
    sSetError(cp, CONF_FILE_ERR_SCOPE_NOT_FOUND, "scope '%s' not found or not a table", scope_name);
    return -1;
  }

  /* Check for dot-path */
  const char *dot = strchr(key, '.');
  if (dot) {
    /* Extract the sub-table name (everything before the dot) */
    size_t subtable_len = (size_t)(dot - key);
    char   subtable[PRIV_CONFIG_DEFAULT_MAX_KEY_NAME_SZ];
    if (subtable_len >= sizeof(subtable)) {
      lua_pop(L, 1);
      sSetError(cp, CONF_FILE_ERR_ARGUMENT, "sub-table name too long in key '%s'", key);
      return -1;
    }
    memcpy(subtable, key, subtable_len);
    subtable[subtable_len] = '\0';

    /* Push the sub-table */
    lua_getfield(L, -1, subtable);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 2); /* value + scope */
      sSetError(cp, CONF_FILE_ERR_KEY_NOT_FOUND, "scope '%s': sub-table '%s' not found or not a table", scope_name,
                subtable);
      return -1;
    }

    /* Replace scope with sub-table on the stack */
    lua_remove(L, -2); /* remove scope, keep sub-table */

    /* Push the inner key on the sub-table */
    lua_getfield(L, -1, dot + 1);
  }
  else {
    /* Simple key */
    lua_getfield(L, -1, key);
  }

  /* Check what we got */
  if (lua_isnil(L, -1)) {
    /* Pop value + the table we pushed (scope or sub-table) */
    lua_pop(L, 2);
    sSetError(cp, CONF_FILE_ERR_KEY_NOT_FOUND, "scope '%s': key '%s' not found", scope_name, key);
    return -1;
  }

  /* Return the Lua type tag */
  int tag = lua_type(L, -1);
  /* Keep the table and value on the stack — caller pops both */
  return tag;
}

/*
 * ConfFileInit — load a Lua config file and capture allowlisted scopes.
 *
 * Supports both styles:
 *   Style A:  return { ufsrv = {...}, ufnet = {...}, ... }
 *   Style B:  ufsrv = {...}; ufnet = {...};  (global assignments)
 *
 * Only keys in s_config_scopes[] are stored in the registry config table
 * and promoted to _G.  Standard libraries and unknown globals are ignored.
 *
 * Caller must check: cp != NULL && cp->last_error == CONF_FILE_OK
 * struct ConfFileOpaque must include: int config_ref;  (LUA_REFNIL if unset)
 */

/* Product config scopes — extend when a new tier/adjunct is introduced. */
static const char *const ufsrv_config_scopes[] = {
    "ufsrv",
    "ufnet",
    "ufsrvwebsock",
    "ufsrvapi",
    "ufsrvmcp",
    "ufsrvauth",
    NULL
};

static bool
sIsConfigScope(const char *name)
{
    if (!name) return false;
    for (const char *const *p = ufsrv_config_scopes; *p != NULL; p++) {
        if (strcmp(name, *p) == 0) return true;
    }

    return false;
}

/* Copy allowlisted string-keyed tables from `src` into `dst` and into _G. src and dst are absolute stack indices.
* After a global-assignment config runs:
* * @code{.lua}
* ufsrv = { ... }
* ufnet = { ... }
* @endcode
*
_G now holds both:

1. Config scopes you care about (ufsrv, ufnet, ufsrvwebsock, …)
2. Everything luaL_openlibs installed (string, table, io, package, …)

Since we harvest “every string-keyed table in _G”, without a filter, the config table would also contain:
cfg.string, cfg.table, cfg.io, cfg.os, cfg.math, cfg.package, ...

which is undesirable for schema validation (extra top-level “scopes”) and for any logic that assumes config keys are only your domains.
so, we wmain maintain an "allowlist of scopes" instead.


 */
static void
sHarvestScopes(lua_State *L, int src, int dst)
{
#if LUA_VERSION_NUM >= 502
    lua_pushglobaltable(L);
    int g = lua_gettop(L);
#else
    int g = LUA_GLOBALSINDEX;
#endif

    lua_pushnil(L);
    while (lua_next(L, src)) {
        /* stack: …, key, value */
        if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
            const char *k = lua_tostring(L, -2);
            if (sIsConfigScope(k)) {
                /* dst[key] = value */
                lua_pushvalue(L, -2);
                lua_pushvalue(L, -2);
                lua_settable(L, dst);

                /* _G[key] = value  (for sLookupValue / TransferToConfigValues) */
                lua_pushvalue(L, -2);
                lua_pushvalue(L, -2);
                lua_settable(L, g);
            }
        }
        lua_pop(L, 1); /* pop value, keep key for lua_next */
    }

#if LUA_VERSION_NUM >= 502
    lua_pop(L, 1); /* _G */
#endif
}

PUBLIC_API ConfFile
ConfFileInit(const char *config_file)
{
    if (!config_file) return NULL;

    ConfFile cp = calloc(1, sizeof(struct ConfFileOpaque));
    if (!cp) return NULL;

    cp->config_ref = LUA_REFNIL;
    cp->L = luaL_newstate();
    if (!cp->L) {
        sSetError(cp, CONF_FILE_LUA_ERR_LUA_STATE, "Failed to initialise lua state.");
        return cp;
    }
    luaL_openlibs(cp->L);
    lua_State *L = cp->L;

    /* Load and run config; capture one return value. */
    if (luaL_loadfile(L, config_file) || lua_pcall(L, 0, 1, 0)) {
        const char *err = lua_tostring(L, -1);
        sSetError(cp, CONF_FILE_ERR_LUA, "failed to load/execute '%s': %s", config_file, err ? err : "unknown error");
        return cp;
    }
    /* Stack: [result_or_nil] */

    lua_createtable(L, 0, 8);
    int cfg = lua_gettop(L);
    /* Stack: [result_or_nil, cfg] */

    if (lua_istable(L, 1)) {
        /*
         * Style A — return { ufsrv = {...}, ... }
         * Harvest only allowlisted keys from the returned table.
         */
        sHarvestScopes(L, 1, cfg);
    } else {
        /*
         * Style B — global assignments (ufsrv = {...}; ...)
         * Harvest only allowlisted keys from _G.
         */
#if LUA_VERSION_NUM >= 502
        lua_pushglobaltable(L);
#else
        lua_pushvalue(L, LUA_GLOBALSINDEX);
#endif
        int g = lua_gettop(L);
        /* Copy allowlisted tables into cfg; they are already in _G. */
        lua_pushnil(L);
        while (lua_next(L, g)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
                const char *k = lua_tostring(L, -2);
                if (sIsConfigScope(k)) {
                    lua_pushvalue(L, -2);
                    lua_pushvalue(L, -2);
                    lua_settable(L, cfg);
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1); /* _G */
    }

    /* Anchor cfg in the registry; clear stack. */
    lua_pushvalue(L, cfg);
    cp->config_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_settop(L, 0);

    /* Require at least one allowlisted scope. */
    lua_rawgeti(L, LUA_REGISTRYINDEX, cp->config_ref);
    int empty = 1;
    lua_pushnil(L);
    if (lua_next(L, -2)) {
        empty = 0;
        lua_pop(L, 2); /* key, value */
    }
    lua_settop(L, 0);

    if (empty) {
        sSetError(cp, CONF_FILE_ERR_LUA, "config '%s' produced no recognised scopes " "(expected one of: ufsrv, ufnet, ufsrvwebsock, …)", config_file);
        return cp;
    }

    cp->last_error = CONF_FILE_OK;
    return cp;
}

PUBLIC_API void
ConfFileDestroy(ConfFile cp)
{
  if (!cp) return;
  if (cp->L) {
    if (cp->config_ref != LUA_REFNIL) {
      luaL_unref(cp->L, LUA_REGISTRYINDEX, cp->config_ref);
      cp->config_ref = LUA_REFNIL;
    }

    lua_close(cp->L);
  }
  free(cp);
}

// Error handling

PUBLIC_API ConfFileError
ConfFileGetLastError(ConfFile cp)
{
  if (!cp) return CONF_FILE_ERR_ARGUMENT;
  return cp->last_error;
}

PUBLIC_API const char *
ConfFileGetLastErrorMessage(ConfFile cp)
{
  if (!cp) return "handle is NULL";
  return cp->error_message;
}

PUBLIC_API const char *
ConfFileErrorToString(ConfFileError err)
{
  switch (err) {
  case CONF_FILE_OK: return "OK";
  case CONF_FILE_ERR_LUA: return "Lua runtime error";
  case CONF_FILE_ERR_SCOPE_NOT_FOUND: return "scope not found";
  case CONF_FILE_ERR_KEY_NOT_FOUND: return "key not found";
  case CONF_FILE_ERR_TYPE_MISMATCH: return "type mismatch";
  case CONF_FILE_ERR_ARGUMENT: return "invalid argument";
  case CONF_FILE_ERR_MEMORY: return "memory allocation failure";
  case CONF_FILE_ERR_NOT_INITIALISED: return "not initialised";
  case CONF_FILE_ERR_INDEX_OUT_OF_RANGE: return "index out of range";
  case CONF_FILE_LUA_ERR_LUA_STATE: return "lua state initialisation error";
  default: return "unknown error";
  }
}

// Scalar accessors

PUBLIC_API const char *
ConfFileGetString(ConfFile cp, const char *scope, const char *key)
{
  if (!cp || !cp->L || !scope || !key) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return NULL;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) return NULL; /* error already set */

  if (tag != LUA_TSTRING) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a string", scope, key);
    return NULL;
  }

  const char *val = lua_tostring(cp->L, -1);
  lua_pop(cp->L, 2);
  cp->last_error = CONF_FILE_OK;
  return val;
}

PUBLIC_API const char *
ConfFileGetStringDefault(ConfFile cp, const char *scope, const char *key, const char *default_value)
{
  if (!cp || !cp->L || !scope || !key) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return default_value;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) {
    /* Key not found or scope not found — use default, don't set error */
    cp->last_error = CONF_FILE_OK;
    return default_value;
  }

  if (tag != LUA_TSTRING) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a string", scope, key);
    return default_value;
  }

  const char *val = lua_tostring(cp->L, -1);
  lua_pop(cp->L, 2);
  cp->last_error = CONF_FILE_OK;
  return val;
}

PUBLIC_API int
ConfFileGetStringCopy(ConfFile cp, const char *scope, const char *key, char *buf, size_t buf_size)
{
  if (!cp || !cp->L || !scope || !key || !buf || buf_size == 0) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument or zero buffer");
    return -1;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) return -1;

  if (tag != LUA_TSTRING) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a string", scope, key);
    return -1;
  }

  size_t      len;
  const char *val     = lua_tolstring(cp->L, -1, &len);
  size_t      to_copy = len < buf_size - 1 ? len : buf_size - 1;
  memcpy(buf, val, to_copy);
  buf[to_copy] = '\0';

  lua_pop(cp->L, 2);
  cp->last_error = CONF_FILE_OK;
  return (int)to_copy;
}

PUBLIC_API int
ConfFileGetInt(ConfFile cp, const char *scope, const char *key)
{
  if (!cp || !cp->L || !scope || !key) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return 0;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) return 0;

  if (tag != LUA_TNUMBER) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a number", scope, key);
    return 0;
  }

  /* Check integrality */
  int        isnum = 0;
  lua_Number n     = lua_tonumberx(cp->L, -1, &isnum);
  if (!isnum || n != (lua_Integer)n) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not an integer", scope, key);
    return 0;
  }

  int val = (int)lua_tointeger(cp->L, -1);
  lua_pop(cp->L, 2);
  cp->last_error = CONF_FILE_OK;
  return val;
}

PUBLIC_API int
ConfFileGetIntDefault(ConfFile cp, const char *scope, const char *key, int default_value)
{
  if (!cp || !cp->L || !scope || !key) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return default_value;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) {
    cp->last_error = CONF_FILE_OK;
    return default_value;
  }

  if (tag != LUA_TNUMBER) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a number", scope, key);
    return default_value;
  }

  int        isnum = 0;
  lua_Number n     = lua_tonumberx(cp->L, -1, &isnum);
  if (!isnum || n != (lua_Integer)n) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not an integer", scope, key);
    return default_value;
  }

  int val = (int)lua_tointeger(cp->L, -1);
  lua_pop(cp->L, 2);
  cp->last_error = CONF_FILE_OK;
  return val;
}

PUBLIC_API double
ConfFileGetNumber(ConfFile cp, const char *scope, const char *key)
{
  if (!cp || !cp->L || !scope || !key) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return 0.0;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) return 0.0;

  if (tag != LUA_TNUMBER) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a number", scope, key);
    return 0.0;
  }

  int    isnum = 0;
  double val   = (double)lua_tonumberx(cp->L, -1, &isnum);
  lua_pop(cp->L, 2);
  cp->last_error = CONF_FILE_OK;
  (void)isnum; /* tag == LUA_TNUMBER guarantees isnum */
  return val;
}

PUBLIC_API bool
ConfFileGetBool(ConfFile cp, const char *scope, const char *key)
{
  if (!cp || !cp->L || !scope || !key) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return false;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) return false;

  if (tag != LUA_TBOOLEAN) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a boolean", scope, key);
    return false;
  }

  bool val = lua_toboolean(cp->L, -1);
  lua_pop(cp->L, 2);
  cp->last_error = CONF_FILE_OK;
  return val;
}

/* Nested table access  */

PUBLIC_API bool
ConfFileIsTable(ConfFile cp, const char *scope, const char *key)
{
  if (!cp || !cp->L || !scope || !key) return false;

  ConfFileError saved = cp->last_error;
  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) {
    cp->last_error = saved;
    return false;
  }

  bool is_tbl = (tag == LUA_TTABLE);
  lua_pop(cp->L, 2);
  cp->last_error = saved;
  return is_tbl;
}

/* Array access  */

PUBLIC_API int
ConfFileGetArrayLength(ConfFile cp, const char *scope, const char *key)
{
  if (!cp || !cp->L || !scope || !key) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return -1;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) return -1;

  if (tag != LUA_TTABLE) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a table/array", scope, key);
    return -1;
  }

  int len = (int)lua_rawlen(cp->L, -1);
  lua_pop(cp->L, 2);
  cp->last_error = CONF_FILE_OK;
  return len;
}

PUBLIC_API const char *
ConfFileGetArrayElement(ConfFile cp, const char *scope, const char *key, int index)
{
  if (!cp || !cp->L || !scope || !key) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return NULL;
  }
  if (index < 1) {
    sSetError(cp, CONF_FILE_ERR_INDEX_OUT_OF_RANGE, "array index %d out of range (1-based)", index);
    return NULL;
  }

  lua_settop(cp->L, 0);

  int tag = sLookupValue(cp, scope, key);
  if (tag < 0) return NULL;

  if (tag != LUA_TTABLE) {
    lua_pop(cp->L, 2);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s' is not a table", scope, key);
    return NULL;
  }

  lua_pushinteger(cp->L, index);
  lua_gettable(cp->L, -2);

  if (lua_isnil(cp->L, -1)) {
    lua_pop(cp->L, 3); /* nil, array table, scope/sub-table */
    sSetError(cp, CONF_FILE_ERR_INDEX_OUT_OF_RANGE, "scope '%s': key '%s': index %d out of range", scope, key, index);
    return NULL;
  }

  if (!lua_isstring(cp->L, -1)) {
    lua_pop(cp->L, 3);
    sSetError(cp, CONF_FILE_ERR_TYPE_MISMATCH, "scope '%s': key '%s': element %d is not a string", scope, key, index);
    return NULL;
  }

  const char *val = lua_tostring(cp->L, -1);
  lua_pop(cp->L, 3);
  cp->last_error = CONF_FILE_OK;
  return val;
}

/*  Scope / key discovery  */

PUBLIC_API bool
ConfFileHasScope(ConfFile cp, const char *scope)
{
  if (!cp || !cp->L || !scope) return false;

  ConfFileError saved = cp->last_error;
  lua_settop(cp->L, 0);

  lua_getglobal(cp->L, scope);
  bool exists = lua_istable(cp->L, -1);
  lua_pop(cp->L, 1);
  cp->last_error = saved;
  return exists;
}

PUBLIC_API const char *
ConfFileNextScope(ConfFile cp, int *prev_index)
{
  if (!cp || !cp->L || !prev_index) return NULL;

  lua_State *L = cp->L;

  if (*prev_index < 0) {
    /* First call: push nil, push global table */
    lua_settop(L, 0);
#if LUA_VERSION_NUM >= 502
    lua_pushglobaltable(L);
#else
    lua_pushvalue(L, LUA_GLOBALSINDEX);
#endif
    lua_pushnil(L);
    *prev_index = 0;
  }

  /* The stack has: [global_table, nil_or_prev_key] */
  int table_idx = lua_gettop(L) - 1;

  /* lua_next pops the key and pushes (next_key, next_value) */
  while (lua_next(L, table_idx) != 0) {
    /* Got (key, value) — check if value is a scope table */
    if (lua_istable(L, -1)) {
      const char *scope_name = lua_tostring(L, -2);
      /* Pop value, leave key for next iteration */
      lua_pop(L, 1);
      return scope_name;
    }
    /* Not a table — pop value, try next */
    lua_pop(L, 1);
  }

  /* No more keys — clean up global table + nil */
  lua_settop(L, 0);
  return NULL;
}

PUBLIC_API const char *
ConfFileNextKey(ConfFile cp, const char *scope, int *prev_index)
{
  if (!cp || !cp->L || !scope || !prev_index) return NULL;

  lua_State *L = cp->L;

  if (*prev_index < 0) {
    /* First call: push scope table + nil key */
    lua_settop(L, 0);
    if (!sPushScope(cp, scope)) {
      sSetError(cp, CONF_FILE_ERR_SCOPE_NOT_FOUND, "scope '%s' not found", scope);
      return NULL;
    }
    lua_pushnil(L);
    *prev_index = 0;
  }

  /* Stack: [scope_table, nil_or_prev_key] */
  int table_idx = lua_gettop(L) - 1;

  if (lua_next(L, table_idx) == 0) {
    /* No more keys — clean up */
    lua_settop(L, 0);
    return NULL;
  }

  /* Got (key, value) — return key, pop value, leave key for next call */
  const char *key_name = lua_tostring(L, -2);
  lua_pop(L, 1); /* pop value only */

  return key_name;
}

// #include "ufsrv_core/ufsrv_config/generated/data_ufsrv_config_literal_to_field_map_priv_generated.h"
#include "ufsrv_core/configfile_processor/ufsrv_config_keymap_entry_type.h"

/*  Symbol-table resolution  */

static int
sKeyMapComparator(const void *lua_key, const void *entry)
{
  return strcmp((const char*)lua_key, ((const UfsrvConfigKeyMapEntry*)entry)->config_literal);
}

/**
 *  Given an associative map of {string litreal -> index number} couplets: return index number associated with string literal.
 *  This is specifically written to work with \p UfsrvConfigKeyMapEntry
 * @param key literal key to resolve
 * @param key_map User generated associative map, containing couplets of {string, int} values
 * @param key_map_sz total size of map (count of entries)
 * @return Resolved index number. IMPORTANT: user must check resolved index is less than UFSRV_CONFIG_FIELD_COUNT, otherwise
 * that would indicate unresolved string literal.
 */
static enum UfsrvConfigField
sResolveConfigFieldAssociatedIndex(const char *key, const void *key_map, size_t key_map_sz)
{
  /* bsearch()'s base argument is declared nonnull — a NULL map (the documented
     "no key-map available" case) or zero size must short-circuit here rather
     than reaching bsearch(NULL, ...). */
  if (!key_map || key_map_sz == 0) {
    return UFSRV_CONFIG_FIELD_COUNT;
  }
  UfsrvConfigKeyMapEntry *hit = bsearch(key, key_map, key_map_sz, sizeof(UfsrvConfigKeyMapEntry), sKeyMapComparator);
  return hit ? hit->field_index : UFSRV_CONFIG_FIELD_COUNT;
}

PUBLIC_API int
ConfFileTransferToConfigValues(ConfFile cp, const ConfFileSymbolTableEntry config_file_symbols_map[], const void *associative_lookup_map, size_t associative_lookup_map_sz, void (*on_error)(const ConfFileSymbolTableEntry *entry, const char *message))
{
  if (!cp || !cp->L || !config_file_symbols_map) {
    sSetError(cp, CONF_FILE_ERR_ARGUMENT, "NULL argument");
    return 0;
  }

  int failures = 0;

  for (const ConfFileSymbolTableEntry *e = config_file_symbols_map; e->scope != NULL; e++) {
    ConfFileSymbolTableEntry *e_mutable = (ConfFileSymbolTableEntry*)e;

    // link up with ufsrv config domain using the provided associative lookup map
    e_mutable->field_index = sResolveConfigFieldAssociatedIndex(e->key, associative_lookup_map, associative_lookup_map_sz);

    // Extract raw value from Lua config
    lua_settop(cp->L, 0);

    int tag = sLookupValue(cp, e->scope, e->key);

    if (tag < 0) {
      char msg[256] = {0};
      /* Not in config file */
      if (e->is_required) {
        if (on_error) {
          snprintf(msg, sizeof(msg), "scope '%s': REQUIRED key '%s': not found in config file", e->scope, e->key);
          on_error(e, msg);
        }
        failures++;
        continue;
      }
      snprintf(msg, sizeof(msg), "scope '%s': OPTIONAL key '%s': not found in config file", e->scope, e->key);
      continue;
    }

    int expected = -1;
    switch (e->type) {
    case UFSRV_CONFIG_VALUE_STR: expected = LUA_TSTRING;
      break;
    case UFSRV_CONFIG_VALUE_INT: /* fallthrough */
    case UFSRV_CONFIG_VALUE_DOUBLE: expected = LUA_TNUMBER;
      break;
    case UFSRV_CONFIG_VALUE_BOOL: expected = LUA_TBOOLEAN;
      break;
    case UFSRV_CONFIG_VALUE_ARRAY: expected = LUA_TTABLE;
      break;
    default:
      if (on_error) {
        char msg[256];
        snprintf(msg, sizeof(msg), "scope '%s': key '%s': unsupported type %d", e->scope, e->key, (int)e->type);
        on_error(e, msg);
      }
      failures++;
      lua_pop(cp->L, 2);
      continue;
    }

    if (tag != expected) {
      if (on_error) {
        char msg[256];
        snprintf(msg, sizeof(msg), "scope '%s': key '%s': type mismatch (expected %d, got %d)", e->scope, e->key, e->type, tag);
        on_error(e, msg);
      }
      failures++;
      lua_pop(cp->L, 2);
      continue;
    }

    /* Extract value into e->value (populated directly, no intermediate union) */
    switch (e->type) {
    case UFSRV_CONFIG_VALUE_STR: {
      size_t      len;
      const char *s                         = lua_tolstring(cp->L, -1, &len);
      e_mutable->value.stored_value.str_val = strdup(s);
      e_mutable->value.type_allocation_sz   = len + 1;
      e_mutable->value.source               = UFSRV_CONFIG_SOURCE_CONFIG_FILE;
      break;
    }
    case UFSRV_CONFIG_VALUE_INT: {
      e_mutable->value.stored_value.int_val = (int64_t)lua_tointeger(cp->L, -1);
      e_mutable->value.type_allocation_sz   = 0;
      e_mutable->value.source               = UFSRV_CONFIG_SOURCE_CONFIG_FILE;
      break;
    }
    case UFSRV_CONFIG_VALUE_DOUBLE: {
      int        isnum = 0;
      lua_Number n     = lua_tonumberx(cp->L, -1, &isnum);
      if (isnum) {
        e_mutable->value.stored_value.double_value = (double)n;
        e_mutable->value.type_allocation_sz        = 0;
        e_mutable->value.source                    = UFSRV_CONFIG_SOURCE_CONFIG_FILE;
      }
      else {
        if (on_error) {
          char msg[256];
          snprintf(msg, sizeof(msg), "scope '%s': key '%s': failed to extract double", e->scope, e->key);
          on_error(e, msg);
        }
        failures++;
        lua_pop(cp->L, 2);
        continue;
      }
      break;
    }
    case UFSRV_CONFIG_VALUE_BOOL: {
      e_mutable->value.stored_value.int_val = lua_toboolean(cp->L, -1) ? 1 : 0;
      e_mutable->value.type_allocation_sz   = 0;
      e_mutable->value.source               = UFSRV_CONFIG_SOURCE_CONFIG_FILE;
      break;
    }
    case UFSRV_CONFIG_VALUE_ARRAY: {
      int arr_len = (int)lua_rawlen(cp->L, -1);
      if (arr_len > 0) {
        //  First pass: compute total size and count the string elements that
        //  will actually be marshalled.  collection_sz must reflect the number
        //  of NUL-terminated entries written into the blob — not lua_rawlen(),
        //  which also counts non-string elements and would over-count the
        //  consumer-side walk past the buffer.
        size_t total_sz  = 0;
        size_t str_count = 0;
        int    i;
        for (i = 1; i <= arr_len; i++) {
          lua_pushinteger(cp->L, i);
          lua_gettable(cp->L, -2);
          if (lua_isstring(cp->L, -1)) {
            size_t elen;
            lua_tolstring(cp->L, -1, &elen);
            total_sz += elen + 1; //string + NUL
            str_count++;
          }
          lua_pop(cp->L, 1);
        }
        if (total_sz > 0) {
          char *blob = malloc(total_sz);
          if (blob) {
            // Second pass: copy strings with NUL delimiters
            char *dst = blob;
            for (i = 1; i <= arr_len; i++) {
              lua_pushinteger(cp->L, i);
              lua_gettable(cp->L, -2);
              if (lua_isstring(cp->L, -1)) {
                size_t      elen;
                const char *es = lua_tolstring(cp->L, -1, &elen);
                memcpy(dst, es, elen);
                dst[elen] = '\0';
                dst       += elen + 1;
              }
              lua_pop(cp->L, 1);
            }
            e_mutable->value.stored_value.blob_val = blob;
            e_mutable->value.type_allocation_sz    = total_sz;
            e_mutable->value.collection_sz         = str_count;
            e_mutable->value.source                = UFSRV_CONFIG_SOURCE_CONFIG_FILE;
          }
          else {
            if (on_error) {
              char msg[256];
              snprintf(msg, sizeof(msg), "scope '%s': key '%s': malloc failed for array blob", e->scope, e->key);
              on_error(e, msg);
            }
            failures++;
            lua_pop(cp->L, 2);
            continue;
          }
        }
      }
      break;
    }
    default:
      break;
    }

    lua_pop(cp->L, 2);

    /* Run lifecycle.on_validate callback */
    if (e->lifecycle.on_validate) {
      char errbuf[256] = {0};
      if (!e->lifecycle.on_validate(e, &e_mutable->value, errbuf, sizeof(errbuf))) {
        if (on_error) on_error(e, errbuf[0] ? errbuf : "validation failed");
        failures++;
      }
    }
  }

  cp->last_error = CONF_FILE_OK;
  return failures;
}

PUBLIC_API ConfFile
ConfFileValidate(const ConfigFileValidatorDescriptor *config_file_validator_descriptor)
{
    if (!config_file_validator_descriptor->on_error) return NULL;

    /* ---- Phase 1: load config ---- */
    ConfFile cp = ConfFileInit(config_file_validator_descriptor->config_file);
    if (!cp || cp->last_error != CONF_FILE_OK) {
        config_file_validator_descriptor->on_error("", "", "error", cp->error_message);
        if (cp) ConfFileDestroy(cp);
        return NULL;
    }

    if (cp->config_ref == LUA_REFNIL) {
        config_file_validator_descriptor->on_error("", "", "error", "internal: config table not stored");
        ConfFileDestroy(cp);
        return NULL;
    }

    /* ---- Phase 2: Lua schema validation ---- */
    {
        lua_State *L = cp->L;

        if (!*config_file_validator_descriptor->validator_file) {
            config_file_validator_descriptor->on_error("", "", "error", "validator file not provided");
            ConfFileDestroy(cp);
            return NULL;
        }

        if (luaL_dofile(L, config_file_validator_descriptor->validator_file) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            config_file_validator_descriptor->on_error("", "", "error", err ? err : "failed to load validator");
            ConfFileDestroy(cp);
            return NULL;
        }

        /* resolve validate_config (module table or global) */
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "validate_config");
            lua_remove(L, -2);         /* drop module table */
        } else {
            lua_pop(L, 1);
            lua_getglobal(L, "validate_config");
        }

        if (!lua_isfunction(L, -1)) {
            config_file_validator_descriptor->on_error("", "", "error", "validate_config function not found in validator");
            ConfFileDestroy(cp);
            return NULL;
        }

        /* Stack: [validate_config] */

        /* Arg 1: config table from registry (do not scrape _G) */
        lua_rawgeti(L, LUA_REGISTRYINDEX, cp->config_ref);

        /* Arg 2: core schema path */
        lua_pushstring(L, *config_file_validator_descriptor->core_schema_file ? config_file_validator_descriptor->core_schema_file : "");

        /* Arg 3: adjunct schema path */
        lua_pushstring(L, *config_file_validator_descriptor->adjunct_schema_file ? config_file_validator_descriptor->adjunct_schema_file : "");

        /* Arg 4: opts = { strict = false | true } */
        lua_newtable(L);
        lua_pushboolean(L, config_file_validator_descriptor->validator_args.is_strict ? 1 : 0);
        lua_setfield(L, -2, "strict");

        // validate_config(config, core, adjunct, opts)
        if (lua_pcall(L, 4, 1, 0) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            config_file_validator_descriptor->on_error("", "", "error", err ? err : "schema validation failed");
            ConfFileDestroy(cp);
            return NULL;
        }

        // Success — discard validated result table; symbols still come from the original config via _G / sLookupValue.
        lua_settop(L, 0);
    }

    return cp;
}

// Adjunct configuration extension

static void * s_adjunct_config    = NULL;
__unused static size_t s_adjunct_config_sz = 0;

PUBLIC_API void
ConfFileSetAdjunct(void *adjunct_ptr, size_t adjunct_sz)
{
  s_adjunct_config    = adjunct_ptr;
  s_adjunct_config_sz = adjunct_sz;
}

PUBLIC_API void *
ConfFileAdjunct(void)
{
  return s_adjunct_config;
}

PUBLIC_API const char *
ConfigFileCoreSchema()
{
  return CONFIG_DEFAULT_CONFIGFILE_CORE_SCHEMA;
}

PUBLIC_API const char *
ConfigFileAdjunctSchema()
{
  return CONFIG_DEFAULT_CONFIGFILE_ADJUNCT_SCHEMA;
}

PUBLIC_API const char *
ConfigFileSchemaValidator()
{
  return CONFIG_DEFAULT_CONFIGFILE_VALIDATOR;
}

/* ---- helpers ---------------------------------------------------------- */

__unused static const char *
sLookupConfigLiteral(const struct UfsrvConfigKeyMapEntry *map, size_t map_sz, int field_index)
{
    if (!map)
        return NULL;
    for (size_t i = 0; i < map_sz; i++) {
        if (map[i].field_index == field_index && map[i].config_literal)
            return map[i].config_literal;
    }
    return NULL;
}

static void
sJsonEscape(FILE *fp, const char *s)
{
    if (!s) {
        fputs("null", fp);
        return;
    }
    fputc('"', fp);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", fp); break;
        case '\\': fputs("\\\\", fp); break;
        case '\b': fputs("\\b", fp);  break;
        case '\f': fputs("\\f", fp);  break;
        case '\n': fputs("\\n", fp);  break;
        case '\r': fputs("\\r", fp);  break;
        case '\t': fputs("\\t", fp);  break;
        default:
            if (*p < 0x20)
                fprintf(fp, "\\u%04x", *p);
            else
                fputc(*p, fp);
            break;
        }
    }
    fputc('"', fp);
}

static void
sDumpJsonArray(FILE *fp, const UfsrvConfigValue *value)
{
    if (value->stored_value.blob_val && value->collection_sz > 0) {
        fputc('[', fp);
        const char *p = (const char *)value->stored_value.blob_val;
        for (size_t i = 0; i < value->collection_sz; i++) {
            if (i > 0) fputs(", ", fp);
            sJsonEscape(fp, p);
            p += strlen(p) + 1;
        }
        fputc(']', fp);
    } else {
        fputs("null", fp);
    }
}

static void
sDumpJsonValue(FILE *fp, const ConfFileSymbolTableEntry *e)
{
    const UfsrvConfigValue *v = &e->value;

    switch (e->type) {
    case UFSRV_CONFIG_VALUE_INT:
    case UFSRV_CONFIG_VALUE_ENUM:
        fprintf(fp, "%lld", (long long)v->stored_value.int_val);
        break;

    case UFSRV_CONFIG_VALUE_BOOL:
        fputs(v->stored_value.int_val ? "true" : "false", fp);
        break;

    case UFSRV_CONFIG_VALUE_DOUBLE:
        fprintf(fp, "%.17g", v->stored_value.double_value);
        break;

    case UFSRV_CONFIG_VALUE_STR:
        sJsonEscape(fp, (const char *)v->stored_value.str_val);
        break;

    case UFSRV_CONFIG_VALUE_ARRAY:
        sDumpJsonArray(fp, v);
        break;

    case UFSRV_CONFIG_VALUE_TYPE_UNSPECIFIED:
    default:
        fputs("null", fp);
        break;
    }
}

/* Emit one symbol as a JSON object field (caller supplies leading comma policy). */
static void
sDumpJsonEntry(FILE *fp, const ConfFileSymbolTableEntry *e, const struct UfsrvConfigKeyMapEntry *map, size_t map_sz, int *first_in_scope)
{
    /*const char *name = sLookupConfigLiteral(map, map_sz, e->field_index);
    if (!name) name = e->key;
    if (!name) name = "<unknown>";*/

    const char *name = e->key;

    if (!*first_in_scope) fputs(",\n", fp);
    *first_in_scope = 0;

    fputs("    ", fp);
    sJsonEscape(fp, name);
    fputs(": {\n", fp);

    fputs("      \"value\": ", fp);
    sDumpJsonValue(fp, e);
    fputs(",\n", fp);

    fputs("      \"type\": ", fp);
    sJsonEscape(fp, UfsrvConfigTypeLiteral(e->type));
    fputs(",\n", fp);
    fprintf(fp, "      \"field_index\": %d,\n", e->field_index);
    fprintf(fp, "      \"required\": %s,\n", e->is_required ? "true" : "false");
    fprintf(fp, "      \"source\": ");
    sJsonEscape(fp, UfsrvConfigSourceNameLiteral(e->value.source));
    fputs(",\n", fp);

    fputs("      \"description\": ", fp);
    sJsonEscape(fp, e->description ? e->description : "");
    fputs("\n    }", fp);
}

/* True if scope already recorded in list[0..n). */
static int
sScopeSeen(const char **list, size_t n, const char *scope)
{
    for (size_t i = 0; i < n; i++) {
        if (strcmp(list[i], scope) == 0) return 1;
    }
    return 0;
}

PUBLIC_API int
ConfFileDumpJson(const ConfFileSymbolTableEntry *g_ufsrv_config_symbols, const ConfFileSymbolTableEntry *g_ufsrv_config_app_symbols, const UfsrvConfigKeyMapEntry *map, const size_t map_sz, const char *json_dump_file)
{
    FILE *fp = stderr;
    int close_fp = 0;

    if (json_dump_file && *json_dump_file) {
        fp = fopen(json_dump_file, "w");
        if (!fp) return -1;
        close_fp = 1;
    }

    const ConfFileSymbolTableEntry *tables[2] = {
        g_ufsrv_config_symbols,
        g_ufsrv_config_app_symbols
    };

    /* Collect unique scopes in first-seen order (max 64 scopes). */
    const char *scopes[64] = {0};
    size_t nscopes = 0;

    for (int t = 0; t < 2; t++) {
        if (!tables[t]) continue;
        for (const ConfFileSymbolTableEntry *e = tables[t]; e->scope != NULL; e++) {
            if (nscopes < 64 && !sScopeSeen(scopes, nscopes, e->scope)) scopes[nscopes++] = e->scope;
        }
    }

    fputs("{\n", fp);

    for (size_t s = 0; s < nscopes; s++) {
        if (s > 0) fputs(",\n", fp);

        fputs("  ", fp);
        sJsonEscape(fp, scopes[s]);
        fputs(": {\n", fp);

        int first_in_scope = 1;

        for (int t = 0; t < 2; t++) {
            if (!tables[t]) continue;
            for (const ConfFileSymbolTableEntry *e = tables[t]; e->scope != NULL; e++) {
                if (strcmp(e->scope, scopes[s]) != 0) continue;
                sDumpJsonEntry(fp, e, map, map_sz, &first_in_scope);
            }
        }

        fputs("\n  }", fp);
    }

    fputs("\n}\n", fp);

    if (close_fp) fclose(fp);

    return 0;
}