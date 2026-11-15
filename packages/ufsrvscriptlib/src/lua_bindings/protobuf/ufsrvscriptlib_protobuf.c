/**
 * @file ufsrvscriptlib_protobuf.c
 * @brief Native Lua bindings for Protocol Buffers — the ufsrvscriptlib interface
 *        seam over the vendored lua-protobuf core.
 *
 * A registration shim, in the same spirit as json: there is no UfsrvScriptProtobuf*
 * API to wrap, only a uniform entry point that (a) composes the vendored pb
 * openers into the `pb` global, and (b) loads the two pure-Lua companions
 * (`protoc`, `serpent`) in a private _ENV so their `require "pb"` resolves
 * without opening the sandbox `package` library.
 *
 * Adaptations from lua-protobuf (MIT, see design §12):
 *  - the `pb.buffer`/`pb.slice`/`pb.conv` submodules are attached directly (the
 *    upstream distribution loads them via `require "pb.buffer"` etc.);
 *  - `pb.io` and `pb.unsafe` are NOT attached, and the core `pb.loadfile` is
 *    nil'd (filesystem-write / raw-pointer / filesystem-read surfaces the
 *    sandbox closes — design §10);
 *  - `protoc.lua`/`serpent.lua` are embedded at build time (xxd -i) and run in a
 *    private _ENV whose `require` resolves only `pb`, and whose `__index` does a
 *    rawget against _G so the strict-globals guard does not reject the
 *    companions' own `local io = io` bindings.
 */
#include <ufsrvscriptlib/ufsrvscriptlib_protobuf.h>

#include <lauxlib.h>
#include <stdio.h>
#include <string.h>

/* Embedded companions — generated at build time from the pristine vendored
 * protoc.lua / serpent.lua (see packages/ufsrvscriptlib/CMakeLists.txt). */
#include "protoc_lua.h"    /* protoc_lua[] / protoc_lua_len   */
#include "serpent_lua.h"   /* serpent_lua[] / serpent_lua_len */

/* Vendored lua-protobuf openers — the only external symbols pb.c exports.
 * pb.unsafe / pb.io are deliberately not declared (not exposed). */
LUALIB_API int luaopen_pb(lua_State *L);
LUALIB_API int luaopen_pb_buffer(lua_State *L);
LUALIB_API int luaopen_pb_slice(lua_State *L);
LUALIB_API int luaopen_pb_conv(lua_State *L);

/* Resolve the single module the companions need: protoc.lua does
 * `pcall(require, "pb")` with no global fallback. The shim lives only inside the
 * companions' private _ENV — never in _G, never visible to scripts. */
static int
sProtobufRequire(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);

    if (strcmp(name, "pb") == 0) {
        lua_getglobal(L, "pb");
        return 1;
    }
    return luaL_error(L, "module '%s' not found", name);
}

/* __index for the companions' private _ENV: rawget against _G, so a missing
 * global (e.g. `io`, which the sandbox does not load) reads as nil rather than
 * tripping the strict-globals guard that _G itself carries. */
static int
sCompanionEnvIndex(lua_State *L)
{
    /* Do not leak the real _G (which carries the strict-globals guard) into the
     * companions: return nil for "_G" so serpent.lua's `_G or _ENV` picks the
     * companion's own permissive env instead of the guarded globals table. */
    if (lua_type(L, 2) == LUA_TSTRING) {
        size_t key_len = 0;
        const char *key = lua_tolstring(L, 2, &key_len);

        if (key_len == 2 && key[0] == '_' && key[1] == 'G') {
            lua_pushnil(L);
            return 1;
        }
    }

    lua_pushglobaltable(L);      /* [env, key, _G]     */
    lua_pushvalue(L, 2);         /* [env, key, _G, key] */
    lua_rawget(L, -2);           /* [env, key, _G, val] */
    lua_remove(L, -2);           /* [env, key, val]     */
    return 1;
}

/* Load + run one embedded companion chunk in a private _ENV. Returns 1 with the
 * module table on the stack on success, or 0 (stack balanced) on failure. */
static int
sLoadCompanion(lua_State *L, const unsigned char *src, size_t len,
               const char *chunkname)
{
    if (luaL_loadbuffer(L, (const char *)src, len, chunkname) != LUA_OK) {
        fprintf(stderr, "ufsrvscriptlib_protobuf: %s: %s\n", chunkname,
                lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }

    lua_newtable(L);                        /* [chunk, env]              */
    lua_newtable(L);                        /* [chunk, env, mt]          */
    lua_pushcfunction(L, sCompanionEnvIndex);
    lua_setfield(L, -2, "__index");         /* mt.__index (raw _G view)  */
    lua_setmetatable(L, -2);                /* setmetatable(env, mt)     */
    lua_pushcfunction(L, sProtobufRequire);
    lua_setfield(L, -2, "require");         /* env.require (pb only)     */
    lua_setupvalue(L, -2, 1);               /* chunk._ENV = env (pops it) */

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        fprintf(stderr, "ufsrvscriptlib_protobuf: %s: %s\n", chunkname,
                lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    return 1;
}

PUBLIC_API void
UfsrvScriptProtobufRegister(lua_State *L)
{
    luaopen_pb(L);                          /* [pb]                     */
    luaopen_pb_buffer(L);  lua_setfield(L, -2, "buffer");   /* pb.buffer */
    luaopen_pb_slice(L);   lua_setfield(L, -2, "slice");    /* pb.slice  */
    luaopen_pb_conv(L);    lua_setfield(L, -2, "conv");     /* pb.conv   */
    lua_pushnil(L);        lua_setfield(L, -2, "loadfile"); /* no fs read */
    lua_setglobal(L, "pb");                 /* _G.pb = pb               */

    if (sLoadCompanion(L, protoc_lua, protoc_lua_len, "=protoc.lua"))
        lua_setglobal(L, "protoc");
    if (sLoadCompanion(L, serpent_lua, serpent_lua_len, "=serpent.lua"))
        lua_setglobal(L, "serpent");
}
