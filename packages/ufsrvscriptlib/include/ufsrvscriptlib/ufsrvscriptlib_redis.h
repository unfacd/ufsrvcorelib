/**
 * @file ufsrvscriptlib_redis.h
 * @brief Native Lua bindings for the hiredis Redis client.
 *
 * A thin `UfsrvScriptRedis*` wrapper over hiredis (the Lua-agnostic seam) plus
 * `UfsrvScriptRedisRegister()`, which builds the `redis` global table
 * (`connect`, `unwrap_reply`, `REPLY_*`/`ERR_*` enums, `NIL`, `status`) and the
 * connection userdata/metatable object model, ported from lua-hiredis.
 *
 * This is a deliberate addition to the ufsrvscriptlib dependency set: the redis
 * binding links the vendored hiredis (see packages/hiredis/).
 */
#ifndef UFSRVSCRIPTLIB_REDIS_H
#define UFSRVSCRIPTLIB_REDIS_H

#include <uflib/uflib_defs.h>

#include <stddef.h>

#include <hiredis.h>
#include <lua.h>

/* Thin hiredis wrappers — the Lua-agnostic seam the bindings call. */
PUBLIC_API redisContext *UfsrvScriptRedisConnect(const char *address, int port);
PUBLIC_API redisReply   *UfsrvScriptRedisCommand(redisContext *ctx, int argc,
                                                 const char **argv, const size_t *argvlen);
PUBLIC_API void          UfsrvScriptRedisAppendCommand(redisContext *ctx, int argc,
                                                       const char **argv, const size_t *argvlen);
PUBLIC_API int           UfsrvScriptRedisGetReply(redisContext *ctx, redisReply **reply_out);
PUBLIC_API void          UfsrvScriptRedisClose(redisContext *ctx);

/**
 * @brief Build the `redis` global table on the given lua_State.
 *
 * Call after UfsrvScriptBootstrap() and before running any script. The module
 * table is set via lua_setglobal (C-level), which strict.lua exempts.
 */
PUBLIC_API void UfsrvScriptRedisRegister(lua_State *L);

#endif /* UFSRVSCRIPTLIB_REDIS_H */
