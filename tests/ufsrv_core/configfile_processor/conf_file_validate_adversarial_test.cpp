/**
 * @file conf_file_validate_adversarial_test.cpp
 * @brief Severely adversarial test for ConfFileValidate().
 *
 * Probes NULL / empty / garbage parameters, malicious Lua config and validator
 * inputs, symbol-table edge cases, and the finding-callback contract.  Built
 * under ASan/UBSan/LSan so memory violations surface as failures.
 *
 * Copyright (C) 2015-2026 unfacd works
 */

#include <gtest/gtest.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/configfile_processor/configfile_processor.h>
#include <ufsrvcorelib/ufsrv_core/configfile_processor/configfile_processor_symbol_table_type.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_value_type.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
}

/* ── Finding capture ────────────────────────────────────────────────────── */

struct Finding {
  std::string scope;
  std::string key;
  std::string severity;
  std::string message;
};

static std::vector<Finding> g_findings;

static void
sOnFinding(const char *scope, const char *key, const char *severity, const char *message)
{
  g_findings.push_back({scope ? scope : "", key ? key : "",
                        severity ? severity : "", message ? message : ""});
}

/* ── Temp-file helper (unique per-process, no randomness) ────────────────── */

static int sTempCounter = 0;

static std::string
sWriteTempFile(const char *content)
{
  char path[128];
  snprintf(path, sizeof(path), "/tmp/conf_file_validate_%d_%d.lua",
           (int)getpid(), ++sTempCounter);
  FILE *f = fopen(path, "w");
  if (!f) return "";
  fputs(content, f);
  fclose(f);
  return std::string(path);
}

static void
sRemoveFile(const std::string &path)
{
  if (!path.empty()) unlink(path.c_str());
}

/* ── Symbol-table helpers ────────────────────────────────────────────────── */

static ConfFileSymbolTableEntry
sMakeEntry(const char *scope, const char *key, UfsrvConfigValueType type)
{
  ConfFileSymbolTableEntry e;
  memset(&e, 0, sizeof(e));
  e.scope = scope;
  e.key   = key;
  e.type  = type;
  return e;
}

/* Minimal validator: defines a GLOBAL validate_config that always passes. */
static const char *kVValidatorPass =
  "function validate_config(config, core_schema, adjunct_schema)\n"
  "  return true\n"
  "end\n";

/* Minimal config: one ufsrv scope, one int key. */
static const char *kConfigOneInt =
  "return { ufsrv = { command_console_port = 8080 } }\n";

/* ── Fixture ─────────────────────────────────────────────────────────────── */

class ConfFileValidateAdversarialTest : public ::testing::Test {
protected:
  void SetUp() override { g_findings.clear(); }
};

/* ════════════════════════════════════════════════════════════════════════
   Group A — parameter NULL / empty / garbage
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileValidateAdversarialTest, NullOnFindingReturnsMinusOne)
{
  EXPECT_EQ(ConfFileValidate(nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, 0), -1);
  EXPECT_TRUE(g_findings.empty());
}

TEST_F(ConfFileValidateAdversarialTest, NullConfigFileReportsError)
{
  int rc = ConfFileValidate(nullptr, "core.lua", nullptr, "validator.lua",
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  EXPECT_EQ(rc, 1);
  ASSERT_EQ(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].severity.c_str(), "error");
  EXPECT_STREQ(g_findings[0].message.c_str(), "failed to load config file");
}

TEST_F(ConfFileValidateAdversarialTest, EmptyConfigFileReportsError)
{
  int rc = ConfFileValidate("", "core.lua", nullptr, "validator.lua",
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  EXPECT_EQ(rc, 1);
  ASSERT_EQ(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].message.c_str(), "failed to load config file");
}

TEST_F(ConfFileValidateAdversarialTest, NonexistentConfigFileReportsError)
{
  int rc = ConfFileValidate("/tmp/definitely_missing_conf_file_42.lua",
                            "core.lua", nullptr, "validator.lua",
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  EXPECT_EQ(rc, 1);
}

TEST_F(ConfFileValidateAdversarialTest, DirectoryAsConfigFileReportsError)
{
  /* fopen("/tmp","r") succeeds but the read fails → load error, not a crash. */
  int rc = ConfFileValidate("/tmp", "core.lua", nullptr, "validator.lua",
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  EXPECT_EQ(rc, 1);
  ASSERT_EQ(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].message.c_str(), "failed to load config file");
}

TEST_F(ConfFileValidateAdversarialTest, ConfigLuaErrorReportsError)
{
  std::string cfg = sWriteTempFile("error('boom in config')\n");
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, "validator.lua",
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  EXPECT_EQ(rc, 1);
  ASSERT_EQ(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].message.c_str(), "failed to load config file");
}

