/**
 * @file ufsrvscriptlib_lua_test.cpp
 * @brief Adversarial gtest suite for the ufsrvscriptlib Lua engine.
 *
 * These tests are written to break the engine, not to bless its current
 * behaviour: NULL and malformed inputs, sandbox escape attempts, the
 * call/pcall error contract, dispatch edge cases (unknown commands, arity,
 * recursion), and lifecycle misuse (run-before-load, double-load, re-run).
 */
#include <gtest/gtest.h>

extern "C" {
#include <ufsrvscriptlib/ufsrvscriptlib_lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Binding implementations used by the tests (all intentionally referenced by
 * the kBindings array so none trigger unused-function warnings).
 * ------------------------------------------------------------------------- */

static int
sDouble(lua_State *lua_state)
{
    lua_pushnumber(lua_state, 2.0 * luaL_checknumber(lua_state, 1));
    return 1;
}

static int
sFail(lua_State *lua_state)
{
    UfsrvScriptLuaPushError(lua_state, "deliberate failure");
    return 1;
}

/* Raises directly — the engine must catch it for pcall and propagate for call. */
static int
sRaise(lua_State *lua_state)
{
    return luaL_error(lua_state, "binding raised");
}

/* Returns zero results — the script must observe nil. */
static int
sNilret(lua_State *lua_state)
{
    (void)lua_state;
    return 0;
}

/* Pushes a { err = 42 } table whose err field is NOT a string. */
static int
sBadErr(lua_State *lua_state)
{
    lua_newtable(lua_state);
    lua_pushliteral(lua_state, "err");
    lua_pushinteger(lua_state, 42);
    lua_settable(lua_state, -3);
    return 1;
}

/* Re-enters the dispatcher through a nested ufsrvscript.pcall — must be caught by the
 * reentrancy guard, not loop forever or corrupt the stack. */
static int
sRecurse(lua_State *lua_state)
{
    lua_getglobal(lua_state, UFSRVSCRIPTLIB_GLOBAL);
    lua_getfield(lua_state, -1, "pcall");
    lua_pushstring(lua_state, "double");
    lua_pushinteger(lua_state, 5);
    lua_remove(lua_state, -4);       /* drop the ufsrv table: [pcall, "double", 5] */
    lua_pcall(lua_state, 2, 1, 0);   /* nested pcall → { err = "recursive call detected" } */
    return 1;
}

static const UfsrvScriptFunctionBinding kBindings[] = {
    { "double",  1, sDouble },
    { "fail",    0, sFail },
    { "raise",   0, sRaise },
    { "nilret",  0, sNilret },
    { "bad_err", 0, sBadErr },
    { "recurse", 0, sRecurse },
    { NULL, 0, NULL }
};

/* Load a script buffer and run it, discarding the result (keeps the stack
 * clean for assert-style scripts). Returns 0 on success, -1 on any failure. */
static int
sLoadRun(UfsrvScriptLua *lua_ptr, const char *src)
{
    if (UfsrvScriptLuaLoadBuffer(lua_ptr, src, strlen(src), "@test") != 0)
        return -1;
    if (UfsrvScriptLuaRun(lua_ptr) != 0)
        return -1;
    lua_pop(UfsrvScriptLuaState(lua_ptr), 1);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Bootstrap / lifecycle
 * ------------------------------------------------------------------------- */

TEST(UfsrvScriptBootstrap, NullArgsProduceUsableHandle)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_NE(UfsrvScriptLuaState(lua), nullptr);
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptBootstrap, SentinelOnlyBindings)
{
    const UfsrvScriptFunctionBinding only_sentinel[] = { { NULL, 0, NULL } };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(only_sentinel, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaClose, NullIsNoop)
{
    UfsrvScriptLuaClose(NULL); /* must not crash */
}

TEST(UfsrvScriptLuaRun, RunBeforeLoad)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(UfsrvScriptLuaRun(lua), -1);
    EXPECT_STREQ(UfsrvScriptLuaErrorString(lua), "no script loaded");
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaRun, LoadTwiceReplacesScript)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    ASSERT_EQ(UfsrvScriptLuaLoadBuffer(lua, "return 1", 8, "@a"), 0);
    ASSERT_EQ(UfsrvScriptLuaLoadBuffer(lua, "return 2", 8, "@b"), 0);
    ASSERT_EQ(UfsrvScriptLuaRun(lua), 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(UfsrvScriptLuaState(lua), -1), 2.0);
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaRun, ReRunSameScript)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    ASSERT_EQ(UfsrvScriptLuaLoadBuffer(lua, "return 7", 8, "@c"), 0);
    ASSERT_EQ(UfsrvScriptLuaRun(lua), 0);
    lua_pop(UfsrvScriptLuaState(lua), 1);
    ASSERT_EQ(UfsrvScriptLuaRun(lua), 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(UfsrvScriptLuaState(lua), -1), 7.0);
    UfsrvScriptLuaClose(lua);
}

