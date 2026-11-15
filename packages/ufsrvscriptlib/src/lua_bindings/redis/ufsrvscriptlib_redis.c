/**
 * @file ufsrvscriptlib_redis.c
 * @brief Native Lua bindings for the hiredis Redis client.
 *
 * Ports the lua-hiredis object model — `connect(host, port)` returning a
 * userdata connection with `:command()`, `:append_command()`, `:get_reply()`,
 * `:close()` — to the ufsrvscriptlib engine, over a thin `UfsrvScriptRedis*`
 * wrapper around hiredis.
 *
 * Adaptations from lua-hiredis (BSD-3-Clause):
 *  - Lua 5.4 API (native luaL_newlib/luaL_setfuncs, no luaL_register shim);
 *  - the module table is reached with lua_getglobal("redis") instead of the
 *    fragile caller-upvalue idiom, so the connection methods carry no upvalues.
 */
#include <ufsrvscriptlib/ufsrvscriptlib_redis.h>

#include <lauxlib.h>
#include <stdio.h>
#include <string.h>

#define UFSRVSCRIPTLIB_REDIS_VERSION     "ufsrvscriptlib_redis 0.1.0"
#define UFSRVSCRIPTLIB_REDIS_COPYRIGHT   "Copyright (C) 2015-2026 unfacd works"
#define UFSRVSCRIPTLIB_REDIS_DESCRIPTION "Lua bindings for the hiredis Redis client (adapted from lua-hiredis)"

#define UFSRVSCRIPTLIB_REDIS_CONN_MT    "ufsrvscript.redis.connection"
#define UFSRVSCRIPTLIB_REDIS_CONST_MT   "ufsrvscript.redis.const"
#define UFSRVSCRIPTLIB_REDIS_STATUS_MT  "ufsrvscript.redis.status"
#define UFSRVSCRIPTLIB_REDIS_NIL_KEY    "NIL"
#define UFSRVSCRIPTLIB_REDIS_MAXARGS    (256)

/* ---------------------------------------------------------------------------
 * Thin hiredis wrappers (the Lua-agnostic seam).
 * ------------------------------------------------------------------------- */

redisContext *
UfsrvScriptRedisConnect(const char *address, int port)
{
    return redisConnect(address, port);
}

redisReply *
UfsrvScriptRedisCommand(redisContext *ctx, int argc, const char **argv,
                        const size_t *argvlen)
{
    return (redisReply *)redisCommandArgv(ctx, argc, argv, argvlen);
}

void
UfsrvScriptRedisAppendCommand(redisContext *ctx, int argc, const char **argv,
                              const size_t *argvlen)
{
    (void)redisAppendCommandArgv(ctx, argc, argv, argvlen);
}

int
UfsrvScriptRedisGetReply(redisContext *ctx, redisReply **reply_out)
{
    return redisGetReply(ctx, (void **)reply_out);
}

void
UfsrvScriptRedisClose(redisContext *ctx)
{
    redisFree(ctx);
}

/* ---------------------------------------------------------------------------
 * Lua object model.
 * ------------------------------------------------------------------------- */

typedef struct UfsrvScriptRedisEnum {
    const char *name;
    int         value;
} UfsrvScriptRedisEnum;

typedef struct UfsrvScriptRedisConn {
    redisContext *context;
} UfsrvScriptRedisConn;

static void
sRegisterEnums(lua_State *lua_state_ptr, const UfsrvScriptRedisEnum *enum_ptr)
{
    for (; enum_ptr->name; ++enum_ptr) {
        lua_pushinteger(lua_state_ptr, enum_ptr->value);
        lua_setfield(lua_state_ptr, -2, enum_ptr->name);
    }
}

/* A "const object" — a table { name, type } carrying a metatable; used for
 * STATUS/ERROR replies and NIL so scripts can test reply kind without string
 * comparison. */
static int sConstToString(lua_State *lua_state_ptr);

static int
sPushNewConst(lua_State *lua_state_ptr, const char *name, size_t name_len, int type)
{
    static const luaL_Reg sConstMt[] = {
        { "__tostring", sConstToString },
        { NULL, NULL }
    };

    lua_createtable(lua_state_ptr, 0, 2);
    lua_pushlstring(lua_state_ptr, name, name_len);
    lua_setfield(lua_state_ptr, -2, "name");
    lua_pushinteger(lua_state_ptr, type);
    lua_setfield(lua_state_ptr, -2, "type");

    if (luaL_newmetatable(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_CONST_MT)) {
        luaL_setfuncs(lua_state_ptr, sConstMt, 0);
        lua_pushvalue(lua_state_ptr, -1);
        lua_setfield(lua_state_ptr, -2, "__index");
        lua_pushliteral(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_CONST_MT);
        lua_setfield(lua_state_ptr, -2, "__metatable");
    }
    lua_setmetatable(lua_state_ptr, -2);

    return 1;
}