TEST_F(ConfFileValidateAdversarialTest, ConfigScalarReturnIsAccepted)
{
  /* A non-table return value must not crash ConfFileInit's promotion loop. */
  std::string cfg = sWriteTempFile("return 42\n");
  std::string v   = sWriteTempFile(kVValidatorPass);
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 0);
}

/* ════════════════════════════════════════════════════════════════════════
   Group B — validator edge cases
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileValidateAdversarialTest, NullValidatorFileReportsError)
{
  /* NULL validator is rejected explicitly before any Lua stack access. */
  std::string cfg = sWriteTempFile(kConfigOneInt);
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, nullptr,
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].message.c_str(), "validator file not provided");
}

TEST_F(ConfFileValidateAdversarialTest, EmptyValidatorFileReportsError)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, "",
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
}

TEST_F(ConfFileValidateAdversarialTest, NonexistentValidatorFileReportsError)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr,
                            "/tmp/definitely_missing_validator_42.lua",
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
}

TEST_F(ConfFileValidateAdversarialTest, ValidatorMissingFunctionReportsError)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile("-- no validate_config defined here\n");
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].message.c_str(),
               "validate_config function not found in validator");
}

TEST_F(ConfFileValidateAdversarialTest, ValidatorNonFunctionReportsError)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile("validate_config = 42\n");
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].message.c_str(),
               "validate_config function not found in validator");
}

TEST_F(ConfFileValidateAdversarialTest, ValidatorSyntaxErrorReportsError)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile("this is ((( not valid lua\n");
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
}

TEST_F(ConfFileValidateAdversarialTest, ValidatorRuntimeErrorSurfacesMessage)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(
      "function validate_config(c, s, a) error('boom in validator') end\n");
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("boom in validator"), std::string::npos);
}

/* The shipped core_schema_validator.lua exports validate_config as a module
   table (`return { validate_config = ... }`) — ConfFileValidate must accept
   both that and a plain global function. */
TEST_F(ConfFileValidateAdversarialTest, ValidatorModuleStyleExportIsAccepted)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(
      "return { validate_config = function(c, s, a) return true end }\n");
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(g_findings.empty());
}

/* Prove the validator receives the REAL config table (not an empty one):
   the validator asserts the ufsrv scope and its value are present. */
TEST_F(ConfFileValidateAdversarialTest, ValidatorReceivesRealConfigTable)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(
      "function validate_config(c, s, a)\n"
      "  if c.ufsrv == nil then error('ufsrv scope missing from config') end\n"
      "  if c.ufsrv.command_console_port ~= 8080 then error('command_console_port not passed') end\n"
      "  return true\n"
      "end\n");
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(g_findings.empty());
}

/* ════════════════════════════════════════════════════════════════════════
   Group C — happy path + schema NULL handling
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileValidateAdversarialTest, NullSchemasAreSafe)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(kVValidatorPass);
  int rc = ConfFileValidate(cfg.c_str(), nullptr, nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 0);
}

/* ════════════════════════════════════════════════════════════════════════
   Group D — symbol-table edge cases
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileValidateAdversarialTest, SymbolsMissingKeyCountsFailure)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(kVValidatorPass);
  std::vector<ConfFileSymbolTableEntry> syms;
  syms.push_back(sMakeEntry("ufsrv", "nonexistent_key", UFSRV_CONFIG_VALUE_INT));
  syms.push_back(sMakeEntry(nullptr, nullptr, UFSRV_CONFIG_VALUE_TYPE_UNSPECIFIED));

  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            syms.data(), nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 1);
}

TEST_F(ConfFileValidateAdversarialTest, SymbolsTypeMismatchCountsFailure)
{
  std::string cfg = sWriteTempFile(
      "return { ufsrv = { command_console_port = 'not a number' } }\n");
  std::string v   = sWriteTempFile(kVValidatorPass);
  std::vector<ConfFileSymbolTableEntry> syms;
  syms.push_back(sMakeEntry("ufsrv", "command_console_port", UFSRV_CONFIG_VALUE_INT));
  syms.push_back(sMakeEntry(nullptr, nullptr, UFSRV_CONFIG_VALUE_TYPE_UNSPECIFIED));

  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            syms.data(), nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 1);
}

TEST_F(ConfFileValidateAdversarialTest, SymbolsResolvedOkReturnsZero)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(kVValidatorPass);
  std::vector<ConfFileSymbolTableEntry> syms;
  syms.push_back(sMakeEntry("ufsrv", "command_console_port", UFSRV_CONFIG_VALUE_INT));
  syms.push_back(sMakeEntry(nullptr, nullptr, UFSRV_CONFIG_VALUE_TYPE_UNSPECIFIED));

  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            syms.data(), nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(syms[0].field_index, (int)UFSRV_CONFIG_FIELD_COUNT); /* NULL map → unresolved */
}

