/**
 * @file ufsrvscriptlib_lua.c
 * @brief Generic, class-agnostic Lua scripting engine (see ufsrvscriptlib_lua.h).
 *
 * A minimal Lua command host. It creates a sandboxed lua_State, installs a
 * `ufsrv` helper table (call/pcall/error_reply/status_reply/log), and registers
 * the host's NULL-sentinel UfsrvScriptFunctionBinding array. Scripts are wrapped as
 * `function __ufsrv__script() ... end` and run under an error handler that
 * reports `source:line: message`.
 */
#include <ufsrvscriptlib/ufsrvscriptlib_lua.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define UFSRVSCRIPTLIB_SCRIPT_FN    "__ufsrv__script"
#define UFSRVSCRIPTLIB_ERR_HANDLER  "__ufsrv__err__handler"
#define UFSRVSCRIPTLIB_ERRBUF_LEN   512

struct UfsrvScriptLua {
    lua_State                *lua_state;
    const UfsrvScriptFunctionBinding *bindings; /* borrowed, static lifetime */
    int                       in_use;   /* reentrancy guard for call/pcall */
    char                      err_buf[UFSRVSCRIPTLIB_ERRBUF_LEN];
};

/* ---------------------------------------------------------------------------
 * Lua bootstrap snippets (adapted from Redis scripting.c, BSD-3).
 * ------------------------------------------------------------------------- */

/* strict.lua: forbid accidental creation/reading of undeclared globals from
 * script code (C-level access — what == "C" — is allowed). */
static const char strict_lua[] =
    "local dbg = debug\n"
    "local mt = {}\n"
    "setmetatable(_G, mt)\n"
    "mt.__newindex = function (t, n, v)\n"
    "  if dbg.getinfo(2) then\n"
    "    local w = dbg.getinfo(2, 'S').what\n"
    "    if w ~= 'main' and w ~= 'C' then\n"
    "      error(\"Script attempted to create global variable '\" .. tostring(n) .. \"'\", 2)\n"
    "    end\n"
    "  end\n"
    "  rawset(t, n, v)\n"
    "end\n"
    "mt.__index = function (t, n)\n"
    "  if dbg.getinfo(2) and dbg.getinfo(2, 'S').what ~= 'C' then\n"
    "    error(\"Script attempted to access undeclared global variable '\" .. tostring(n) .. \"'\", 2)\n"
    "  end\n"
    "  return rawget(t, n)\n"
    "end\n"
    "debug = nil\n";

/* Error handler: enrich a raised error with the script's source:line. The
 * `local dbg = debug` upvalue survives the strict.lua `debug = nil`. */
static const char err_handler_lua[] =
    "local dbg = debug\n"
    "function " UFSRVSCRIPTLIB_ERR_HANDLER "(err)\n"
    "  local i = dbg.getinfo(2, 'nSl')\n"
    "  if i and i.what == 'C' then\n"
    "    i = dbg.getinfo(3, 'nSl')\n"
    "  end\n"
    "  if i then\n"
    "    return i.source .. ':' .. i.currentline .. ': ' .. err\n"
    "  else\n"
    "    return err\n"
    "  end\n"
    "end\n";

/* ---------------------------------------------------------------------------
 * Small helpers.
 * ------------------------------------------------------------------------- */

static void
sSetErr(UfsrvScriptLua *lua_ptr, const char *msg_ptr)
{
    snprintf(lua_ptr->err_buf, sizeof(lua_ptr->err_buf), "%s",
             msg_ptr ? msg_ptr : "unknown error");
}

/* Push a { err = "msg" } table. */
static void
sPushErrorTable(lua_State *lua_state_ptr, const char *msg_ptr)
{
    lua_newtable(lua_state_ptr);
    lua_pushliteral(lua_state_ptr, "err");
    lua_pushstring(lua_state_ptr, msg_ptr);
    lua_settable(lua_state_ptr, -3);
}

/* Raise the error stored in the { err = "..." } table on top of the stack. */
static int
sRaiseErrorTable(lua_State *lua_state_ptr)
{
    lua_pushliteral(lua_state_ptr, "err");
    lua_gettable(lua_state_ptr, -2); /* stack: table, err_string */
    lua_remove(lua_state_ptr, -2);   /* stack: err_string */
    return lua_error(lua_state_ptr);
}