/* Metamethod: tostring(const_object) -> const_object.name. */
static int
sConstToString(lua_State *lua_state_ptr)
{
    luaL_checktype(lua_state_ptr, 1, LUA_TTABLE);
    lua_getfield(lua_state_ptr, 1, "name");
    return 1;
}

/* status table __index: status["OK"] creates (and caches) a const object. */
static int
sStatusIndex(lua_State *lua_state_ptr)
{
    size_t key_len = 0;
    const char *key;

    luaL_checktype(lua_state_ptr, 1, LUA_TTABLE);
    key = luaL_checklstring(lua_state_ptr, 2, &key_len);

    sPushNewConst(lua_state_ptr, key, key_len, REDIS_REPLY_STATUS);
    lua_rawset(lua_state_ptr, 1); /* t[key] = const */

    lua_pushlstring(lua_state_ptr, key, key_len);
    lua_gettable(lua_state_ptr, 1);
    return 1;
}

static const luaL_Reg sStatusMt[] = {
    { "__index", sStatusIndex },
    { NULL, NULL }
};

static const UfsrvScriptRedisEnum sErrors[] = {
    { "ERR_IO",       REDIS_ERR_IO },
    { "ERR_EOF",      REDIS_ERR_EOF },
    { "ERR_PROTOCOL", REDIS_ERR_PROTOCOL },
    { "ERR_OTHER",    REDIS_ERR_OTHER },
    { NULL, 0 }
};

static const UfsrvScriptRedisEnum sReplyTypes[] = {
    { "REPLY_STRING",  REDIS_REPLY_STRING },
    { "REPLY_ARRAY",   REDIS_REPLY_ARRAY },
    { "REPLY_INTEGER", REDIS_REPLY_INTEGER },
    { "REPLY_NIL",     REDIS_REPLY_NIL },
    { "REPLY_STATUS",  REDIS_REPLY_STATUS },
    { "REPLY_ERROR",   REDIS_REPLY_ERROR },
    { NULL, 0 }
};

/* Return the connection's redisContext, raising if the userdata is closed. */
static redisContext *
sCheckConnection(lua_State *lua_state_ptr, int idx)
{
    UfsrvScriptRedisConn *conn = (UfsrvScriptRedisConn *)luaL_checkudata(
        lua_state_ptr, idx, UFSRVSCRIPTLIB_REDIS_CONN_MT);

    if (!conn->context)
        luaL_error(lua_state_ptr, "ufsrvscriptlib_redis: attempted to use a closed connection");
    return conn->context;
}

/* Push nil, errstr, errno (the lua-hiredis multi-return error convention). */
static int
sPushError(lua_State *lua_state_ptr, redisContext *ctx)
{
    lua_pushnil(lua_state_ptr);
    lua_pushstring(lua_state_ptr, ctx->errstr[0] ? ctx->errstr : "(no error message)");
    lua_pushinteger(lua_state_ptr, ctx->err);
    return 3;
}

/* Read the command argv/argvlen from stack [idx..top]; returns argc. */
static int
sLoadArgs(lua_State *lua_state_ptr, int idx, const char **argv, size_t *argvlen)
{
    int nargs = lua_gettop(lua_state_ptr) - idx + 1;
    int i;

    if (nargs <= 0)
        return luaL_error(lua_state_ptr, "missing command name");
    if (nargs > UFSRVSCRIPTLIB_REDIS_MAXARGS)
        return luaL_error(lua_state_ptr, "too many arguments");

    for (i = 0; i < nargs; ++i) {
        size_t len = 0;
        const char *str = lua_tolstring(lua_state_ptr, idx + i, &len);

        if (!str)
            return luaL_argerror(lua_state_ptr, idx + i, "expected a string or number value");
        argv[i] = str;
        argvlen[i] = len;
    }
    return nargs;
}

