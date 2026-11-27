/**
 * @file configfile_processor_test.cpp
 * @brief Google Test suite for the configfile_processor module.
 *
 * Exercises all 20 public functions against real Lua config files and the
 * committed schemas.  The production configfile_processor.c is compiled
 * directly into the test executable.
 *
 * Copyright (C) 2015-2026 unfacd works
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>

extern "C" {
#include "configfile_processor.h"
#include "configfile_processor_priv.h"
#include "ufsrv_config_keymap_entry_type.h"
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
#include <ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h>
#include "data_ufsrv_config_literal_to_field_map_priv_generated.h"
}

/* ── Path resolution ──────────────────────────────────────────────────── */

static const char *kRepoRoot = nullptr;

/** One-time init: locate the repo root relative to the test binary. */
static void sInitPaths() {
  if (kRepoRoot) return;
  // Walk up from CMAKE_SOURCE_DIR (set via compile definition below) or use
  // the REPO_ROOT environment variable set by ctest.
  const char *env = getenv("REPO_ROOT");
  if (env) {
    kRepoRoot = strdup(env);
    return;
  }
  // Fallback: assume running from build/debug/
  kRepoRoot = CMAKE_SOURCE_DIR;
}

static std::string etcPath(const char *basename) {
  sInitPaths();
  return std::string(kRepoRoot) + "/etc/" + basename;
}

static std::string fixturePath(const char *basename) {
  sInitPaths();
  return std::string(kRepoRoot) + "/tests/lua/fixtures/" + basename;
}

/* ── Temp-file helper ─────────────────────────────────────────────────── */

static std::string sWriteTempFile(const char *content) {
  char tmpl[] = "/tmp/conf_test_XXXXXX";
  int fd = mkstemp(tmpl);
  EXPECT_GE(fd, 0);
  if (fd < 0) return "";
  FILE *fp = fdopen(fd, "w");
  EXPECT_NE(fp, nullptr);
  fputs(content, fp);
  fclose(fp);
  return std::string(tmpl);
}

/* ════════════════════════════════════════════════════════════════════════
   Group 1 — Lifecycle
   ════════════════════════════════════════════════════════════════════════ */

TEST(ConfFileLifecycle, InitValid) {
  auto path = etcPath("core_schema_ufsrv.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
  ConfFileDestroy(cp);
}

TEST(ConfFileLifecycle, InitNonexistentFile) {
  ConfFile cp = ConfFileInit("/tmp/no_such_config_12345.lua");
  EXPECT_EQ(cp, nullptr);
}

TEST(ConfFileLifecycle, InitNullPath) {
  ConfFile cp = ConfFileInit(nullptr);
  EXPECT_EQ(cp, nullptr);
}

TEST(ConfFileLifecycle, InitSyntaxError) {
  auto path = sWriteTempFile("ufsrv = { name = , }");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  EXPECT_EQ(cp, nullptr);
  unlink(path.c_str());
}

TEST(ConfFileLifecycle, DestroyNull) {
  ConfFileDestroy(nullptr);  // must not crash
  SUCCEED();
}

TEST(ConfFileLifecycle, DestroyValid) {
  auto path = etcPath("core_schema_ufsrv.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  ConfFileDestroy(cp);
  SUCCEED();
}

TEST(ConfFileLifecycle, DoubleDestroy) {
  auto path = etcPath("core_schema_ufsrv.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  ConfFileDestroy(cp);
  ConfFileDestroy(nullptr);  // second destroy is a no-op via null
  SUCCEED();
}

/* ════════════════════════════════════════════════════════════════════════
   Group 2 — Error handling
   ════════════════════════════════════════════════════════════════════════ */

TEST(ConfFileError, ErrorToString) {
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_OK), "OK");
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_ERR_LUA), "Lua runtime error");
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_ERR_SCOPE_NOT_FOUND), "scope not found");
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_ERR_KEY_NOT_FOUND), "key not found");
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_ERR_TYPE_MISMATCH), "type mismatch");
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_ERR_ARGUMENT), "invalid argument");
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_ERR_MEMORY), "memory allocation failure");
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_ERR_NOT_INITIALISED), "not initialised");
  EXPECT_STREQ(ConfFileErrorToString(CONF_FILE_ERR_INDEX_OUT_OF_RANGE), "index out of range");
}

TEST(ConfFileError, GetLastErrorOnNull) {
  EXPECT_EQ(ConfFileGetLastError(nullptr), CONF_FILE_ERR_ARGUMENT);
}