/* ---------------------------------------------------------------------------
 * NULL / malformed inputs (crash resistance)
 * ------------------------------------------------------------------------- */

TEST(UfsrvScriptLuaLoad, NullHandle)
{
    EXPECT_EQ(UfsrvScriptLuaLoadBuffer(NULL, "return 1", 8, "@x"), -1);
    EXPECT_EQ(UfsrvScriptLuaLoadFile(NULL, "x.lua"), -1);
    EXPECT_EQ(UfsrvScriptLuaRun(NULL), -1);
    EXPECT_EQ(UfsrvScriptLuaState(NULL), nullptr);
}

TEST(UfsrvScriptLuaLoad, NullBuffer)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(UfsrvScriptLuaLoadBuffer(lua, NULL, 0, "@x"), -1);
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaLoad, NullPath)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(UfsrvScriptLuaLoadFile(lua, NULL), -1);
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaLoad, EmptyAndWhitespaceScript)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, ""), 0);
    EXPECT_EQ(sLoadRun(lua, "  \n\t "), 0);
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaLoad, SyntaxError)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(UfsrvScriptLuaLoadBuffer(lua, "this is not lua !!!", 19, "@bad"), -1);
    EXPECT_STRNE(UfsrvScriptLuaErrorString(lua), "");
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaLoad, MissingFile)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(UfsrvScriptLuaLoadFile(lua, "/definitely/not/a/real/file.lua"), -1);
    EXPECT_STRNE(UfsrvScriptLuaErrorString(lua), "");
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaLoad, LoadFileHappyPath)
{
    /* Write a real .lua script to a temp file and load it via LoadFile — the
     * only path that exercises fopen + sReadAll + sAtChunkname end-to-end. */
    char tmpl[] = "/tmp/ufsrvscriptlib_test_XXXXXX";
    int fd = mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    const char *script = "return ufsrvscript.call('double', 21)";
    ASSERT_EQ(write(fd, script, strlen(script)), (ssize_t)strlen(script));
    close(fd);

    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    ASSERT_EQ(UfsrvScriptLuaLoadFile(lua, tmpl), 0);
    ASSERT_EQ(UfsrvScriptLuaRun(lua), 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(UfsrvScriptLuaState(lua), -1), 42.0);
    UfsrvScriptLuaClose(lua);
    remove(tmpl);
}

TEST(UfsrvScriptLuaLoad, BufferWithoutNulTerminator)
{
    /* A buffer that is not NUL-terminated, with an exact length — the engine
     * must honour len rather than strlen. */
    const char src[] = { 'r', 'e', 't', 'u', 'r', 'n', ' ', '4', '2' };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    ASSERT_EQ(UfsrvScriptLuaLoadBuffer(lua, src, sizeof(src), "@nonul"), 0);
    ASSERT_EQ(UfsrvScriptLuaRun(lua), 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(UfsrvScriptLuaState(lua), -1), 42.0);
    UfsrvScriptLuaClose(lua);
}

TEST(UfsrvScriptLuaLoad, LargeScriptDoesNotCrash)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    std::string big = "local t = {}";
    for (int i = 1; i <= 10000; i++)
        big += "; t[" + std::to_string(i) + "] = " + std::to_string(i);
    big += "; return #t";
    ASSERT_EQ(UfsrvScriptLuaLoadBuffer(lua, big.c_str(), big.size(), "@big"), 0);
    ASSERT_EQ(UfsrvScriptLuaRun(lua), 0);
    EXPECT_DOUBLE_EQ(lua_tonumber(UfsrvScriptLuaState(lua), -1), 10000.0);
    UfsrvScriptLuaClose(lua);
}