/* Push {err=...} then either raise it (call) or return it (pcall). */
static int
sScriptFail(lua_State *lua_state_ptr, int raise_error, const char *msg_ptr)
{
    sPushErrorTable(lua_state_ptr, msg_ptr);
    return raise_error ? sRaiseErrorTable(lua_state_ptr) : 1;
}

/* Compile + run a fixed bootstrap snippet; log (best-effort) on failure. */
static void
sRunBootstrapChunk(lua_State *lua_state_ptr, const char *code_ptr, size_t len,
                   const char *chunkname_ptr)
{
    if (luaL_loadbuffer(lua_state_ptr, code_ptr, len, chunkname_ptr) != LUA_OK) {
        fprintf(stderr, "ufsrvscriptlib: %s: %s\n", chunkname_ptr,
                lua_tostring(lua_state_ptr, -1));
        lua_pop(lua_state_ptr, 1);
        return;
    }
    if (lua_pcall(lua_state_ptr, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "ufsrvscriptlib: %s: %s\n", chunkname_ptr,
                lua_tostring(lua_state_ptr, -1));
        lua_pop(lua_state_ptr, 1);
    }
}

static void
sInstallErrHandler(lua_State *lua_state_ptr)
{
    sRunBootstrapChunk(lua_state_ptr, err_handler_lua,
                       sizeof(err_handler_lua) - 1, "@err_handler");
}

static void
sInstallStrictGlobals(lua_State *lua_state_ptr)
{
    sRunBootstrapChunk(lua_state_ptr, strict_lua, sizeof(strict_lua) - 1,
                       "@strict_lua");
}