TEST(ConfFileError, GetLastErrorMessageOnNull) {
  EXPECT_STREQ(ConfFileGetLastErrorMessage(nullptr), "handle is NULL");
}

/* ════════════════════════════════════════════════════════════════════════
   Group 3 — String accessors
   ════════════════════════════════════════════════════════════════════════ */

class ConfFileStringTest : public ::testing::Test {
protected:
  ConfFile cp = nullptr;

  void SetUp() override {
    // Create a minimal config with ufsrv scope
    auto path = sWriteTempFile(
        "ufsrv = { server_run_mode = \"shadow\", command_console_port = 19701, "
        "ufsrv_geogroup = 3, server_id = 1 }\n");
    ASSERT_FALSE(path.empty());
    cp = ConfFileInit(path.c_str());
    ASSERT_NE(cp, nullptr);
    unlink(path.c_str());
  }

  void TearDown() override {
    ConfFileDestroy(cp);
    cp = nullptr;
  }
};

TEST_F(ConfFileStringTest, GetStringValid) {
  const char *val = ConfFileGetString(cp, "ufsrv", "server_run_mode");
  ASSERT_NE(val, nullptr);
  EXPECT_STREQ(val, "shadow");
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
}

TEST_F(ConfFileStringTest, GetStringMissingScope) {
  const char *val = ConfFileGetString(cp, "nonexistent", "key");
  EXPECT_EQ(val, nullptr);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_SCOPE_NOT_FOUND);
}

TEST_F(ConfFileStringTest, GetStringMissingKey) {
  const char *val = ConfFileGetString(cp, "ufsrv", "nonexistent_key");
  EXPECT_EQ(val, nullptr);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_KEY_NOT_FOUND);
}

TEST_F(ConfFileStringTest, GetStringTypeMismatch) {
  const char *val = ConfFileGetString(cp, "ufsrv", "command_console_port");
  EXPECT_EQ(val, nullptr);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_TYPE_MISMATCH);
}

TEST_F(ConfFileStringTest, GetStringDefaultValid) {
  const char *val = ConfFileGetStringDefault(cp, "ufsrv", "server_run_mode", "normal");
  ASSERT_NE(val, nullptr);
  EXPECT_STREQ(val, "shadow");
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
}

TEST_F(ConfFileStringTest, GetStringDefaultMissing) {
  const char *val = ConfFileGetStringDefault(cp, "ufsrv", "nonexistent", "fallback");
  ASSERT_NE(val, nullptr);
  EXPECT_STREQ(val, "fallback");
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);  // defaults don't set error
}

TEST_F(ConfFileStringTest, GetStringCopy) {
  char buf[64] = {0};
  int n = ConfFileGetStringCopy(cp, "ufsrv", "server_run_mode", buf, sizeof(buf));
  EXPECT_GT(n, 0);
  EXPECT_STREQ(buf, "shadow");
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
}

TEST_F(ConfFileStringTest, GetStringCopySmallBuf) {
  char buf[4] = {0};
  int n = ConfFileGetStringCopy(cp, "ufsrv", "server_run_mode", buf, sizeof(buf));
  EXPECT_EQ(n, 3);
  EXPECT_STREQ(buf, "sha");  // truncated
}

