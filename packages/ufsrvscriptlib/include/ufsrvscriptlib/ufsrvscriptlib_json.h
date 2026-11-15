/**
 * @file ufsrvscriptlib_json.h
 * @brief Native Lua bindings for JSON — the ufsrvscriptlib interface seam.
 *
 * Declares UfsrvScriptJsonRegister(), which builds the `json` global over the
 * vendored lua-cjson core. JSON is a stateless codec with no external C API to
 * wrap, so — unlike redis/mysql — there is no UfsrvScriptJson* wrapper seam:
 * the seam is a registration shim (re-homing the vendored loader under the
 * uniform global + error convention). See
 * dev/technical_designs/UFSRVCORELIB_UFSRVSCRIPT_JSON_DESIGN.md.
 *
 * This adds no new dependency: the vendored core links nothing beyond the Lua
 * API and libc (see §3 of the design).
 */
#ifndef UFSRVSCRIPTLIB_JSON_H
#define UFSRVSCRIPTLIB_JSON_H

#include <uflib/uflib_defs.h>

#include <lua.h>

/**
 * @brief Build the `json` global table on the given lua_State.
 *
 * Call after UfsrvScriptBootstrap() and before running any script. The module
 * table is set via lua_setglobal (C-level), which strict.lua exempts.
 */
PUBLIC_API void UfsrvScriptJsonRegister(lua_State *L);

#endif /* UFSRVSCRIPTLIB_JSON_H */