/* ---------------------------------------------------------------------------
 * Sandbox
 * ------------------------------------------------------------------------- */

TEST(Sandbox, LoadfileAndDofileArePruned)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    lua_State *L = UfsrvScriptLuaState(lua);
    lua_getglobal(L, "loadfile");
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    lua_getglobal(L, "dofile");
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    UfsrvScriptLuaClose(lua);
}

TEST(Sandbox, OsIoPackageAbsentByDefault)
{
    UfsrvScriptLuaOptions opts = { true, false, false };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, &opts);
    ASSERT_NE(lua, nullptr);
    lua_State *L = UfsrvScriptLuaState(lua);
    lua_getglobal(L, "os");
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    lua_getglobal(L, "io");
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    lua_getglobal(L, "package");
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    UfsrvScriptLuaClose(lua);
}

TEST(Sandbox, AllowOsAndPackageStillPrunesLoadfile)
{
    UfsrvScriptLuaOptions opts = { true, true, true };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, &opts);
    ASSERT_NE(lua, nullptr);
    lua_State *L = UfsrvScriptLuaState(lua);
    lua_getglobal(L, "os");
    EXPECT_EQ(lua_type(L, -1), LUA_TTABLE);
    lua_pop(L, 1);
    lua_getglobal(L, "package");
    EXPECT_EQ(lua_type(L, -1), LUA_TTABLE);
    lua_pop(L, 1);
    lua_getglobal(L, "loadfile");
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    lua_getglobal(L, "dofile");
    EXPECT_EQ(lua_type(L, -1), LUA_TNIL);
    lua_pop(L, 1);
    UfsrvScriptLuaClose(lua);
}

TEST(Sandbox, StrictGlobalsRejectAssignment)
{
    UfsrvScriptLuaOptions opts = { true, false, false };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, &opts);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "leaky = 1"), -1);
    EXPECT_NE(std::string(UfsrvScriptLuaErrorString(lua)).find("global"),
              std::string::npos);
    UfsrvScriptLuaClose(lua);
}

TEST(Sandbox, StrictGlobalsRejectReadOfUndeclared)
{
    UfsrvScriptLuaOptions opts = { true, false, false };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, &opts);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "local x = undeclared_global"), -1);
    UfsrvScriptLuaClose(lua);
}

TEST(Sandbox, StrictGlobalsOffAllowsAssignment)
{
    UfsrvScriptLuaOptions opts = { false, false, false };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, &opts);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "leaky = 1"), 0);
    UfsrvScriptLuaClose(lua);
}

/* NULL opts must resolve to the documented defaults (strict globals on), not
 * silently disable the guard. */
TEST(Sandbox, NullOptsEnableStrictGlobals)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "leaky = 1"), -1);
    EXPECT_NE(std::string(UfsrvScriptLuaErrorString(lua)).find("global"),
              std::string::npos);
    UfsrvScriptLuaClose(lua);
}

/* ---------------------------------------------------------------------------
 * Constants (read-only globals; dotted names build nested tables)
 * ------------------------------------------------------------------------- */

TEST(Constant, DottedPathBuildsNestedTable)
{
    const UfsrvScriptConstantBinding constants[] = {
        { "ufsrvcli.version", UFSRVSCRIPTLIB_CONSTANT_TYPE_STRING, "0.1.0", 0, 0 },
        { "ufsrvcli.port",    UFSRVSCRIPTLIB_CONSTANT_TYPE_INTEGER, NULL, 19701, 0 },
        { "ufsrvcli.tls",     UFSRVSCRIPTLIB_CONSTANT_TYPE_BOOLEAN, NULL, 0, 1 },
        { NULL, UFSRVSCRIPTLIB_CONSTANT_TYPE_NONE, NULL, 0, 0 }
    };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, constants, NULL);
    ASSERT_NE(lua, nullptr);
    lua_State *L = UfsrvScriptLuaState(lua);

    lua_getglobal(L, "ufsrvcli");
    ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);

    lua_getfield(L, -1, "version");
    EXPECT_STREQ(lua_tostring(L, -1), "0.1.0");
    lua_pop(L, 1);

    lua_getfield(L, -1, "port");
    EXPECT_DOUBLE_EQ(lua_tonumber(L, -1), 19701.0);
    lua_pop(L, 1);

    lua_getfield(L, -1, "tls");
    EXPECT_EQ(lua_toboolean(L, -1), 1);
    lua_pop(L, 2);

    UfsrvScriptLuaClose(lua);
}

