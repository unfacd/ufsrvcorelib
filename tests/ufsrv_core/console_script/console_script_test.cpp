/**
 * @file console_script_test.cpp
 * @brief gtest suite for command-console registration and dispatch.
 *
 * Covers the contract the servers rely on:
 *   - an exact, non-stemmed node is dispatched unchanged;
 *   - a node registered with options.is_stemmed is a sub-router: the walk stops
 *     there and the remaining path is handed to the handler in
 *     ctx->stemmed_path ("" when the stem itself was addressed);
 *   - a node registered below a stem is shadowed by it, by design;
 *   - ctx->user_data is whatever the registrant passed, returned verbatim;
 *   - ctx->buffer is the caller's buffer;
 *   - unknown paths and group metadata behave as before.
 */

#include "gtest/gtest.h"

extern "C" {
#include <ufsrvcorelib/ufsrv_console_script/ufsrv_console_script.h>
#include <ufsrvcorelib/ufsrv_console_script/ufsrv_console_script_type.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>
}

#include <cstring>
#include <string>

namespace {

/* What the last handler invocation saw. */
struct Observation {
  int          calls;
  const char  *stemmed_path;
  void        *user_data;
  BufferDescriptor *buffer;
};

Observation g_seen;

void
ResetObservation()
{
  g_seen.calls        = 0;
  g_seen.stemmed_path = "";
  g_seen.user_data    = nullptr;
  g_seen.buffer       = nullptr;
}

BufferDescriptor *
RecordingGet(ScriptHandlerContext *ctx)
{
  g_seen.calls        = g_seen.calls + 1;
  g_seen.stemmed_path = ctx->stemmed_path;
  g_seen.user_data    = ctx->user_data;
  g_seen.buffer       = ctx->buffer;

  return ctx->buffer;
}

BufferDescriptor *
RecordingDel(ScriptHandlerContext *ctx)
{
  return RecordingGet(ctx);
}

BufferDescriptor *
RecordingPut(ScriptHandlerContext *ctx)
{
  return RecordingGet(ctx);
}

class ConsoleScriptTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    UfsrvConsoleScriptDestroy();
    ResetObservation();
  }

  void TearDown() override
  {
    UfsrvConsoleScriptDestroy();
  }

  static void RegisterGet(const char *path, ScriptHandlerUserData *user_data = nullptr,
                          bool stemmed = false)
  {
    UfsrvConsoleScriptHandler h = {};

    h.on_get              = RecordingGet;
    h.methods             = UFSRV_CONSOLE_METHOD_GET;
    h.user_data           = user_data;
    h.options.is_stemmed  = stemmed;

    ASSERT_EQ(UfsrvConsoleScriptRegister(path, "test node", &h), 0) << path;
  }

  static void RegisterAll(const char *path, ScriptHandlerUserData *user_data,
                          bool stemmed)
  {
    UfsrvConsoleScriptHandler h = {};

    h.on_get             = RecordingGet;
    h.on_del             = RecordingDel;
    h.on_put             = RecordingPut;
    h.methods            = UFSRV_CONSOLE_METHOD_GET | UFSRV_CONSOLE_METHOD_DEL |
                           UFSRV_CONSOLE_METHOD_PUT;
    h.user_data          = user_data;
    h.options.is_stemmed = stemmed;

    ASSERT_EQ(UfsrvConsoleScriptRegister(path, "test node", &h), 0) << path;
  }

  /* Dispatch and return the buffer the resolver produced. */
  static BufferDescriptor *Dispatch(const char *method, const char *path,
                                    BufferDescriptor *bd)
  {
    return UfsrvConsoleScriptResolve(method, path, bd);
  }
};

/* ── Exact, non-stemmed node: unchanged behaviour ───────────────────────── */

TEST_F(ConsoleScriptTest, ExactNodeDispatchesUnchanged)
{
  int user_data = 7;

  RegisterGet("/plain/exact", &user_data);

  BufferDescriptor bd;
  BufferDescriptorInit(&bd, 64);

  BufferDescriptor *out = Dispatch("GET", "/plain/exact", &bd);

  EXPECT_EQ(out, &bd);
  EXPECT_EQ(g_seen.calls, 1);
  EXPECT_EQ(g_seen.stemmed_path, nullptr);
  EXPECT_EQ(g_seen.user_data, &user_data);
  EXPECT_EQ(g_seen.buffer, &bd);

  BufferDescriptorRelease(&bd);
}