TEST_F(ConfFileValidateAdversarialTest, NullAssociativeMapWithSymbolsDoesNotCrash)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(kVValidatorPass);
  std::vector<ConfFileSymbolTableEntry> syms;
  syms.push_back(sMakeEntry("ufsrv", "command_console_port", UFSRV_CONFIG_VALUE_INT));
  syms.push_back(sMakeEntry(nullptr, nullptr, UFSRV_CONFIG_VALUE_TYPE_UNSPECIFIED));

  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            syms.data(), nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 0);
}

/* A NULL lookup map is now short-circuited before bsearch, whatever the size. */
TEST_F(ConfFileValidateAdversarialTest, NullMapNonzeroSzIsIgnored)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(kVValidatorPass);
  std::vector<ConfFileSymbolTableEntry> syms;
  syms.push_back(sMakeEntry("ufsrv", "command_console_port", UFSRV_CONFIG_VALUE_INT));
  syms.push_back(sMakeEntry(nullptr, nullptr, UFSRV_CONFIG_VALUE_TYPE_UNSPECIFIED));

  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            syms.data(), nullptr, sOnFinding, nullptr, (size_t)-1);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 0);
}

/* ════════════════════════════════════════════════════════════════════════
   Group E — finding-callback integrity
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileValidateAdversarialTest, FindingCallbackReceivesNonNullFields)
{
  std::string cfg = sWriteTempFile(kConfigOneInt);
  std::string v   = sWriteTempFile(
      "function validate_config(c, s, a) error('x') end\n");
  int rc = ConfFileValidate(cfg.c_str(), "core.lua", nullptr, v.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  sRemoveFile(v);
  EXPECT_EQ(rc, 1);
  ASSERT_EQ(g_findings.size(), 1u);
  /* severity must be a recognised token and message must be non-empty */
  EXPECT_EQ(g_findings[0].severity, "error");
  EXPECT_FALSE(g_findings[0].message.empty());
}

/* ════════════════════════════════════════════════════════════════════════
   Group F — end-to-end with the real shipped validator + core schema
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileValidateAdversarialTest, RealValidatorAcceptsValidConfig)
{
  std::string cfg = sWriteTempFile(
      "return { ufsrv = { ufsrv_geogroup = 1, command_console_port = 8080, sys_runtime = {} } }\n");
  std::string validator   = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_validator.lua";
  std::string core_schema = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_ufsrv.lua";
  int rc = ConfFileValidate(cfg.c_str(), core_schema.c_str(), nullptr, validator.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(g_findings.empty());
}

TEST_F(ConfFileValidateAdversarialTest, RealValidatorRejectsMissingRequiredField)
{
  /* ufsrv_geogroup is required in the core schema but omitted here. */
  std::string cfg = sWriteTempFile(
      "return { ufsrv = { command_console_port = 8080, sys_runtime = {} } }\n");
  std::string validator   = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_validator.lua";
  std::string core_schema = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_ufsrv.lua";
  int rc = ConfFileValidate(cfg.c_str(), core_schema.c_str(), nullptr, validator.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("ufsrv_geogroup"), std::string::npos);
}

TEST_F(ConfFileValidateAdversarialTest, RealValidatorRejectsTypeMismatch)
{
  std::string cfg = sWriteTempFile(
      "return { ufsrv = { ufsrv_geogroup = 1, command_console_port = 'not a port', sys_runtime = {} } }\n");
  std::string validator   = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_validator.lua";
  std::string core_schema = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_ufsrv.lua";
  int rc = ConfFileValidate(cfg.c_str(), core_schema.c_str(), nullptr, validator.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("command_console_port"), std::string::npos);
}

/* Shared helper: run ConfFileValidate against the real validator + core schema. */
static int
sRealValidate(const char *config_lua)
{
  std::string cfg         = sWriteTempFile(config_lua);
  std::string validator   = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_validator.lua";
  std::string core_schema = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_ufsrv.lua";
  int rc = ConfFileValidate(cfg.c_str(), core_schema.c_str(), nullptr, validator.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  return rc;
}

TEST_F(ConfFileValidateAdversarialTest, RealValidatorRejectsValueBelowMin)
{
  int rc = sRealValidate("return { ufsrv = { ufsrv_geogroup = 1, command_console_port = 0, sys_runtime = {} } }\n");
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("command_console_port"), std::string::npos);
  EXPECT_NE(g_findings[0].message.find("below minimum"), std::string::npos);
}

TEST_F(ConfFileValidateAdversarialTest, RealValidatorRejectsValueAboveMax)
{
  int rc = sRealValidate("return { ufsrv = { ufsrv_geogroup = 1, command_console_port = 70000, sys_runtime = {} } }\n");
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("command_console_port"), std::string::npos);
  EXPECT_NE(g_findings[0].message.find("above maximum"), std::string::npos);
}