TEST(Constant, FlatNameIsGlobal)
{
    const UfsrvScriptConstantBinding constants[] = {
        { "answer", UFSRVSCRIPTLIB_CONSTANT_TYPE_INTEGER, NULL, 42, 0 },
        { NULL, UFSRVSCRIPTLIB_CONSTANT_TYPE_NONE, NULL, 0, 0 }
    };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, constants, NULL);
    ASSERT_NE(lua, nullptr);
    lua_State *L = UfsrvScriptLuaState(lua);

    lua_getglobal(L, "answer");
    EXPECT_DOUBLE_EQ(lua_tonumber(L, -1), 42.0);
    lua_pop(L, 1);

    UfsrvScriptLuaClose(lua);
}

/* Two-level nesting, with the second constant merging into the table built by
 * the first — exercises the lua_replace "container = child" loop more than once
 * and descending into an existing sub-table. */
TEST(Constant, DeepNestingAndMerge)
{
    const UfsrvScriptConstantBinding constants[] = {
        { "a.b.c", UFSRVSCRIPTLIB_CONSTANT_TYPE_STRING, "deep", 0, 0 },
        { "a.b.d", UFSRVSCRIPTLIB_CONSTANT_TYPE_INTEGER, NULL, 7, 0 },
        { NULL, UFSRVSCRIPTLIB_CONSTANT_TYPE_NONE, NULL, 0, 0 }
    };
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, constants, NULL);
    ASSERT_NE(lua, nullptr);
    lua_State *L = UfsrvScriptLuaState(lua);

    lua_getglobal(L, "a");
    ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);

    lua_getfield(L, -1, "b");
    ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);

    lua_getfield(L, -1, "c");
    EXPECT_STREQ(lua_tostring(L, -1), "deep");
    lua_pop(L, 1);

    lua_getfield(L, -1, "d");
    EXPECT_DOUBLE_EQ(lua_tonumber(L, -1), 7.0);
    lua_pop(L, 2);

    UfsrvScriptLuaClose(lua);
}

/* ---------------------------------------------------------------------------
 * Error contract (call raises, pcall returns {err})
 * ------------------------------------------------------------------------- */

TEST(ErrorContract, CallRaisesOnErrTable)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "ufsrvscript.call('fail')"), -1);
    EXPECT_NE(std::string(UfsrvScriptLuaErrorString(lua)).find("deliberate failure"),
              std::string::npos);
    UfsrvScriptLuaClose(lua);
}

TEST(ErrorContract, PcallReturnsErrTable)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua,
        "local e = ufsrvscript.pcall('fail');"
        "assert(type(e) == 'table' and e.err == 'deliberate failure')"), 0);
    UfsrvScriptLuaClose(lua);
}

TEST(ErrorContract, BindingRaiseIsCaughtByPcall)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua,
        "local e = ufsrvscript.pcall('raise'); assert(type(e) == 'table' and e.err ~= nil)"), 0);
    UfsrvScriptLuaClose(lua);
}

TEST(ErrorContract, ErrorReplyAndStatusReplyHelpers)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua,
        "assert(ufsrvscript.error_reply('boom').err == 'boom');"
        "assert(ufsrvscript.status_reply('hi').ok == 'hi')"), 0);
    UfsrvScriptLuaClose(lua);
}

/* ---------------------------------------------------------------------------
 * Dispatch edge cases
 * ------------------------------------------------------------------------- */

TEST(Dispatch, UnknownCommand)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "ufsrvscript.call('nonexistent')"), -1);
    UfsrvScriptLuaClose(lua);
}

TEST(Dispatch, UnknownCommandViaPcall)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua,
        "local e = ufsrvscript.pcall('nonexistent'); assert(type(e) == 'table' and e.err ~= nil)"), 0);
    UfsrvScriptLuaClose(lua);
}