TEST_F(ConsoleScriptTest, UnknownPathStillReturnsNull)
{
  RegisterGet("/plain/exact");

  BufferDescriptor bd;
  BufferDescriptorInit(&bd, 64);

  EXPECT_EQ(Dispatch("GET", "/plain/nope", &bd), nullptr);
  EXPECT_EQ(g_seen.calls, 0);

  BufferDescriptorRelease(&bd);
}

/* ── Stem: sub-router semantics ─────────────────────────────────────────── */

TEST_F(ConsoleScriptTest, StemReceivesRemainder)
{
  RegisterGet("/internals/delegator", nullptr, /*stemmed=*/true);

  BufferDescriptor bd;
  BufferDescriptorInit(&bd, 64);

  ASSERT_NE(Dispatch("GET", "/internals/delegator/suspend", &bd), nullptr);
  EXPECT_STREQ(g_seen.stemmed_path, "suspend");

  ASSERT_NE(Dispatch("GET", "/internals/delegator/suspend/id-42", &bd), nullptr);
  EXPECT_STREQ(g_seen.stemmed_path, "suspend/id-42");

  ASSERT_NE(Dispatch("GET", "/internals/delegator", &bd), nullptr);
  EXPECT_STREQ(g_seen.stemmed_path, "");

  EXPECT_EQ(g_seen.calls, 3);

  BufferDescriptorRelease(&bd);
}

TEST_F(ConsoleScriptTest, StemShadowsNodeRegisteredBelowIt)
{
  RegisterAll("/internals/delegator", nullptr, /*stemmed=*/true);
  RegisterGet("/internals/delegator/status");

  BufferDescriptor bd;
  BufferDescriptorInit(&bd, 64);

  ASSERT_NE(Dispatch("GET", "/internals/delegator/status", &bd), nullptr);

  /* the stem handled it; the node below never ran */
  EXPECT_EQ(g_seen.calls, 1);
  EXPECT_STREQ(g_seen.stemmed_path, "status");

  BufferDescriptorRelease(&bd);
}

TEST_F(ConsoleScriptTest, StemHandlesDelAndPutWithRemainder)
{
  RegisterAll("/internals/delegator", nullptr, /*stemmed=*/true);

  BufferDescriptor bd;
  BufferDescriptorInit(&bd, 64);

  ASSERT_NE(Dispatch("DEL", "/internals/delegator/jobs/9", &bd), nullptr);
  EXPECT_STREQ(g_seen.stemmed_path, "jobs/9");

  ASSERT_NE(Dispatch("PUT", "/internals/delegator/jobs/9", &bd), nullptr);
  EXPECT_STREQ(g_seen.stemmed_path, "jobs/9");

  BufferDescriptorRelease(&bd);
}

/* ── user_data is per registration and handed back verbatim ─────────────── */

TEST_F(ConsoleScriptTest, UserDataIsReturnedToItsOwnHandler)
{
  int first  = 1;
  int second = 2;

  RegisterAll("/one", &first, /*stemmed=*/true);
  RegisterAll("/two", &second, /*stemmed=*/true);

  BufferDescriptor bd;
  BufferDescriptorInit(&bd, 64);

  ASSERT_NE(Dispatch("GET", "/one/child", &bd), nullptr);
  EXPECT_EQ(g_seen.user_data, &first);

  ASSERT_NE(Dispatch("GET", "/two/child", &bd), nullptr);
  EXPECT_EQ(g_seen.user_data, &second);

  BufferDescriptorRelease(&bd);
}

/* ── Group metadata is unaffected ───────────────────────────────────────── */

TEST_F(ConsoleScriptTest, GroupNodeStillEnumeratesChildren)
{
  RegisterGet("/group/alpha");
  RegisterGet("/group/beta");

  BufferDescriptor bd;
  BufferDescriptorInit(&bd, 128);

  BufferDescriptor *out = Dispatch("GET", "/group", &bd);
  ASSERT_NE(out, nullptr);
  ASSERT_NE(out->data, nullptr);
  EXPECT_NE(std::strstr(out->data, "alpha"), nullptr);
  EXPECT_NE(std::strstr(out->data, "beta"), nullptr);

  BufferDescriptorRelease(&bd);
}

}  // namespace
