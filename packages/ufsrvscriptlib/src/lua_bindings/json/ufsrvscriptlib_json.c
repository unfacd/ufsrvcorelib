/**
 * @file ufsrvscriptlib_json.c
 * @brief Native Lua bindings for JSON — the ufsrvscriptlib interface seam.
 *
 * A registration shim over the vendored lua-cjson core (lua_cjson.c +
 * strbuf/fpconv, MIT — see
 * dev/technical_designs/UFSRVCORELIB_UFSRVSCRIPT_JSON_DESIGN.md). JSON is a
 * stateless codec with no external C API, so the seam does only what the uniform
 * interface requires: build the module table via the vendor's "safe" loader (so
 * encode/decode return nil,err) and re-home it under the `json` global with the
 * uniform error convention.
 */
#include <ufsrvscriptlib/ufsrvscriptlib_json.h>

#include <lua.h>

/* The vendored lua-cjson core exports its loaders. The engine prunes require,
 * so this calls the loader directly and re-homes the table under `json`. The
 * "safe" loader wraps encode/decode in pcall, yielding the uniform nil,err
 * convention (see the design doc §6). */
extern int luaopen_cjson_safe(lua_State *L);

PUBLIC_API void
UfsrvScriptJsonRegister(lua_State *L)
{
    luaopen_cjson_safe(L);      /* build the cjson table (nil,err encode/decode) */
    lua_pushliteral(L, "json"); /* align _NAME with the uniform global          */
    lua_setfield(L, -2, "_NAME");
    lua_setglobal(L, "json");   /* re-home under the uniform global name        */
}