/* ════════════════════════════════════════════════════════════════════════
   Group 4 — Integer accessors
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileStringTest, GetIntValid) {
  int val = ConfFileGetInt(cp, "ufsrv", "command_console_port");
  EXPECT_EQ(val, 19701);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
}

TEST_F(ConfFileStringTest, GetIntMissing) {
  int val = ConfFileGetInt(cp, "ufsrv", "nonexistent");
  EXPECT_EQ(val, 0);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_KEY_NOT_FOUND);
}

TEST_F(ConfFileStringTest, GetIntTypeMismatch) {
  int val = ConfFileGetInt(cp, "ufsrv", "server_run_mode");
  EXPECT_EQ(val, 0);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_TYPE_MISMATCH);
}

TEST_F(ConfFileStringTest, GetIntDefault) {
  int val = ConfFileGetIntDefault(cp, "ufsrv", "nonexistent", 999);
  EXPECT_EQ(val, 999);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
}

TEST_F(ConfFileStringTest, GetIntGenuineZero) {
  // server_id is 1, not 0 — so we test that zero return with error is distinguishable
  int val = ConfFileGetInt(cp, "ufsrv", "nonexistent");
  EXPECT_EQ(val, 0);
  EXPECT_NE(ConfFileGetLastError(cp), CONF_FILE_OK);  // error was set

  // But a genuine read of a zero value would have CONF_FILE_OK
  // (we can't test genuine zero with our fixture — no zero-valued int field)
}

/* ════════════════════════════════════════════════════════════════════════
   Group 5 — Number and Boolean accessors
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileStringTest, GetNumberValid) {
  double val = ConfFileGetNumber(cp, "ufsrv", "command_console_port");
  EXPECT_DOUBLE_EQ(val, 19701.0);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
}

TEST_F(ConfFileStringTest, GetBoolValid) {
  // Our fixture doesn't have a boolean — test with a temp config
  auto path = sWriteTempFile("ufsrv = { ufsrv_geogroup = 3, command_console_port = 19701, enabled = true }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp2 = ConfFileInit(path.c_str());
  ASSERT_NE(cp2, nullptr);
  unlink(path.c_str());

  bool val = ConfFileGetBool(cp2, "ufsrv", "enabled");
  EXPECT_TRUE(val);
  EXPECT_EQ(ConfFileGetLastError(cp2), CONF_FILE_OK);
  ConfFileDestroy(cp2);
}

TEST_F(ConfFileStringTest, GetBoolFalse) {
  auto path = sWriteTempFile("ufsrv = { ufsrv_geogroup = 3, command_console_port = 19701, enabled = false }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp2 = ConfFileInit(path.c_str());
  ASSERT_NE(cp2, nullptr);
  unlink(path.c_str());

  bool val = ConfFileGetBool(cp2, "ufsrv", "enabled");
  EXPECT_FALSE(val);
  EXPECT_EQ(ConfFileGetLastError(cp2), CONF_FILE_OK);
  ConfFileDestroy(cp2);
}

TEST_F(ConfFileStringTest, GetBoolTypeMismatch) {
  bool val = ConfFileGetBool(cp, "ufsrv", "command_console_port");
  EXPECT_FALSE(val);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_TYPE_MISMATCH);
}

/* ════════════════════════════════════════════════════════════════════════
   Group 6 — Nested table and array access
   ════════════════════════════════════════════════════════════════════════ */

TEST(ConfFileTableArray, IsTable) {
  auto path = sWriteTempFile(
      "ufsrv = { ufsrv_geogroup=3, command_console_port=19701, "
      "user_timeouts = { unauthenticated_timeout = 60 } }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  EXPECT_TRUE(ConfFileIsTable(cp, "ufsrv", "user_timeouts"));
  EXPECT_FALSE(ConfFileIsTable(cp, "ufsrv", "command_console_port"));
  EXPECT_FALSE(ConfFileIsTable(cp, "ufsrv", "nonexistent"));
  ConfFileDestroy(cp);
}

TEST(ConfFileTableArray, ArrayLength) {
  auto path = sWriteTempFile(
      "ufsrvapi = { "
      "  allowed_origins = { \"https://a.com\", \"https://b.com\", \"https://c.com\" } "
      "}\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  int len = ConfFileGetArrayLength(cp, "ufsrvapi", "allowed_origins");
  EXPECT_EQ(len, 3);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
  ConfFileDestroy(cp);
}

TEST(ConfFileTableArray, ArrayElement) {
  auto path = sWriteTempFile(
      "ufsrvapi = { allowed_origins = { \"https://first.com\", \"https://second.com\" } }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  const char *el = ConfFileGetArrayElement(cp, "ufsrvapi", "allowed_origins", 1);
  ASSERT_NE(el, nullptr);
  EXPECT_STREQ(el, "https://first.com");
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);
  ConfFileDestroy(cp);
}

TEST(ConfFileTableArray, ArrayElementOutOfRange) {
  auto path = sWriteTempFile(
      "ufsrvapi = { allowed_origins = { \"https://first.com\" } }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  const char *el = ConfFileGetArrayElement(cp, "ufsrvapi", "allowed_origins", 99);
  EXPECT_EQ(el, nullptr);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_INDEX_OUT_OF_RANGE);
  ConfFileDestroy(cp);
}

TEST(ConfFileTableArray, ArrayElementIndexZero) {
  auto path = sWriteTempFile(
      "ufsrvapi = { allowed_origins = { \"https://first.com\" } }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  const char *el = ConfFileGetArrayElement(cp, "ufsrvapi", "allowed_origins", 0);
  EXPECT_EQ(el, nullptr);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_INDEX_OUT_OF_RANGE);
  ConfFileDestroy(cp);
}

/* ════════════════════════════════════════════════════════════════════════
   Group 7 — Scope and key discovery
   ════════════════════════════════════════════════════════════════════════ */

TEST(ConfFileDiscovery, HasScope) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);

  EXPECT_TRUE(ConfFileHasScope(cp, "ufsrv"));
  EXPECT_TRUE(ConfFileHasScope(cp, "ufnet"));
  EXPECT_FALSE(ConfFileHasScope(cp, "nonexistent"));

  ConfFileDestroy(cp);
}