/* Open only the whitelisted standard libraries. */
static void
sLoadWhitelistedLibs(lua_State *lua_state_ptr, const UfsrvScriptLuaOptions *opts_ptr)
{
    luaL_requiref(lua_state_ptr, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(lua_state_ptr, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(lua_state_ptr, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(lua_state_ptr, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(lua_state_ptr, LUA_DBLIBNAME, luaopen_debug, 1);
    if (opts_ptr && opts_ptr->is_os_allowed)
        luaL_requiref(lua_state_ptr, LUA_OSLIBNAME, luaopen_os, 1);
    if (opts_ptr && opts_ptr->is_package_allowed)
        luaL_requiref(lua_state_ptr, LUA_LOADLIBNAME, luaopen_package, 1);
    lua_settop(lua_state_ptr, 0); /* drop the library tables requiref left */
}

/* Remove functions that could reach the filesystem through the base library. */
static void
sRemoveUnsupported(lua_State *lua_state_ptr)
{
    lua_pushnil(lua_state_ptr);
    lua_setglobal(lua_state_ptr, "loadfile");
    lua_pushnil(lua_state_ptr);
    lua_setglobal(lua_state_ptr, "dofile");
}

/* ---------------------------------------------------------------------------
 * The `ufsrv` helper table and command dispatch.
 * ------------------------------------------------------------------------- */

static int sDispatch(lua_State *lua_state_ptr, int raise_error);

static int
sUfsrvCall(lua_State *lua_state_ptr)
{
    return sDispatch(lua_state_ptr, 1);
}

static int
sUfsrvPcall(lua_State *lua_state_ptr)
{
    return sDispatch(lua_state_ptr, 0);
}

/* look up the named binding and invoke it. The
 * handle is carried as the single upvalue (lightuserdata). */
static int
sDispatch(lua_State *lua_state_ptr, int raise_error)
{
    UfsrvScriptLua *lua_ptr = (UfsrvScriptLua *)lua_touserdata(lua_state_ptr,
                                                               lua_upvalueindex(1));
    const UfsrvScriptFunctionBinding *binding;
    const char *name;
    int argc, nargs;

    if (!lua_ptr)
        return sScriptFail(lua_state_ptr, raise_error, "ufsrv.call(): missing engine context");

    argc = lua_gettop(lua_state_ptr);
    if (argc < 1)
        return sScriptFail(lua_state_ptr, raise_error, "ufsrv.call() requires a command name");

    name = lua_tostring(lua_state_ptr, 1);
    if (!name)
        return sScriptFail(lua_state_ptr, raise_error, "ufsrv.call() command name must be a string");

    if (lua_ptr->in_use)
        return sScriptFail(lua_state_ptr, raise_error, "ufsrv.call() recursive call detected");
    lua_ptr->in_use = 1;

    binding = lua_ptr->bindings;
    while (binding && binding->name && strcmp(binding->name, name) != 0)
        binding++;
    if (!binding || !binding->name) {
        lua_ptr->in_use = 0;
        return sScriptFail(lua_state_ptr, raise_error, "unknown ufsrv command");
    }

    nargs = argc - 1;
    if (binding->arity != 0) {
        int bad = (binding->arity > 0 && nargs != binding->arity) ||
                  (binding->arity < 0 && nargs < -binding->arity);
        if (bad) {
            lua_ptr->in_use = 0;
            return sScriptFail(lua_state_ptr, raise_error, "wrong number of arguments to ufsrv command");
        }
    }

    /* Invoke the binding with args [2..argc]. */
    lua_pushcfunction(lua_state_ptr, binding->func); /* stack: name, args..., func */
    lua_insert(lua_state_ptr, 1);                    /* stack: func, name, args... */
    lua_remove(lua_state_ptr, 2);                    /* stack: func, args... */
    if (lua_pcall(lua_state_ptr, nargs, 1, 0) != LUA_OK) {
        const char *msg = lua_tostring(lua_state_ptr, -1);
        lua_ptr->in_use = 0;
        lua_pop(lua_state_ptr, 1);
        return sScriptFail(lua_state_ptr, raise_error, msg ? msg : "ufsrv command raised an error");
    }
    lua_ptr->in_use = 0;

    /* Exactly one result on top. If raising and it is {err=<string>}, raise. */
    if (raise_error && lua_istable(lua_state_ptr, -1)) {
        lua_pushliteral(lua_state_ptr, "err");
        lua_gettable(lua_state_ptr, -2);
        if (lua_type(lua_state_ptr, -1) == LUA_TSTRING) {
            lua_remove(lua_state_ptr, -2); /* stack: err_string */
            return lua_error(lua_state_ptr);
        }
        lua_pop(lua_state_ptr, 1); /* pop a non-string err field */
    }
    return 1;
}

static int
sUfsrvErrorReply(lua_State *lua_state_ptr)
{
    if (lua_gettop(lua_state_ptr) != 1 || lua_type(lua_state_ptr, 1) != LUA_TSTRING)
        return luaL_error(lua_state_ptr, "error_reply() expects one string");
    sPushErrorTable(lua_state_ptr, lua_tostring(lua_state_ptr, 1));
    return 1;
}

static int
sUfsrvStatusReply(lua_State *lua_state_ptr)
{
    if (lua_gettop(lua_state_ptr) != 1 || lua_type(lua_state_ptr, 1) != LUA_TSTRING)
        return luaL_error(lua_state_ptr, "status_reply() expects one string");
    lua_newtable(lua_state_ptr);
    lua_pushliteral(lua_state_ptr, "ok");
    lua_pushvalue(lua_state_ptr, 1);
    lua_settable(lua_state_ptr, -3);
    return 1;
}

static int
sUfsrvLog(lua_State *lua_state_ptr)
{
    int i, argc = lua_gettop(lua_state_ptr);

    for (i = 1; i <= argc; i++) {
        size_t len;
        const char *s = lua_tolstring(lua_state_ptr, i, &len);
        if (s)
            fprintf(stderr, "%s%s", i > 1 ? " " : "", s);
        else
            fprintf(stderr, "%s%s", i > 1 ? " " : "", luaL_typename(lua_state_ptr, i));
    }
    fputc('\n', stderr);
    return 0;
}

/* Build the `ufsrv` table (helpers + host bindings) and set it global. */
static void
sRegisterBindings(lua_State *lua_state_ptr, UfsrvScriptLua *lua_ptr)
{
    const UfsrvScriptFunctionBinding *binding;

    lua_newtable(lua_state_ptr);

    lua_pushlightuserdata(lua_state_ptr, lua_ptr);
    lua_pushcclosure(lua_state_ptr, sUfsrvCall, 1);
    lua_setfield(lua_state_ptr, -2, "call");

    lua_pushlightuserdata(lua_state_ptr, lua_ptr);
    lua_pushcclosure(lua_state_ptr, sUfsrvPcall, 1);
    lua_setfield(lua_state_ptr, -2, "pcall");

    lua_pushcfunction(lua_state_ptr, sUfsrvErrorReply);
    lua_setfield(lua_state_ptr, -2, "error_reply");

    lua_pushcfunction(lua_state_ptr, sUfsrvStatusReply);
    lua_setfield(lua_state_ptr, -2, "status_reply");

    lua_pushcfunction(lua_state_ptr, sUfsrvLog);
    lua_setfield(lua_state_ptr, -2, "log");

    for (binding = lua_ptr->bindings; binding && binding->name; binding++) {
        lua_pushcfunction(lua_state_ptr, binding->func);
        lua_setfield(lua_state_ptr, -2, binding->name);
    }

    lua_setglobal(lua_state_ptr, UFSRVSCRIPTLIB_GLOBAL);
}

/* Set constant_ptr->name (a dotted path) to its scalar value, creating any
 * intermediate tables so e.g. "ufsrvcli.version" lands as ufsrvcli.version. */
static void
sRegisterConstant(lua_State *lua_state_ptr, const UfsrvScriptConstantBinding *constant_ptr)
{
    const char *p = constant_ptr->name;
    const char *dot;
    int container;

    lua_pushglobaltable(lua_state_ptr);    /* container */
    container = lua_gettop(lua_state_ptr); /* absolute index (stable) */

    while ((dot = strchr(p, '.')) != NULL) {
        size_t seg_len = (size_t)(dot - p);

        lua_pushlstring(lua_state_ptr, p, seg_len); /* key */
        lua_gettable(lua_state_ptr, container);     /* child = container[key] */
        if (!lua_istable(lua_state_ptr, -1)) {
            lua_pop(lua_state_ptr, 1);
            lua_newtable(lua_state_ptr);                 /* sub-table */
            lua_pushlstring(lua_state_ptr, p, seg_len);  /* key */
            lua_pushvalue(lua_state_ptr, -2);            /* dup sub-table */
            lua_settable(lua_state_ptr, container);      /* container[key] = sub-table */
        }
        lua_replace(lua_state_ptr, container); /* container = child */
        p = dot + 1;
    }

    lua_pushlstring(lua_state_ptr, p, strlen(p)); /* key */
    switch (constant_ptr->type) {
        case UFSRVSCRIPTLIB_CONSTANT_TYPE_STRING:
            lua_pushstring(lua_state_ptr, constant_ptr->string_value);
            break;
        case UFSRVSCRIPTLIB_CONSTANT_TYPE_INTEGER:
            lua_pushinteger(lua_state_ptr, constant_ptr->integer_value);
            break;
        case UFSRVSCRIPTLIB_CONSTANT_TYPE_BOOLEAN:
            lua_pushboolean(lua_state_ptr, constant_ptr->boolean_value);
            break;
        default:
            lua_pushnil(lua_state_ptr);
            break;
    }
    lua_settable(lua_state_ptr, container); /* container[key] = value */
    lua_pop(lua_state_ptr, 1);
}

static void
sRegisterConstants(lua_State *lua_state_ptr, const UfsrvScriptConstantBinding *constants_ptr)
{
    const UfsrvScriptConstantBinding *constant_ptr;

    for (constant_ptr = constants_ptr; constant_ptr && constant_ptr->name; constant_ptr++)
        sRegisterConstant(lua_state_ptr, constant_ptr);
}

/* ---------------------------------------------------------------------------
 * Script loading and running.
 * ------------------------------------------------------------------------- */

/* Read all of a stream into a NUL-terminated growable buffer. */
static char *
sReadAll(FILE *fp_ptr, size_t *len_out_ptr)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);

    if (!buf)
        return NULL;
    for (;;) {
        size_t n;
        if (len + 1 >= cap) {
            char *nb;
            cap *= 2;
            nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
        n = fread(buf + len, 1, cap - len - 1, fp_ptr);
        if (n == 0) {
            if (ferror(fp_ptr)) {
                free(buf);
                return NULL;
            }
            break;
        }
        len += n;
    }
    buf[len] = '\0';
    *len_out_ptr = len;
    return buf;
}

/* Allocate a chunkname of the form "@" + name (Lua's filename-source marker). */
static char *
sAtChunkname(const char *name_ptr)
{
    char *cn = malloc(strlen(name_ptr) + 2);
    if (!cn)
        return NULL;
    cn[0] = '@';
    strcpy(cn + 1, name_ptr);
    return cn;
}

/* Wrap a script body as `function __ufsrv__script() ... end`, compile it, and
 * define the global function. chunkname_ptr is borrowed for the compile only. */
static int
sLoadScriptBody(UfsrvScriptLua *lua_ptr, const char *body_ptr, size_t len,
                const char *chunkname_ptr)
{
    lua_State *lua_state = lua_ptr->lua_state;
    size_t prefix_len = strlen("function " UFSRVSCRIPTLIB_SCRIPT_FN "()\n");
    size_t suffix_len = strlen("\nend");
    size_t total = prefix_len + len + suffix_len;
    char *funcdef = malloc(total + 1);
    char *p;

    if (!funcdef) {
        sSetErr(lua_ptr, "out of memory");
        return -1;
    }
    p = funcdef;
    memcpy(p, "function " UFSRVSCRIPTLIB_SCRIPT_FN "()\n", prefix_len);
    p += prefix_len;
    memcpy(p, body_ptr, len);
    p += len;
    memcpy(p, "\nend", suffix_len + 1); /* includes the NUL terminator */

    if (luaL_loadbuffer(lua_state, funcdef, total, chunkname_ptr) != LUA_OK) {
        sSetErr(lua_ptr, lua_tostring(lua_state, -1));
        lua_pop(lua_state, 1);
        free(funcdef);
        return -1;
    }
    free(funcdef); /* the chunk is compiled; the source buffer is no longer needed */

    if (lua_pcall(lua_state, 0, 0, 0) != LUA_OK) {
        sSetErr(lua_ptr, lua_tostring(lua_state, -1));
        lua_pop(lua_state, 1);
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Public API.
 * ------------------------------------------------------------------------- */

/* Default sandbox policy applied when the caller passes a NULL opts. Mirrors
 * the defaults documented in ufsrvscriptlib_lua.h (strict globals on,
 * os/package off). */
static const UfsrvScriptLuaOptions sDefaultOptions = { true, false, false };

UfsrvScriptLua *
UfsrvScriptBootstrap(const UfsrvScriptFunctionBinding *bindings_ptr,
                     const UfsrvScriptConstantBinding *constants_ptr,
                     const UfsrvScriptLuaOptions *opts_ptr)
{
    UfsrvScriptLua *lua_ptr;
    lua_State *lua_state;

    if (!opts_ptr)
        opts_ptr = &sDefaultOptions;

    lua_ptr = calloc(1, sizeof(*lua_ptr));
    if (!lua_ptr)
        return NULL;

    lua_state = luaL_newstate();
    if (!lua_state) {
        free(lua_ptr);
        return NULL;
    }
    lua_ptr->lua_state = lua_state;
    lua_ptr->bindings = bindings_ptr;

    sLoadWhitelistedLibs(lua_state, opts_ptr);
    sRemoveUnsupported(lua_state);
    sInstallErrHandler(lua_state);
    sRegisterBindings(lua_state, lua_ptr);
    sRegisterConstants(lua_state, constants_ptr);

    if (opts_ptr && opts_ptr->is_strict_globals)
        sInstallStrictGlobals(lua_state);

    return lua_ptr;
}

void
UfsrvScriptLuaClose(UfsrvScriptLua *lua_ptr)
{
    if (!lua_ptr)
        return;
    if (lua_ptr->lua_state)
        lua_close(lua_ptr->lua_state);
    free(lua_ptr);
}

int
UfsrvScriptLuaLoadFile(UfsrvScriptLua *lua_ptr, const char *path_ptr)
{
    FILE *fp;
    char *body = NULL, *chunkname = NULL;
    size_t len = 0;
    int rc = -1;

    if (!lua_ptr || !lua_ptr->lua_state || !path_ptr)
        return -1;

    if (strcmp(path_ptr, "-") == 0) {
        fp = stdin;
        chunkname = sAtChunkname("stdin");
    } else {
        fp = fopen(path_ptr, "rb");
        if (!fp) {
            sSetErr(lua_ptr, strerror(errno));
            return -1;
        }
        chunkname = sAtChunkname(path_ptr);
    }
    if (!chunkname) {
        if (fp != stdin)
            fclose(fp);
        sSetErr(lua_ptr, "out of memory");
        return -1;
    }

    body = sReadAll(fp, &len);
    if (fp != stdin)
        fclose(fp);
    if (!body) {
        sSetErr(lua_ptr, "cannot read script source");
        free(chunkname);
        return -1;
    }

    rc = sLoadScriptBody(lua_ptr, body, len, chunkname);
    free(body);
    free(chunkname);
    return rc;
}

int
UfsrvScriptLuaLoadBuffer(UfsrvScriptLua *lua_ptr, const char *buf_ptr, size_t len,
                         const char *chunkname_ptr)
{
    if (!lua_ptr || !lua_ptr->lua_state || !buf_ptr)
        return -1;
    return sLoadScriptBody(lua_ptr, buf_ptr, len, chunkname_ptr ? chunkname_ptr : "@script");
}

int
UfsrvScriptLuaRun(UfsrvScriptLua *lua_ptr)
{
    lua_State *lua_state;
    int err;

    if (!lua_ptr || !lua_ptr->lua_state)
        return -1;
    lua_state = lua_ptr->lua_state;

    lua_getglobal(lua_state, UFSRVSCRIPTLIB_ERR_HANDLER); /* stack: handler */
    lua_getglobal(lua_state, UFSRVSCRIPTLIB_SCRIPT_FN);   /* stack: handler, fn */
    if (lua_isnil(lua_state, -1)) {
        sSetErr(lua_ptr, "no script loaded");
        lua_pop(lua_state, 2);
        return -1;
    }

    err = lua_pcall(lua_state, 0, 1, -2); /* fn at -1, handler at -2 */
    if (err != LUA_OK) {
        const char *msg = lua_tostring(lua_state, -1);
        sSetErr(lua_ptr, msg ? msg : "script error");
        lua_pop(lua_state, 2); /* error message + handler */
        return -1;
    }
    lua_remove(lua_state, -2); /* drop the handler, leaving the single result */
    return 0;
}

lua_State *
UfsrvScriptLuaState(UfsrvScriptLua *lua_ptr)
{
    return lua_ptr ? lua_ptr->lua_state : NULL;
}

const char *
UfsrvScriptLuaErrorString(UfsrvScriptLua *lua_ptr)
{
    return (lua_ptr && lua_ptr->err_buf[0]) ? lua_ptr->err_buf : "";
}

void
UfsrvScriptLuaPushError(lua_State *lua_state_ptr, const char *fmt_ptr, ...)
{
    va_list ap;
    char msg[256];

    if (!lua_state_ptr)
        return;
    va_start(ap, fmt_ptr);
    vsnprintf(msg, sizeof(msg), fmt_ptr ? fmt_ptr : "error", ap);
    va_end(ap);
    sPushErrorTable(lua_state_ptr, msg);
}

void
UfsrvScriptLuaPushStatus(lua_State *lua_state_ptr, const char *msg_ptr)
{
    if (!lua_state_ptr)
        return;
    lua_newtable(lua_state_ptr);
    lua_pushliteral(lua_state_ptr, "ok");
    lua_pushstring(lua_state_ptr, msg_ptr ? msg_ptr : "");
    lua_settable(lua_state_ptr, -3);
}