TEST(Dispatch, ArityExactViolation)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    /* "double" declares arity 1; passing two args must error. */
    EXPECT_EQ(sLoadRun(lua, "ufsrvscript.call('double', 1, 2)"), -1);
    UfsrvScriptLuaClose(lua);
}

TEST(Dispatch, CallWithNoCommandName)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "ufsrvscript.call()"), -1);
    UfsrvScriptLuaClose(lua);
}

TEST(Dispatch, NonStringCommandName)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "ufsrvscript.call(123)"), -1);
    UfsrvScriptLuaClose(lua);
}

TEST(Dispatch, RecursionDetected)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua,
        "local e = ufsrvscript.pcall('recurse'); assert(type(e) == 'table' and e.err ~= nil)"), 0);
    UfsrvScriptLuaClose(lua);
}

TEST(Dispatch, BindingAvailableFirstClass)
{
    /* Bindings are also registered as first-class functions on the ufsrv table. */
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "assert(ufsrvscript.double(10) == 20)"), 0);
    UfsrvScriptLuaClose(lua);
}

/* ---------------------------------------------------------------------------
 * Result marshalling
 * ------------------------------------------------------------------------- */

TEST(ResultMarshalling, BindingReturningNil)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua, "assert(ufsrvscript.call('nilret') == nil)"), 0);
    UfsrvScriptLuaClose(lua);
}

TEST(ResultMarshalling, NonStringErrFieldDoesNotRaise)
{
    /* A { err = 42 } table (non-string err) must not be mistaken for an error. */
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_EQ(sLoadRun(lua,
        "local t = ufsrvscript.call('bad_err'); assert(type(t) == 'table' and t.err == 42)"), 0);
    UfsrvScriptLuaClose(lua);
}

TEST(ResultMarshalling, ScriptReturnValueLeftOnStack)
{
    const char src[] = "return ufsrvscript.call('double', 21)";
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(kBindings, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    ASSERT_EQ(UfsrvScriptLuaLoadBuffer(lua, src, sizeof(src) - 1, "@test"), 0);
    ASSERT_EQ(UfsrvScriptLuaRun(lua), 0);
    lua_State *L = UfsrvScriptLuaState(lua);
    EXPECT_EQ(lua_type(L, -1), LUA_TNUMBER);
    EXPECT_DOUBLE_EQ(lua_tonumber(L, -1), 42.0);
    lua_pop(L, 1);
    UfsrvScriptLuaClose(lua);
}

/* ---------------------------------------------------------------------------
 * Helper functions + error-string accessors
 * ------------------------------------------------------------------------- */

TEST(Helpers, PushErrorFormatsArgs)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    lua_State *L = UfsrvScriptLuaState(lua);
    UfsrvScriptLuaPushError(L, "err %d", 7);
    ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);
    lua_getfield(L, -1, "err");
    EXPECT_STREQ(lua_tostring(L, -1), "err 7");
    lua_pop(L, 2);
    UfsrvScriptLuaClose(lua);
}

TEST(Helpers, PushStatusFormatsMessage)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    lua_State *L = UfsrvScriptLuaState(lua);
    UfsrvScriptLuaPushStatus(L, "hi");
    ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);
    lua_getfield(L, -1, "ok");
    EXPECT_STREQ(lua_tostring(L, -1), "hi");
    lua_pop(L, 2);
    UfsrvScriptLuaClose(lua);
}

TEST(Helpers, PushErrorAndStatusNullState)
{
    UfsrvScriptLuaPushError(NULL, "x");   /* must not crash */
    UfsrvScriptLuaPushStatus(NULL, "x");  /* must not crash */
}

TEST(ErrorString, EmptyBeforeAnyError)
{
    UfsrvScriptLua *lua = UfsrvScriptBootstrap(NULL, NULL, NULL);
    ASSERT_NE(lua, nullptr);
    EXPECT_STREQ(UfsrvScriptLuaErrorString(lua), "");
    UfsrvScriptLuaClose(lua);
}

TEST(ErrorString, NullHandle)
{
    EXPECT_STREQ(UfsrvScriptLuaErrorString(NULL), "");
}