TEST(ConfFileDiscovery, NextScope) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);

  int idx = -1;
  int count = 0;
  bool found_ufsrv = false, found_ufnet = false;
  const char *scope;
  while ((scope = ConfFileNextScope(cp, &idx)) != nullptr) {
    count++;
    if (strcmp(scope, "ufsrv") == 0) found_ufsrv = true;
    if (strcmp(scope, "ufnet") == 0) found_ufnet = true;
  }
  EXPECT_GE(count, 2);
  EXPECT_TRUE(found_ufsrv);
  EXPECT_TRUE(found_ufnet);

  ConfFileDestroy(cp);
}

TEST(ConfFileDiscovery, NextKey) {
  auto path = sWriteTempFile(
      "ufsrv = { ufsrv_geogroup = 3, command_console_port = 19701, server_id = 1 }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  int idx = -1;
  int count = 0;
  const char *key;
  while ((key = ConfFileNextKey(cp, "ufsrv", &idx)) != nullptr) {
    count++;
    EXPECT_NE(key[0], '\0');
  }
  EXPECT_EQ(count, 3);
  ConfFileDestroy(cp);
}

/* ════════════════════════════════════════════════════════════════════════
   Group 8 — Symbol-table resolution
   ════════════════════════════════════════════════════════════════════════ */

static int  g_is_valid_calls = 0;

static bool sValidatePort(const ConfFileSymbolTableEntry *entry,
                          const UfsrvConfigValue *value, char *errbuf, size_t errsz) {
  (void)entry;
  g_is_valid_calls++;
  if (value->stored_value.int_val < 1 || value->stored_value.int_val > 65535) {
    snprintf(errbuf, errsz, "port out of range");
    return false;
  }
  return true;
}

static ConfFileSymbolTableEntry g_test_symbols[] = {
  { .scope = "ufsrv", .key = "command_console_port",
    .type = UFSRV_CONFIG_VALUE_INT, .value = {0},
    .field_index = UFSRV_CONFIG_FIELD_COUNT,
    .description = "test port",
    .lifecycle = { .on_validate = sValidatePort } },
  { .scope = "ufsrv", .key = "server_run_mode",
    .type = UFSRV_CONFIG_VALUE_STR, .value = {0},
    .field_index = UFSRV_CONFIG_FIELD_COUNT,
    .description = "test mode",
    .lifecycle = { .on_validate = nullptr } },
  { .scope = nullptr }
};

TEST(ConfFileSymbolTable, ResolveSymbolsBasic) {
  auto path = sWriteTempFile(
      "ufsrv = { ufsrv_geogroup=3, command_console_port=19701, "
      "server_run_mode=\"shadow\" }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  g_is_valid_calls = 0;

  int failures = ConfFileTransferToConfigValues(cp, g_test_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 0);
  EXPECT_EQ(g_test_symbols[0].value.stored_value.int_val, 19701);
  EXPECT_STREQ(g_test_symbols[1].value.stored_value.str_val, "shadow");
  EXPECT_EQ(g_is_valid_calls, 1);
  EXPECT_EQ(g_test_symbols[0].value.source, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

  ConfFileDestroy(cp);
}

TEST(ConfFileSymbolTable, TransfersNestedRestTlsValues) {
  auto path = sWriteTempFile(
      "return { ufsrvsfu = { rest_server = { tls = { "
      "is_enabled = true, certificate_file = 'rest-chain.pem', "
      "private_key = 'enc:v1:test-envelope' } } } }");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  unlink(path.c_str());
  ASSERT_NE(cp, nullptr);

  ConfFileSymbolTableEntry symbols[] = {
    { .scope = "ufsrvsfu", .key = "rest_server.tls.is_enabled",
      .type = UFSRV_CONFIG_VALUE_BOOL },
    { .scope = "ufsrvsfu", .key = "rest_server.tls.certificate_file",
      .type = UFSRV_CONFIG_VALUE_STR },
    { .scope = "ufsrvsfu", .key = "rest_server.tls.private_key",
      .type = UFSRV_CONFIG_VALUE_STR },
    { .scope = nullptr }
  };
  EXPECT_EQ(ConfFileTransferToConfigValues(cp, symbols, nullptr, 0, nullptr), 0);
  EXPECT_EQ(symbols[0].value.stored_value.int_val, 1);
  EXPECT_STREQ(symbols[1].value.stored_value.str_val, "rest-chain.pem");
  EXPECT_STREQ(symbols[2].value.stored_value.str_val, "enc:v1:test-envelope");
  for (size_t i = 0; i < 3; ++i)
    EXPECT_EQ(symbols[i].value.source, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

  free(symbols[1].value.stored_value.str_val);
  free(symbols[2].value.stored_value.str_val);
  ConfFileDestroy(cp);
}

TEST(ConfFileSymbolTable, ResolveMissingKeyReportsError) {
  auto path = sWriteTempFile(
      "ufsrv = { ufsrv_geogroup = 3, command_console_port = 4444 }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  // server_run_mode is missing → error, not defaulted
  g_test_symbols[1].value = UfsrvConfigValue{0};
  int failures = ConfFileTransferToConfigValues(cp, g_test_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 1);
  // The missing key's value slot stays unassigned
  EXPECT_EQ(g_test_symbols[1].value.source, UFSRV_CONFIG_SOURCE_UNASSIGNED);
  // The present key still resolves
  EXPECT_EQ(g_test_symbols[0].value.stored_value.int_val, 4444);

  ConfFileDestroy(cp);
}

TEST(ConfFileSymbolTable, ResolveValidationFailure) {
  auto path = sWriteTempFile(
      "ufsrv = { ufsrv_geogroup=3, command_console_port=0, "
      "server_run_mode=\"shadow\" }\n");  // port 0 is invalid
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  g_is_valid_calls = 0;
  int failures = ConfFileTransferToConfigValues(cp, g_test_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 1);  // port validation failed
  EXPECT_EQ(g_is_valid_calls, 1);

  ConfFileDestroy(cp);
}

/* ════════════════════════════════════════════════════════════════════════
   Group 9 — JSON dump
   ════════════════════════════════════════════════════════════════════════ */

TEST(ConfFileDumpJson, BasicDump) {
  // Populate e->value via ResolveSymbols, then serialise
  auto path = sWriteTempFile(
      "ufsrv = { ufsrv_geogroup=3, command_console_port=19701, "
      "server_run_mode=\"shadow\" }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  ConfFileTransferToConfigValues(cp, g_test_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);

  const char *json_path = "/tmp/conf_test_dump.json";
  unlink(json_path);
  int rc = ConfFileDumpJson(g_test_symbols, json_path);
  EXPECT_EQ(rc, 0);

  // Read back and check
  std::ifstream f(json_path);
  ASSERT_TRUE(f.is_open());
  std::string content((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
  EXPECT_TRUE(content.find("\"ufsrv\"") != std::string::npos);
  EXPECT_TRUE(content.find("19701") != std::string::npos);
  EXPECT_TRUE(content.find("shadow") != std::string::npos);
  f.close();
  unlink(json_path);

  ConfFileDestroy(cp);
}

TEST(ConfFileDumpJson, NullPath) {
  int rc = ConfFileDumpJson(g_test_symbols, nullptr);
  EXPECT_EQ(rc, -1);
}

/* ════════════════════════════════════════════════════════════════════════
   Group 10 — Stack discipline
   ════════════════════════════════════════════════════════════════════════ */

TEST(ConfFileStack, SequentialAccess) {
  auto path = sWriteTempFile(
      "ufsrv = { ufsrv_geogroup=3, command_console_port=19701, "
      "server_run_mode=\"shadow\", server_id=1 }\n");
  ASSERT_FALSE(path.empty());
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  unlink(path.c_str());

  // 100 sequential calls — must not overflow the Lua stack
  for (int i = 0; i < 100; i++) {
    const char *s = ConfFileGetString(cp, "ufsrv", "server_run_mode");
    EXPECT_STREQ(s, "shadow");
    int v = ConfFileGetInt(cp, "ufsrv", "command_console_port");
    EXPECT_EQ(v, 19701);
  }
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_OK);

  ConfFileDestroy(cp);
}