/* Convert a redisReply into a Lua value (recursive for arrays). */
static int
sPushReply(lua_State *lua_state_ptr, redisReply *reply)
{
    switch (reply->type) {
        case REDIS_REPLY_STATUS:
            lua_getglobal(lua_state_ptr, "redis");
            lua_getfield(lua_state_ptr, -1, "status");
            lua_remove(lua_state_ptr, -2);
            lua_pushlstring(lua_state_ptr, reply->str, reply->len);
            lua_gettable(lua_state_ptr, -2); /* status[name] */
            lua_remove(lua_state_ptr, -2);
            break;

        case REDIS_REPLY_ERROR:
            sPushNewConst(lua_state_ptr, reply->str, reply->len, REDIS_REPLY_ERROR);
            break;

        case REDIS_REPLY_INTEGER:
            lua_pushinteger(lua_state_ptr, reply->integer);
            break;

        case REDIS_REPLY_NIL:
            lua_getglobal(lua_state_ptr, "redis");
            lua_getfield(lua_state_ptr, -1, UFSRVSCRIPTLIB_REDIS_NIL_KEY);
            lua_remove(lua_state_ptr, -2);
            break;

        case REDIS_REPLY_STRING:
            lua_pushlstring(lua_state_ptr, reply->str, reply->len);
            break;

        case REDIS_REPLY_ARRAY: {
            unsigned int i;

            lua_createtable(lua_state_ptr, reply->elements, 0);
            for (i = 0; i < reply->elements; ++i) {
                sPushReply(lua_state_ptr, reply->element[i]);
                lua_rawseti(lua_state_ptr, -2, (int)i + 1);
            }
            break;
        }

        default:
            return luaL_error(lua_state_ptr, "unknown reply type: %d", reply->type);
    }
    return 1;
}

static int
sConnCommand(lua_State *lua_state_ptr)
{
    redisContext *ctx = sCheckConnection(lua_state_ptr, 1);
    const char *argv[UFSRVSCRIPTLIB_REDIS_MAXARGS];
    size_t argvlen[UFSRVSCRIPTLIB_REDIS_MAXARGS];
    int nargs = sLoadArgs(lua_state_ptr, 2, argv, argvlen);
    redisReply *reply = UfsrvScriptRedisCommand(ctx, nargs, argv, argvlen);
    int nret;

    if (!reply)
        return sPushError(lua_state_ptr, ctx);

    nret = sPushReply(lua_state_ptr, reply);
    freeReplyObject(reply);
    return nret;
}

static int
sConnAppendCommand(lua_State *lua_state_ptr)
{
    redisContext *ctx = sCheckConnection(lua_state_ptr, 1);
    const char *argv[UFSRVSCRIPTLIB_REDIS_MAXARGS];
    size_t argvlen[UFSRVSCRIPTLIB_REDIS_MAXARGS];
    int nargs = sLoadArgs(lua_state_ptr, 2, argv, argvlen);

    UfsrvScriptRedisAppendCommand(ctx, nargs, argv, argvlen);
    return 0;
}

static int
sConnGetReply(lua_State *lua_state_ptr)
{
    redisContext *ctx = sCheckConnection(lua_state_ptr, 1);
    redisReply *reply = NULL;
    int nret;

    if (UfsrvScriptRedisGetReply(ctx, &reply) != REDIS_OK || !reply)
        return sPushError(lua_state_ptr, ctx);

    nret = sPushReply(lua_state_ptr, reply);
    freeReplyObject(reply);
    return nret;
}

static int
sConnClose(lua_State *lua_state_ptr)
{
    UfsrvScriptRedisConn *conn = (UfsrvScriptRedisConn *)luaL_checkudata(
        lua_state_ptr, 1, UFSRVSCRIPTLIB_REDIS_CONN_MT);

    if (conn && conn->context) {
        UfsrvScriptRedisClose(conn->context);
        conn->context = NULL;
    }
    return 0;
}

static int
sConnToString(lua_State *lua_state_ptr)
{
    (void)sCheckConnection(lua_state_ptr, 1);
    lua_pushliteral(lua_state_ptr, "ufsrvscript.redis.connection");
    return 1;
}

static const luaL_Reg sConnMt[] = {
    { "command",         sConnCommand },
    { "append_command",  sConnAppendCommand },
    { "get_reply",       sConnGetReply },
    { "close",           sConnClose },
    { "__gc",            sConnClose },
    { "__tostring",      sConnToString },
    { NULL, NULL }
};

static int
sConnect(lua_State *lua_state_ptr)
{
    const char *host = luaL_checkstring(lua_state_ptr, 1);
    int port = (int)luaL_checkinteger(lua_state_ptr, 2);
    redisContext *ctx = UfsrvScriptRedisConnect(host, port);
    UfsrvScriptRedisConn *conn;

    if (!ctx) {
        lua_pushnil(lua_state_ptr);
        lua_pushliteral(lua_state_ptr, "failed to create hiredis context");
        return 2;
    }
    if (ctx->err) {
        int nret = sPushError(lua_state_ptr, ctx);

        UfsrvScriptRedisClose(ctx);
        return nret;
    }

    conn = (UfsrvScriptRedisConn *)lua_newuserdata(lua_state_ptr, sizeof(*conn));
    conn->context = ctx;

    if (luaL_newmetatable(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_CONN_MT)) {
        luaL_setfuncs(lua_state_ptr, sConnMt, 0);
        lua_pushvalue(lua_state_ptr, -1);
        lua_setfield(lua_state_ptr, -2, "__index");
    }
    lua_setmetatable(lua_state_ptr, -2);

    return 1;
}

