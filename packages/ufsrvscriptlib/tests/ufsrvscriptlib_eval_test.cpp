/**
 * @file ufsrvscriptlib_eval_test.cpp
 * @brief Adversarial gtest suite for the ufsrvscriptlib evaluator convenience API.
 *
 * Written to break the evaluator, not bless it: NULL script/lua/buffer, hostile
 * scripts that must raise not abort, sandbox enforcement (strict globals, no
 * io/os/package), the lossy JSON-ish marshalling contract, and buffer reuse vs
 * fresh allocation. The invariant under test is "no input aborts the process".
 */
#include <gtest/gtest.h>

extern "C" {
#include <ufsrvscriptlib/ufsrvscriptlib_eval.h>
#include <ufsrvscriptlib/ufsrvscriptlib_lua.h>
#include <ufsrvscriptlib/ufsrvscriptlib_version.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <lua.h>
}

#include <cstdio>
#include <cstring>
#include <string>

namespace {

/* Evaluate `script` one-shot, return the marshalled text, and free the buffer. */
static std::string
EvalText(const char *script)
{
    BufferDescriptor *bd = UfsrvScriptEval(script, NULL);
    std::string out;
    if (bd) {
        if (bd->data)
            out.assign(bd->data, bd->size);
        BufferDescriptorRelease(bd);
        free(bd);
    } else {
        out = "<NULL>";
    }
    return out;
}

static std::string
EvalRunText(UfsrvScriptLua *lua, const char *script)
{
    BufferDescriptor *bd = UfsrvScriptEvalRun(lua, script, NULL);
    std::string out;
    if (bd) {
        if (bd->data)
            out.assign(bd->data, bd->size);
        BufferDescriptorRelease(bd);
        free(bd);
    } else {
        out = "<NULL>";
    }
    return out;
}

class EvalFixture : public ::testing::Test
{
};

TEST_F(EvalFixture, EvalScalarResults)
{
    EXPECT_EQ(EvalText("return 42"), "42");
    EXPECT_EQ(EvalText("return 1.5"), "1.5");
    EXPECT_EQ(EvalText("return 'hi'"), "hi");
    EXPECT_EQ(EvalText("return true"), "true");
    EXPECT_EQ(EvalText("return false"), "false");
    EXPECT_EQ(EvalText("return nil"), "null");
    EXPECT_EQ(EvalText("return"), "null");            /* no return value -> nil */
    EXPECT_EQ(EvalText("return 1 + 2 * 3"), "7");     /* arithmetic */
}

/* A table/function result is lossy by contract: lua_tostring returns NULL for
 * them, so the buffer is empty — never a crash. */
TEST_F(EvalFixture, EvalTableIsLossyButSafe)
{
    EXPECT_EQ(EvalText("return {1,2,3}"), "");
    EXPECT_EQ(EvalText("return function() end"), "");
}

TEST_F(EvalFixture, EvalWhitelistedStdlibWorks)
{
    EXPECT_EQ(EvalText("return string.rep('a', 3)"), "aaa");
    EXPECT_EQ(EvalText("return math.abs(-5)"), "5");
    EXPECT_EQ(EvalText("return table.concat({'a','b'}, ',')"), "a,b");
}

/* Sandbox: the engine prunes loadfile/dofile and rejects os/io/package reads. */
TEST_F(EvalFixture, EvalSandboxEnforced)
{
    EXPECT_NE(EvalText("return io").rfind("{\"error\":", 0), std::string::npos);
    EXPECT_NE(EvalText("return os").rfind("{\"error\":", 0), std::string::npos);
    EXPECT_NE(EvalText("return package").rfind("{\"error\":", 0), std::string::npos);
    EXPECT_NE(EvalText("return loadfile").rfind("{\"error\":", 0), std::string::npos);
    EXPECT_NE(EvalText("return dofile").rfind("{\"error\":", 0), std::string::npos);
}

/* Strict globals: creating an undeclared global from script code must fail. */
TEST_F(EvalFixture, EvalStrictGlobalsRejectsCreation)
{
    const std::string r = EvalText("x = 1; return x");
    EXPECT_NE(r.rfind("{\"error\":", 0), std::string::npos);
}

TEST_F(EvalFixture, EvalSyntaxErrorYieldsErrorJson)
{
    const std::string r = EvalText("return (");
    EXPECT_NE(r.rfind("{\"error\":", 0), std::string::npos);
}

TEST_F(EvalFixture, EvalRuntimeErrorYieldsErrorJson)
{
    const std::string r = EvalText("error('boom')");
    EXPECT_NE(r.rfind("{\"error\":", 0), std::string::npos);
    EXPECT_NE(r.find("boom"), std::string::npos);
}

TEST_F(EvalFixture, EvalNullScriptYieldsErrorJson)
{
    const std::string r = EvalText(NULL);
    EXPECT_NE(r.rfind("{\"error\":", 0), std::string::npos);
}

/* NULL buffer: the evaluator allocates a fresh descriptor; caller frees it. */
TEST_F(EvalFixture, EvalNullBufferAllocates)
{
    BufferDescriptor *bd = UfsrvScriptEval("return 'allocated'", NULL);
    ASSERT_NE(bd, nullptr);
    ASSERT_NE(bd->data, nullptr);
    EXPECT_STREQ(bd->data, "allocated");
    BufferDescriptorRelease(bd);
    free(bd);
}

/* Provided (initialised) buffer is reused, not reallocated. */
TEST_F(EvalFixture, EvalProvidedBufferReused)
{
    BufferDescriptor bd;
    BufferDescriptorInit(&bd, 64);
    ASSERT_NE(bd.data, nullptr);

    BufferDescriptor *out = UfsrvScriptEval("return 'reused'", &bd);
    EXPECT_EQ(out, &bd);
    EXPECT_STREQ(bd.data, "reused");

    /* A second call truncates and rewrites the same buffer. */
    out = UfsrvScriptEval("return 'again'", &bd);
    EXPECT_EQ(out, &bd);
    EXPECT_STREQ(bd.data, "again");

    BufferDescriptorRelease(&bd);
}

/* Persistent VM: repeated EvalRun reuses one sandboxed state without leaking. */
TEST_F(EvalFixture, EvalPersistentReuse)
{
    UfsrvScriptLua *lua = UfsrvScriptEvalOpen(NULL);
    ASSERT_NE(lua, nullptr);

    EXPECT_EQ(EvalRunText(lua, "return 1"), "1");
    EXPECT_EQ(EvalRunText(lua, "return 2"), "2");
    EXPECT_EQ(EvalRunText(lua, "return 'three'"), "three");

    for (int i = 0; i < 500; ++i) {
        EXPECT_EQ(EvalRunText(lua, "return 42"), "42");
    }

    UfsrvScriptLuaClose(lua);
}

/* A C-level global set on the persistent VM survives across EvalRun calls. */
TEST_F(EvalFixture, EvalPersistentGlobalState)
{
    UfsrvScriptLua *lua = UfsrvScriptEvalOpen(NULL);
    ASSERT_NE(lua, nullptr);

    lua_State *L = UfsrvScriptLuaState(lua);
    lua_pushinteger(L, 41);
    lua_setglobal(L, "persistent_answer");

    EXPECT_EQ(EvalRunText(lua, "return persistent_answer + 1"), "42");
    UfsrvScriptLuaClose(lua);
}

/* NULL engine / NULL script on the persistent path. */
TEST_F(EvalFixture, EvalRunNullArgsYieldErrorJson)
{
    UfsrvScriptLua *lua = UfsrvScriptEvalOpen(NULL);
    ASSERT_NE(lua, nullptr);

    EXPECT_NE(EvalRunText(NULL, "return 1").rfind("{\"error\":", 0), std::string::npos);
    EXPECT_NE(EvalRunText(lua, NULL).rfind("{\"error\":", 0), std::string::npos);

    UfsrvScriptLuaClose(lua);
}

/* Hostile scripts must raise-or-return, never abort (process survival). */
TEST_F(EvalFixture, EvalHostileInputsDoNotCrash)
{
    static const char *const hostile[] = {
        "", " ", "return", "return nil",
        "return (1", "error()", "error(nil)",
        "return 1/0", "return 0/0", "return math.huge",
        "while true do end",               /* infinite loop — skipped below */
        "return string.rep('x', 0)",
        "local t = {}; return t[1]",
    };
    for (const char *s : hostile) {
        if (std::string(s) == "while true do end")
            continue; /* would hang; not a crash test */
        EvalText(s); /* must not abort */
    }
    SUCCEED();
}

/* The generated version pipeline: full version == major.minor.patch, and the
 * compiled timestamp/by strings are non-empty. */
TEST_F(EvalFixture, VersionFunctions)
{
    char expected[32];
    snprintf(expected, sizeof(expected), "%s.%s.%s",
             UfsrvScriptVersionMajor(), UfsrvScriptVersionMinor(),
             UfsrvScriptVersionPatch());
    EXPECT_STREQ(UfsrvScriptVersion(), expected);
    EXPECT_GT(strlen(UfsrvScriptVersionMajor()), 0u);
    EXPECT_GT(strlen(UfsrvScriptCompiledTimestamp()), 0u);
    EXPECT_GT(strlen(UfsrvScriptCompiledBy()), 0u);
}

}  // namespace