TEST_F(ConfFileValidateAdversarialTest, RealValidatorRejectsNegativeGeogroup)
{
  int rc = sRealValidate("return { ufsrv = { ufsrv_geogroup = -1, command_console_port = 8080, sys_runtime = {} } }\n");
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("ufsrv_geogroup"), std::string::npos);
  EXPECT_NE(g_findings[0].message.find("below minimum"), std::string::npos);
}

TEST_F(ConfFileValidateAdversarialTest, RealValidatorRejectsMissingScope)
{
  int rc = sRealValidate("return {}\n");
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("Missing required field"), std::string::npos);
}

TEST_F(ConfFileValidateAdversarialTest, RealValidatorRejectsNonTableScope)
{
  int rc = sRealValidate("return { ufsrv = 'not a table' }\n");
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("Missing required field"), std::string::npos);
}

TEST_F(ConfFileValidateAdversarialTest, ConfigSyntaxErrorReportsError)
{
  /* Unclosed brace → Lua syntax error → load fails before validation. */
  int rc = sRealValidate("return { ufsrv = { \n");
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].message.c_str(), "failed to load config file");
}

/* Realistic full config (global-assignment style, includes a ufnet scope the
   core schema does not define) — must pass validation against the core schema. */
TEST_F(ConfFileValidateAdversarialTest, RealValidatorAcceptsValidFullFixture)
{
  std::string cfg         = std::string(CMAKE_SOURCE_DIR) + "/tests/lua/fixtures/valid_full.lua";
  std::string validator   = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_validator.lua";
  std::string core_schema = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_ufsrv.lua";
  int rc = ConfFileValidate(cfg.c_str(), core_schema.c_str(), nullptr, validator.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(g_findings.empty());
}

/* The ufnet scope is optional (absent → skipped), but when present its fields
   are validated.  Here db_backend.port has the wrong type. */
TEST_F(ConfFileValidateAdversarialTest, RealValidatorRejectsInvalidUfnetWhenPresent)
{
  std::string cfg = sWriteTempFile(
      "return { ufsrv = { ufsrv_geogroup = 1, command_console_port = 8080, sys_runtime = {} },\n"
      "         ufnet = { db_backend = { port = 'not a number' } } }\n");
  std::string validator   = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_validator.lua";
  std::string core_schema = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_ufsrv.lua";
  int rc = ConfFileValidate(cfg.c_str(), core_schema.c_str(), nullptr, validator.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  sRemoveFile(cfg);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_NE(g_findings[0].message.find("db_backend"), std::string::npos);
}

/* ufnet = {} is valid — every field inside ufnet is optional. */
TEST_F(ConfFileValidateAdversarialTest, RealValidatorAcceptsEmptyUfnet)
{
  int rc = sRealValidate(
      "return { ufsrv = { ufsrv_geogroup = 1, command_console_port = 8080, sys_runtime = {} }, ufnet = {} }\n");
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(g_findings.empty());
}

/* Realistic three-tier ufsrvwebsock config (ufsrv + ufnet + ufsrvwebsock),
   global-assignment style — must validate against the merged core schema
   plus the ufsrvwebsock adjunct schema. */
TEST_F(ConfFileValidateAdversarialTest, RealValidatorAcceptsUfsrvwebsockConfig)
{
  std::string cfg            = std::string(CMAKE_SOURCE_DIR) + "/tests/lua/fixtures/ufsrvwebsock.config.lua";
  std::string validator      = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_validator.lua";
  std::string core_schema    = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_ufsrv.lua";
  std::string adjunct_schema = std::string(CMAKE_SOURCE_DIR) + "/etc/adjunct_schema_ufsrvwebsock.lua";
  int rc = ConfFileValidate(cfg.c_str(), core_schema.c_str(), adjunct_schema.c_str(),
                            validator.c_str(), nullptr, nullptr, sOnFinding, nullptr, 0);
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(g_findings.empty());
}

/* The shipped missing_required.lua fixture. */
TEST_F(ConfFileValidateAdversarialTest, MissingRequiredFixtureRejected)
{
  std::string cfg         = std::string(CMAKE_SOURCE_DIR) + "/tests/lua/fixtures/missing_required.lua";
  std::string validator   = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_validator.lua";
  std::string core_schema = std::string(CMAKE_SOURCE_DIR) + "/etc/core_schema_ufsrv.lua";
  int rc = ConfFileValidate(cfg.c_str(), core_schema.c_str(), nullptr, validator.c_str(),
                            nullptr, nullptr, sOnFinding, nullptr, 0);
  EXPECT_EQ(rc, 1);
  ASSERT_GE(g_findings.size(), 1u);
  EXPECT_STREQ(g_findings[0].message.c_str(), "failed to load config file");
}