/* Unwrap a const object: (status_name, type) for STATUS, (nil, name) for ERROR,
 * or the value unchanged for anything else. */
static int
sUnwrapReply(lua_State *lua_state_ptr)
{
    int type;

    luaL_checkany(lua_state_ptr, 1);

    if (!lua_istable(lua_state_ptr, 1)) {
        if (lua_isnil(lua_state_ptr, 1) && !lua_isnoneornil(lua_state_ptr, 2)) {
            lua_pushvalue(lua_state_ptr, 1);
            lua_pushvalue(lua_state_ptr, 2);
            return 2;
        }
        lua_pushvalue(lua_state_ptr, 1);
        return 1;
    }

    if (!lua_getmetatable(lua_state_ptr, 1)) {
        lua_pushvalue(lua_state_ptr, 1);
        return 1;
    }

    luaL_getmetatable(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_CONST_MT);
    if (!lua_rawequal(lua_state_ptr, -1, -2)) {
        lua_pop(lua_state_ptr, 2);
        lua_pushvalue(lua_state_ptr, 1);
        return 1;
    }
    lua_pop(lua_state_ptr, 2);

    lua_getfield(lua_state_ptr, 1, "type");
    if (lua_type(lua_state_ptr, -1) != LUA_TNUMBER) {
        lua_pop(lua_state_ptr, 1);
        lua_pushvalue(lua_state_ptr, 1);
        return 1;
    }
    type = (int)lua_tointeger(lua_state_ptr, -1);
    lua_pop(lua_state_ptr, 1);

    if (type == REDIS_REPLY_STATUS) {
        lua_getfield(lua_state_ptr, 1, "name");
        if (!lua_isstring(lua_state_ptr, -1))
            return luaL_error(lua_state_ptr, "ufsrvscriptlib_redis internal error: bad const object");
        lua_pushinteger(lua_state_ptr, REDIS_REPLY_STATUS);
        return 2;
    } else if (type == REDIS_REPLY_ERROR) {
        lua_pushnil(lua_state_ptr);
        lua_getfield(lua_state_ptr, 1, "name");
        if (!lua_isstring(lua_state_ptr, -1))
            return luaL_error(lua_state_ptr, "ufsrvscriptlib_redis internal error: bad const object");
        return 2;
    }

    lua_pushvalue(lua_state_ptr, 1);
    return 1;
}

static const luaL_Reg sRedisModule[] = {
    { "connect",      sConnect },
    { "unwrap_reply", sUnwrapReply },
    { NULL, NULL }
};

PUBLIC_API void
UfsrvScriptRedisRegister(lua_State *lua_state_ptr)
{
    lua_newtable(lua_state_ptr); /* the redis module table */

    lua_pushliteral(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_VERSION);
    lua_setfield(lua_state_ptr, -2, "_VERSION");
    lua_pushliteral(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_COPYRIGHT);
    lua_setfield(lua_state_ptr, -2, "_COPYRIGHT");
    lua_pushliteral(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_DESCRIPTION);
    lua_setfield(lua_state_ptr, -2, "_DESCRIPTION");

    sRegisterEnums(lua_state_ptr, sErrors);
    sRegisterEnums(lua_state_ptr, sReplyTypes);

    sPushNewConst(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_NIL_KEY,
                  strlen(UFSRVSCRIPTLIB_REDIS_NIL_KEY), REDIS_REPLY_NIL);
    lua_setfield(lua_state_ptr, -2, UFSRVSCRIPTLIB_REDIS_NIL_KEY);

    lua_newtable(lua_state_ptr); /* status */
    if (luaL_newmetatable(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_STATUS_MT)) {
        luaL_setfuncs(lua_state_ptr, sStatusMt, 0);
        lua_pushliteral(lua_state_ptr, UFSRVSCRIPTLIB_REDIS_STATUS_MT);
        lua_setfield(lua_state_ptr, -2, "__metatable");
    }
    lua_setmetatable(lua_state_ptr, -2);

    lua_getfield(lua_state_ptr, -1, "OK");
    lua_setfield(lua_state_ptr, -3, "OK");
    lua_getfield(lua_state_ptr, -1, "QUEUED");
    lua_setfield(lua_state_ptr, -3, "QUEUED");
    lua_getfield(lua_state_ptr, -1, "PONG");
    lua_setfield(lua_state_ptr, -3, "PONG");
    lua_setfield(lua_state_ptr, -2, "status");

    luaL_setfuncs(lua_state_ptr, sRedisModule, 0);

    lua_setglobal(lua_state_ptr, "redis");
}
