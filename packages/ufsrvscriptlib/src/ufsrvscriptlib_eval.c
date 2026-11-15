/**
 * @file ufsrvscriptlib_eval.c
 * @brief One-shot and persistent Lua evaluator over the ufsrvscriptlib engine.
 *
 * Wraps the lifecycle API (UfsrvScriptBootstrap / LoadBuffer / Run / Close) as a
 * single "eval a string, get a BufferDescriptor" call, and marshals the result
 * with the same JSON-ish convention as the server console EVAL (nil → "null",
 * boolean → "true"/"false", else lua_tostring). See ufsrvscriptlib_eval.h.
 */
#include <ufsrvscriptlib/ufsrvscriptlib_eval.h>

#include <stdlib.h>
#include <string.h>

/* Prepare @p buffer for a fresh write. When NULL, allocate and initialise a new
 * descriptor (caller frees it, and its backing store, afterwards). When non-NULL
 * it must already be BufferDescriptorInit'd and is truncated in place. Returns
 * the descriptor, or NULL on allocation failure. */
static BufferDescriptor *
sPrepBuffer(BufferDescriptor *buffer)
{
    if (!buffer) {
        buffer = calloc(1, sizeof(*buffer));
        if (!buffer)
            return NULL;
        BufferDescriptorInit(buffer, 256);
        if (!buffer->data) {
            free(buffer);
            return NULL;
        }
    } else {
        buffer->size = 0;
        if (buffer->data)
            buffer->data[0] = '\0';
    }
    return buffer;
}

/* Write a {"error":"…"} object into @p buffer, replacing any prior content.
 * The message is not JSON-escaped — matches the server console EVAL. */
static BufferDescriptor *
sEmitError(BufferDescriptor *buffer, const char *msg)
{
    buffer = sPrepBuffer(buffer);
    if (!buffer)
        return NULL;
    BufferDescriptorAppendFormatted(buffer, "{\"error\":\"%s\"}",
                                    msg ? msg : "unknown error");
    return buffer;
}

/* Marshal the Lua value at the top of the stack into @p buffer (JSON-ish,
 * matching the server console EVAL). */
static BufferDescriptor *
sMarshalResult(lua_State *L, BufferDescriptor *buffer)
{
    const char *s;

    buffer = sPrepBuffer(buffer);
    if (!buffer)
        return NULL;

    if (lua_isnil(L, -1)) {
        BufferDescriptorAppendFormatted(buffer, "null");
    } else if (lua_isboolean(L, -1)) {
        BufferDescriptorAppendFormatted(buffer, "%s",
                                        lua_toboolean(L, -1) ? "true" : "false");
    } else {
        s = lua_tostring(L, -1);
        BufferDescriptorAppendFormatted(buffer, "%s", s ? s : "");
    }
    return buffer;
}

/* Load @p script into @p lua and run it, marshalling the single return value
 * into @p buffer. The VM's stack is left clean on return (so a persistent VM
 * does not accumulate result frames). */
static BufferDescriptor *
sEvalRun(UfsrvScriptLua *lua, const char *script, BufferDescriptor *buffer)
{
    lua_State *L;
    BufferDescriptor *out;

    if (UfsrvScriptLuaLoadBuffer(lua, script, strlen(script), "@eval") != 0)
        return sEmitError(buffer, UfsrvScriptLuaErrorString(lua));

    if (UfsrvScriptLuaRun(lua) != 0)
        return sEmitError(buffer, UfsrvScriptLuaErrorString(lua));

    L = UfsrvScriptLuaState(lua);
    out = sMarshalResult(L, buffer);
    lua_settop(L, 0); /* leave the reusable VM's stack clean */
    return out;
}

PUBLIC_API BufferDescriptor *
UfsrvScriptEval(const char *script, BufferDescriptor *buffer)
{
    UfsrvScriptLua *lua;
    BufferDescriptor *out;

    if (!script)
        return sEmitError(buffer, "no script");

    lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    if (!lua)
        return sEmitError(buffer, "failed to initialise Lua engine");

    out = sEvalRun(lua, script, buffer);
    UfsrvScriptLuaClose(lua);
    return out;
}

PUBLIC_API UfsrvScriptLua *
UfsrvScriptEvalOpen(const UfsrvScriptLuaOptions *opts)
{
    return UfsrvScriptBootstrap(NULL, NULL, opts);
}

PUBLIC_API BufferDescriptor *
UfsrvScriptEvalRun(UfsrvScriptLua *lua, const char *script, BufferDescriptor *buffer)
{
    if (!lua)
        return sEmitError(buffer, "no engine");
    if (!script)
        return sEmitError(buffer, "no script");
    return sEvalRun(lua, script, buffer);
}
