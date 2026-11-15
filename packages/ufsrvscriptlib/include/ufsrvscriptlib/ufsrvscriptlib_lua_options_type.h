/**
 * @file ufsrvscriptlib_lua_options_type.h
 * @brief Sandbox policy options for the ufsrvscriptlib Lua engine.
 *
 * Type-only header: defines UfsrvScriptLuaOptions, the bootstrap-time sandbox
 * policy. It carries no functions and includes only what the type itself needs
 * (<stdbool.h> for bool), so a consumer that merely wants to embed the options
 * struct by value need not pull in the engine API.
 */
#ifndef UFSRVSCRIPTLIB_LUA_OPTIONS_TYPE_H
#define UFSRVSCRIPTLIB_LUA_OPTIONS_TYPE_H

#include <stdbool.h>

/*! Sandbox policy for the created lua_State. */
typedef struct UfsrvScriptLuaOptions {
  bool is_strict_globals;   ///< true = reject accidental global creation/read (strict.lua guard)
  bool is_os_allowed;       ///< true = load the `os` library (filesystem/process escape)
  bool is_package_allowed;  ///< true = load the `package` library (`loadfile`/`dofile` pruned regardless)
} UfsrvScriptLuaOptions;

#endif /* UFSRVSCRIPTLIB_LUA_OPTIONS_TYPE_H */
