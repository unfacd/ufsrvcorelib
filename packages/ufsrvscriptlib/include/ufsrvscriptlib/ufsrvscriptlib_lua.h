/**
 * @file ufsrvscriptlib_lua.h
 * @brief Generic, class-agnostic Lua scripting engine.
 *
 * A minimal Lua command host. It owns a sandboxed lua_State, installs a
 * `ufsrvscript` table of helpers (call/pcall/error_reply/status_reply/log), and
 * registers an application-supplied, NULL-sentinel array of script-callable
 * bindings.
 *
 * The engine is deliberately dependency-clean: it links only against liblua
 * (and uflib solely for the PUBLIC_API visibility macro). It knows nothing
 * about ufsrvapi, ufsrvwebsock, curl, or any specific ufsrv service, so it can
 * be reused by any ufsrv application that wants scriptable operations.
 *
 * Error contract: a binding pushes exactly one result — a Lua value on success,
 * or a `{ err = "..." }` table on failure — and never calls lua_error() itself.
 * `ufsrvscript.call` raises that error (halting the script); `ufsrvscript.pcall` returns
 * the `{ err = ... }` table unchanged.
 */
#ifndef UFSRVSCRIPTLIB_LUA_H
#define UFSRVSCRIPTLIB_LUA_H

#include <uflib/uflib_defs.h>

#include <lua.h>
#include <stddef.h>

#include <ufsrvscriptlib/ufsrvscriptlib_function_binding_type.h>
#include <ufsrvscriptlib/ufsrvscriptlib_constant_binding_type.h>
#include <ufsrvscriptlib/ufsrvscriptlib_lua_options_type.h>

/* Name of the global table that holds the engine helpers and the application's
 * bindings. Scripts call e.g. ufsrvscript.call("register", {...}). */
#define UFSRVSCRIPTLIB_GLOBAL "ufsrvscript"

typedef struct UfsrvScriptLua UfsrvScriptLua;

/**
 * @brief Create the VM, sandbox it, install the error handler and the
 *        `ufsrvscript` helper table, and register `bindings_ptr` and
 *        `constants_ptr`.
 *
 * @details The created lua_State is whitelisted to base/table/string/math/debug
 *          (plus os/package when opts_ptr enables them), has loadfile/dofile
 *          pruned, and — when opts_ptr->is_strict_globals is true — rejects
 *          accidental global creation via a strict.lua metatable.
 *
 * @param[in] bindings_ptr  NULL-sentinel UfsrvScriptFunctionBinding array (borrowed; must
 *                          outlive the handle). May be NULL for no bindings.
 * @param[in] constants_ptr NULL-sentinel UfsrvScriptConstantBinding array of read-only
 *                          globals; each name is a dotted path ("ufsrvcli.version")
 *                          that builds nested tables. May be NULL for none.
 * @param[in] opts_ptr      Sandbox policy. NULL selects all defaults
 *                          (strict globals on, os/package off).
 *
 * @return A ready-to-use handle, or NULL on allocation failure.
 *
 * @code{.c}
 * static const UfsrvScriptFunctionBinding sBindings[] = {
 *     { "register", 0, sRegister },
 *     { NULL, 0, NULL }
 * };
 * static const UfsrvScriptConstantBinding sConstants[] = {
 *     { "ufsrvcli.version", UFSRVSCRIPTLIB_CONSTANT_TYPE_STRING, "0.1.0", 0, 0 },
 *     { NULL, UFSRVSCRIPTLIB_CONSTANT_TYPE_NONE, NULL, 0, 0 }
 * };
 * UfsrvScriptLuaOptions opts = { true, false, false };
 * UfsrvScriptLua *lua = UfsrvScriptBootstrap(sBindings, sConstants, &opts);
 * if (!lua) { ... }
 * UfsrvScriptLuaLoadBuffer(lua, "print(ufsrvscript.call('register', {...}))",
 *                           strlen(src), "@inline");
 * if (UfsrvScriptLuaRun(lua) != 0) fprintf(stderr, "%s\n",
 *                                           UfsrvScriptLuaErrorString(lua));
 * UfsrvScriptLuaClose(lua);
 * @endcode
 */
PUBLIC_API UfsrvScriptLua *
UfsrvScriptBootstrap(const UfsrvScriptFunctionBinding *bindings_ptr,
                     const UfsrvScriptConstantBinding *constants_ptr,
                     const UfsrvScriptLuaOptions *opts_ptr);

/**
 * @brief Release the VM and all associated state.
 *
 * @param[in] lua_ptr  Handle to release. NULL is a no-op.
 */
PUBLIC_API void
UfsrvScriptLuaClose(UfsrvScriptLua *lua_ptr);

/**
 * @brief Load a script from a file, compiling and defining it for a later run.
 *
 * @param[in] lua_ptr   Engine handle.
 * @param[in] path_ptr  Filesystem path, or "-" to read the script from stdin.
 *
 * @return 0 on success, -1 on failure (UfsrvScriptLuaErrorString() has the
 *         reason).
 */
PUBLIC_API int
UfsrvScriptLuaLoadFile(UfsrvScriptLua *lua_ptr, const char *path_ptr);

/**
 * @brief Load a script from an in-memory buffer.
 *
 * @param[in] lua_ptr        Engine handle.
 * @param[in] buf_ptr        Script source (need not be NUL-terminated beyond len).
 * @param[in] len            Length of buf_ptr in bytes.
 * @param[in] chunkname_ptr  Lua chunk name used in error messages (e.g. "@stdin").
 *
 * @return 0 on success, -1 on failure.
 */
PUBLIC_API int
UfsrvScriptLuaLoadBuffer(UfsrvScriptLua *lua_ptr, const char *buf_ptr,
                         size_t len, const char *chunkname_ptr);

/**
 * @brief Run the loaded script, leaving its single return value on the Lua
 *        stack (reachable via UfsrvScriptLuaState()).
 *
 * @param[in] lua_ptr  Engine handle.
 *
 * @return 0 on success, -1 on failure (UfsrvScriptLuaErrorString() has the
 *         reason).
 */
PUBLIC_API int
UfsrvScriptLuaRun(UfsrvScriptLua *lua_ptr);

/**
 * @brief Return the underlying lua_State, for the host to inspect the script's
 *        result after UfsrvScriptLuaRun() or pre-load extra libraries.
 *
 * @param[in] lua_ptr  Engine handle.
 *
 * @return The lua_State, or NULL when lua_ptr is NULL.
 */
PUBLIC_API lua_State *
UfsrvScriptLuaState(UfsrvScriptLua *lua_ptr);

/**
 * @brief Return the last load/run error as a NUL-terminated string.
 *
 * @param[in] lua_ptr  Engine handle.
 *
 * @return The last error ("" when none).
 */
PUBLIC_API const char *
UfsrvScriptLuaErrorString(UfsrvScriptLua *lua_ptr);

/**
 * @brief Push a `{ err = "..." }` table onto the Lua stack (a binding helper).
 *
 * @param[in] lua_state_ptr  Lua state supplied to the binding.
 * @param[in] fmt_ptr        printf-style format string for the error message.
 */
PUBLIC_API void
UfsrvScriptLuaPushError(lua_State *lua_state_ptr, const char *fmt_ptr, ...);

/**
 * @brief Push a `{ ok = "..." }` table onto the Lua stack (a binding helper).
 *
 * @param[in] lua_state_ptr  Lua state supplied to the binding.
 * @param[in] msg_ptr        Status message.
 */
PUBLIC_API void
UfsrvScriptLuaPushStatus(lua_State *lua_state_ptr, const char *msg_ptr);

#endif /* UFSRVSCRIPTLIB_LUA_H */
