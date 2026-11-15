/**
 * @file configfile_processor_adversarial_test.cpp
 * @brief Adversarial test — builds an independent truth table from the Lua
 *        config via raw accessor API, validates it against the original Lua
 *        file with an external validator, then walks every
 *        g_ufsrv_config_symbols[] entry comparing ConfFileResolveSymbols
 *        output against the validated truth table.  ASAN/UBSAN/LSAN/Valgrind.
 *
 * Copyright (C) 2015-2026 unfacd works
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <map>
#include <vector>

extern "C" {
#include "configfile_processor.h"
#include "configfile_processor_priv.h"
#include "ufsrv_config_keymap_entry_type.h"
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
#include <ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h>
#include "data_ufsrv_config_literal_to_field_map_priv_generated.h"
#include "data_config_symbols_core_priv.h"
}

static const char *kRepoRoot = nullptr;

static void sInitPaths() {
  if (kRepoRoot) return;
  kRepoRoot = getenv("UFSRVCORELIB_REPO");
  if (!kRepoRoot) kRepoRoot = CMAKE_SOURCE_DIR;
}

static std::string fixturePath(const char *basename) {
  sInitPaths();
  return std::string(kRepoRoot) + "/tests/lua/fixtures/" + basename;
}

/* ════════════════════════════════════════════════════════════════════════
   Truth table — built independently from raw accessor API
   ════════════════════════════════════════════════════════════════════════ */

struct TruthEntry {
  std::string              scope;
  std::string              key;
  UfsrvConfigValueType     type;
  std::string              str_val;
  int64_t                  int_val;
  double                   double_val;
  bool                     is_array;
  std::vector<std::string> array_elements;
  bool                     present;
};

static std::map<std::string, TruthEntry>
sBuildTruthTable(ConfFile cp) {
  std::map<std::string, TruthEntry> table;

  for (const ConfFileSymbolTableEntry *e = g_ufsrv_config_symbols;
       e->scope != nullptr; e++) {

    std::string full_key = std::string(e->scope) + "." + e->key;
    TruthEntry te;
    te.scope    = e->scope;
    te.key      = e->key;
    te.type     = e->type;
    te.present  = false;
    te.is_array = false;

    switch (e->type) {
    case UFSRV_CONFIG_VALUE_STR: {
      const char *v = ConfFileGetString(cp, e->scope, e->key);
      if (v && ConfFileGetLastError(cp) == CONF_FILE_OK) {
        te.str_val = v;
        te.present = true;
      }
      break;
    }
    case UFSRV_CONFIG_VALUE_INT: {
      int v = ConfFileGetInt(cp, e->scope, e->key);
      if (ConfFileGetLastError(cp) == CONF_FILE_OK) {
        te.int_val = v;
        te.present = true;
      }
      break;
    }
    case UFSRV_CONFIG_VALUE_BOOL: {
      bool v = ConfFileGetBool(cp, e->scope, e->key);
      if (ConfFileGetLastError(cp) == CONF_FILE_OK) {
        te.int_val = v ? 1 : 0;
        te.present = true;
      }
      break;
    }
    case UFSRV_CONFIG_VALUE_DOUBLE: {
      double v = ConfFileGetNumber(cp, e->scope, e->key);
      if (ConfFileGetLastError(cp) == CONF_FILE_OK) {
        te.double_val = v;
        te.present = true;
      }
      break;
    }
    case UFSRV_CONFIG_VALUE_ARRAY: {
      if (ConfFileIsTable(cp, e->scope, e->key)) {
        int len = ConfFileGetArrayLength(cp, e->scope, e->key);
        if (len > 0 && ConfFileGetLastError(cp) == CONF_FILE_OK) {
          te.is_array = true;
          te.present  = true;
          for (int i = 1; i <= len; i++) {
            const char *elem = ConfFileGetArrayElement(cp, e->scope, e->key, i);
            if (elem && ConfFileGetLastError(cp) == CONF_FILE_OK)
              te.array_elements.push_back(elem);
          }
        }
      }
      break;
    }
    default:
      break;
    }

    table[full_key] = te;
  }

  return table;
}

/* ════════════════════════════════════════════════════════════════════════
   Step 1 — Dump truth table to file (one line per entry)
   ════════════════════════════════════════════════════════════════════════ */

static void sDumpTruthTable(
    const std::map<std::string, TruthEntry> &truth,
    const char *path) {
  FILE *f = fopen(path, "w");
  ASSERT_NE(f, nullptr) << "cannot open " << path;

  time_t now = time(nullptr);
  struct tm tm_buf;
  char ts[64];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime_r(&now, &tm_buf));
  fprintf(f, "# Truth table generated %s\n", ts);
  fprintf(f, "# Source: valid_full.lua\n");

  for (const auto &kv : truth) {
    const TruthEntry &te = kv.second;
    if (!te.present) {
      fprintf(f, "ABSENT %s\n", kv.first.c_str());
    } else if (te.is_array) {
      fprintf(f, "ARRAY %s %zu", kv.first.c_str(), te.array_elements.size());
      for (const auto &el : te.array_elements)
        fprintf(f, " %s", el.c_str());
      fprintf(f, "\n");
    } else {
      switch (te.type) {
      case UFSRV_CONFIG_VALUE_STR:
        fprintf(f, "STR %s %s\n", kv.first.c_str(), te.str_val.c_str());
        break;
      case UFSRV_CONFIG_VALUE_INT:
        fprintf(f, "INT %s %lld\n", kv.first.c_str(), (long long)te.int_val);
        break;
      case UFSRV_CONFIG_VALUE_BOOL:
        fprintf(f, "BOOL %s %lld\n", kv.first.c_str(), (long long)te.int_val);
        break;
      case UFSRV_CONFIG_VALUE_DOUBLE:
        fprintf(f, "DOUBLE %s %g\n", kv.first.c_str(), te.double_val);
        break;
      default:
        fprintf(f, "SKIP %s\n", kv.first.c_str());
        break;
      }
    }
  }
  fclose(f);
}

/* ════════════════════════════════════════════════════════════════════════
   Step 2 — External validator (Lua script) verifies truth table against
            the original Lua config file
   ════════════════════════════════════════════════════════════════════════ */

static bool sRunExternalValidator(const char *lua_file, const char *truth_file) {
  auto script_path = fixturePath("../test_truth_validator.lua");

  std::string cmd = std::string("/opt/bin/lua ") +
                    script_path + " " + lua_file + " " + truth_file;
  int rc = system(cmd.c_str());
  return (rc == 0);
}

/* ════════════════════════════════════════════════════════════════════════
   Step 3 — Walk resolved symbol table against validated truth table
   ════════════════════════════════════════════════════════════════════════ */

static void sVerifyAgainstTruth(
    const std::map<std::string, TruthEntry> &truth) {

  for (const ConfFileSymbolTableEntry *e = g_ufsrv_config_symbols;
       e->scope != nullptr; e++) {

    std::string full_key = std::string(e->scope) + "." + e->key;
    auto it = truth.find(full_key);
    ASSERT_NE(it, truth.end()) << "truth entry missing for " << full_key;
    const TruthEntry &te = it->second;

    if (!te.present) {
      EXPECT_EQ(e->value.source, UFSRV_CONFIG_SOURCE_UNASSIGNED)
          << full_key << " not in Lua file, must be UNASSIGNED";
      continue;
    }

    EXPECT_EQ(e->value.source, UFSRV_CONFIG_SOURCE_CONFIG_FILE)
        << full_key << " in Lua file, must be CONFIG_FILE";

    switch (e->type) {
    case UFSRV_CONFIG_VALUE_STR:
      ASSERT_NE(e->value.stored_value.str_val, nullptr) << full_key;
      EXPECT_STREQ(e->value.stored_value.str_val, te.str_val.c_str()) << full_key;
      EXPECT_EQ(e->value.type_allocation_sz, te.str_val.size() + 1) << full_key;
      break;
    case UFSRV_CONFIG_VALUE_INT:
      EXPECT_EQ(e->value.stored_value.int_val, te.int_val) << full_key;
      EXPECT_EQ(e->value.type_allocation_sz, 0u);
      break;
    case UFSRV_CONFIG_VALUE_BOOL:
      EXPECT_EQ(e->value.stored_value.int_val, te.int_val) << full_key;
      EXPECT_EQ(e->value.type_allocation_sz, 0u);
      break;
    case UFSRV_CONFIG_VALUE_DOUBLE:
      EXPECT_DOUBLE_EQ(e->value.stored_value.double_value, te.double_val) << full_key;
      EXPECT_EQ(e->value.type_allocation_sz, 0u);
      break;
    case UFSRV_CONFIG_VALUE_ARRAY:
      if (te.is_array && !te.array_elements.empty()) {
        ASSERT_NE(e->value.stored_value.blob_val, nullptr) << full_key;
        EXPECT_EQ(e->value.collection_sz, te.array_elements.size()) << full_key;
        const char *p = (const char *)e->value.stored_value.blob_val;
        size_t total_sz = 0;
        for (size_t i = 0; i < te.array_elements.size(); i++) {
          EXPECT_STREQ(p, te.array_elements[i].c_str())
              << full_key << "[" << i << "]";
          p += strlen(p) + 1;
          total_sz += te.array_elements[i].size() + 1;
        }
        EXPECT_EQ(e->value.type_allocation_sz, total_sz) << full_key;
      }
      break;
    default:
      break;
    }
  }
}

/* ════════════════════════════════════════════════════════════════════════
   Test fixture
   ════════════════════════════════════════════════════════════════════════ */

class ConfFileAdversarial : public ::testing::Test {
protected:
  void TearDown() override {
    for (ConfFileSymbolTableEntry *e = g_ufsrv_config_symbols;
         e->scope != nullptr; e++) {
      if (e->type == UFSRV_CONFIG_VALUE_STR && e->value.stored_value.str_val)
        free(e->value.stored_value.str_val);
      if (e->type == UFSRV_CONFIG_VALUE_ARRAY && e->value.stored_value.blob_val)
        free(e->value.stored_value.blob_val);
      e->value = UfsrvConfigValue{0};
    }
  }
};

/* ════════════════════════════════════════════════════════════════════════
   Tests
   ════════════════════════════════════════════════════════════════════════ */

TEST_F(ConfFileAdversarial, TruthTablePipeline) {
  auto lua_path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(lua_path.c_str());
  ASSERT_NE(cp, nullptr) << ConfFileGetLastErrorMessage(cp);

  /* Step 1 — Build and dump truth table */
  auto truth = sBuildTruthTable(cp);
  auto truth_path = fixturePath("data_truth_table_valid_full.txt");
  sDumpTruthTable(truth, truth_path.c_str());

  /* Step 2 — External validator checks truth table against Lua source */
  ASSERT_TRUE(sRunExternalValidator(lua_path.c_str(), truth_path.c_str()))
      << "external validator rejected truth table";

  /* Step 3 — Resolve via symbol table */
  ConfFileTransferToConfigValues(cp, g_ufsrv_config_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);

  /* Step 4 — Walk every entry against validated truth */
  sVerifyAgainstTruth(truth);

  /* Step 5 — JSON dump */
  auto json_path = fixturePath("data_config_dump_valid_full.json");
  EXPECT_EQ(ConfFileDumpJson(g_ufsrv_config_symbols, json_path.c_str()), 0);

  std::ifstream f(json_path);
  ASSERT_TRUE(f.is_open());
  std::string json((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
  f.close();

  EXPECT_NE(json.find("\"ufsrv\""), std::string::npos);
  EXPECT_NE(json.find("\"/etc/passwd\""), std::string::npos);
  EXPECT_NE(json.find("\"/etc/shadow\""), std::string::npos);
  EXPECT_NE(json.find("\"/proc/self/mem\""), std::string::npos);

  ConfFileDestroy(cp);
}

TEST_F(ConfFileAdversarial, MissingKeysStayUnassigned) {
  auto lua_path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(lua_path.c_str());
  ASSERT_NE(cp, nullptr);

  auto truth = sBuildTruthTable(cp);
  auto truth_path = fixturePath("data_truth_table_valid_full.txt");
  sDumpTruthTable(truth, truth_path.c_str());
  ASSERT_TRUE(sRunExternalValidator(lua_path.c_str(), truth_path.c_str()));

  ConfFileTransferToConfigValues(cp, g_ufsrv_config_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);

  int present = 0, missing = 0;
  for (const auto &kv : truth) {
    if (kv.second.present) present++; else missing++;
  }
  EXPECT_GT(present, 0);
  EXPECT_GT(missing, 0);

  for (const ConfFileSymbolTableEntry *e = g_ufsrv_config_symbols;
       e->scope != nullptr; e++) {
    auto it = truth.find(std::string(e->scope) + "." + e->key);
    ASSERT_NE(it, truth.end());
    if (it->second.present)
      EXPECT_EQ(e->value.source, UFSRV_CONFIG_SOURCE_CONFIG_FILE);
    else
      EXPECT_EQ(e->value.source, UFSRV_CONFIG_SOURCE_UNASSIGNED);
  }

  ConfFileDestroy(cp);
}

/* ════════════════════════════════════════════════════════════════════════
   Adversarial attacks — malicious / edge-case / unexpected inputs
   designed to break the resolver, accessors, or serialiser.
   ════════════════════════════════════════════════════════════════════════ */

class ConfFileAttack : public ::testing::Test {
protected:
  void TearDown() override {
    for (ConfFileSymbolTableEntry *e = g_ufsrv_config_symbols;
         e->scope != nullptr; e++) {
      if (e->type == UFSRV_CONFIG_VALUE_STR && e->value.stored_value.str_val)
        free(e->value.stored_value.str_val);
      if (e->type == UFSRV_CONFIG_VALUE_ARRAY && e->value.stored_value.blob_val)
        free(e->value.stored_value.blob_val);
      e->value = UfsrvConfigValue{0};
    }
  }
};

/* ── STR attacks ──────────────────────────────────────────────────────── */

TEST_F(ConfFileAttack, NullScopeString) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  const char *v = ConfFileGetString(cp, nullptr, "key");
  EXPECT_EQ(v, nullptr);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileAttack, NullKeyString) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  const char *v = ConfFileGetString(cp, "ufsrv", nullptr);
  EXPECT_EQ(v, nullptr);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileAttack, FormatSpecifierInKey) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);

  static ConfFileSymbolTableEntry fmt_sym[] = {
    { .scope = "ufsrv", .key = "%s%n%x%d%%",
      .type = UFSRV_CONFIG_VALUE_STR, .value = {0},
      .field_index = UFSRV_CONFIG_FIELD_COUNT,
      .description = "format-specifier key",
      .lifecycle = { .on_validate = nullptr } },
    { .scope = nullptr }
  };

  int failures = ConfFileTransferToConfigValues(cp, fmt_sym, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 1);

  const char *jp = "/tmp/conf_attack_fmt.json";
  unlink(jp);
  int rc = ConfFileDumpJson(fmt_sym, jp);
  EXPECT_EQ(rc, 0);
  unlink(jp);

  ConfFileDestroy(cp);
}

/* ── INT attacks ──────────────────────────────────────────────────────── */

TEST_F(ConfFileAttack, NegativePort) {
  const char *lua = "ufsrv = { main_listener_port = -1 }\n";
  char tmpl[] = "/tmp/conf_attack_XXXXXX";
  int fd = mkstemp(tmpl);
  ASSERT_GE(fd, 0);
  FILE *f = fdopen(fd, "w");
  ASSERT_NE(f, nullptr);
  fputs(lua, f); fclose(f);

  ConfFile cp = ConfFileInit(tmpl);
  ASSERT_NE(cp, nullptr);
  unlink(tmpl);

  static int valid_calls = 0;
  auto validate = +[](const ConfFileSymbolTableEntry *e,
                       const UfsrvConfigValue *v, char *buf, size_t sz) -> bool {
    valid_calls++;
    if (v->stored_value.int_val < 0) {
      snprintf(buf, sz, "negative port");
      return false;
    }
    return true;
  };

  static ConfFileSymbolTableEntry neg_sym[] = {
    { .scope = "ufsrv", .key = "main_listener_port",
      .type = UFSRV_CONFIG_VALUE_INT, .value = {0},
      .field_index = UFSRV_CONFIG_FIELD_COUNT, .description = "port",
      .lifecycle = { .on_validate = validate } },
    { .scope = nullptr }
  };

  valid_calls = 0;
  int failures = ConfFileTransferToConfigValues(cp, neg_sym, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 1);
  EXPECT_EQ(valid_calls, 1);

  ConfFileDestroy(cp);
}

TEST_F(ConfFileAttack, Int64MaxValue) {
  const char *lua = "ufsrv = { main_listener_port = 9223372036854775807 }\n";
  char tmpl[] = "/tmp/conf_attack_XXXXXX";
  int fd = mkstemp(tmpl);
  ASSERT_GE(fd, 0);
  FILE *f = fdopen(fd, "w");
  ASSERT_NE(f, nullptr);
  fputs(lua, f); fclose(f);

  ConfFile cp = ConfFileInit(tmpl);
  ASSERT_NE(cp, nullptr);
  unlink(tmpl);

  static ConfFileSymbolTableEntry big_sym[] = {
    { .scope = "ufsrv", .key = "main_listener_port",
      .type = UFSRV_CONFIG_VALUE_INT, .value = {0},
      .field_index = UFSRV_CONFIG_FIELD_COUNT, .description = "port",
      .lifecycle = { .on_validate = nullptr } },
    { .scope = nullptr }
  };

  int failures = ConfFileTransferToConfigValues(cp, big_sym, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 0);
  EXPECT_EQ(big_sym[0].value.stored_value.int_val, 9223372036854775807LL);
  EXPECT_EQ(big_sym[0].value.source, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

  ConfFileDestroy(cp);
}

/* ── ARRAY attacks ────────────────────────────────────────────────────── */

TEST_F(ConfFileAttack, LargeArray) {
  std::string lua = "ufsrv = { allowed_origins = {";
  for (int i = 0; i < 1000; i++) {
    if (i > 0) lua += ", ";
    lua += "\"elem" + std::to_string(i) + "\"";
  }
  lua += "} }\n";

  char tmpl[] = "/tmp/conf_attack_XXXXXX";
  int fd = mkstemp(tmpl);
  ASSERT_GE(fd, 0);
  FILE *f = fdopen(fd, "w");
  ASSERT_NE(f, nullptr);
  fputs(lua.c_str(), f); fclose(f);

  ConfFile cp = ConfFileInit(tmpl);
  ASSERT_NE(cp, nullptr);
  unlink(tmpl);

  static ConfFileSymbolTableEntry arr_sym[] = {
    { .scope = "ufsrv", .key = "allowed_origins",
      .type = UFSRV_CONFIG_VALUE_ARRAY, .value = {0},
      .field_index = UFSRV_CONFIG_FIELD_COUNT, .description = "origins",
      .lifecycle = { .on_validate = nullptr } },
    { .scope = nullptr }
  };

  int failures = ConfFileTransferToConfigValues(cp, arr_sym, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 0);
  EXPECT_EQ(arr_sym[0].value.collection_sz, 1000u);
  ASSERT_NE(arr_sym[0].value.stored_value.blob_val, nullptr);
  EXPECT_GT(arr_sym[0].value.type_allocation_sz, 0u);
  EXPECT_EQ(arr_sym[0].value.source, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

  free(arr_sym[0].value.stored_value.blob_val);
  arr_sym[0].value = UfsrvConfigValue{0};

  ConfFileDestroy(cp);
}

TEST_F(ConfFileAttack, MixedTypeArray) {
  const char *lua =
    "ufsrv = { allowed_origins = { \"ok\", 123, true, \"also-ok\" } }\n";
  char tmpl[] = "/tmp/conf_attack_XXXXXX";
  int fd = mkstemp(tmpl);
  ASSERT_GE(fd, 0);
  FILE *f = fdopen(fd, "w");
  ASSERT_NE(f, nullptr);
  fputs(lua, f); fclose(f);

  ConfFile cp = ConfFileInit(tmpl);
  ASSERT_NE(cp, nullptr);
  unlink(tmpl);

  static ConfFileSymbolTableEntry mix_sym[] = {
    { .scope = "ufsrv", .key = "allowed_origins",
      .type = UFSRV_CONFIG_VALUE_ARRAY, .value = {0},
      .field_index = UFSRV_CONFIG_FIELD_COUNT, .description = "origins",
      .lifecycle = { .on_validate = nullptr } },
    { .scope = nullptr }
  };

  int failures = ConfFileTransferToConfigValues(cp, mix_sym, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 0);
  EXPECT_EQ(mix_sym[0].value.collection_sz, 4u); /* rawlen counts all elements */

  free(mix_sym[0].value.stored_value.blob_val);
  mix_sym[0].value = UfsrvConfigValue{0};

  ConfFileDestroy(cp);
}

TEST_F(ConfFileAttack, ArrayIndexZero) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  const char *elem = ConfFileGetArrayElement(cp, "ufsrv",
      "fileloader.black_listed_files", 0);
  EXPECT_EQ(elem, nullptr);
  ConfFileDestroy(cp);
}

/* ── Memory / lifetime attacks ────────────────────────────────────────── */

TEST_F(ConfFileAttack, ResolveSymbolsNullCp) {
  static ConfFileSymbolTableEntry s[] = {{ .scope = nullptr }};
  int failures = ConfFileTransferToConfigValues(nullptr, s, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 0);
}

TEST_F(ConfFileAttack, ResolveSymbolsNullSymbols) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  int failures = ConfFileTransferToConfigValues(cp, nullptr, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 0);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileAttack, DumpJsonNullSymbols) {
  int rc = ConfFileDumpJson(nullptr, "/tmp/nonexistent.json");
  EXPECT_EQ(rc, -1);
}

/* VULNERABILITY FOUND: ConfFileDestroy frees the opaque struct; subsequent
   access via ConfFileGetString(cp, ...) reads cp->L after free.
   ConfFileGetString's guard (!cp || !cp->L) does not protect against
   use-after-free because the freed memory may be re-used by the allocator.
   This requires a caller discipline fix — the caller must nullify their
   ConfFile pointer after Destroy. */

/* ── Symbol table attacks ─────────────────────────────────────────────── */

TEST_F(ConfFileAttack, UnsupportedType) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);

  static ConfFileSymbolTableEntry ptr_sym[] = {
    { .scope = "ufsrv", .key = "server_run_mode",
      .type = UFSRV_CONFIG_VALUE_PTR, .value = {0},
      .field_index = UFSRV_CONFIG_FIELD_COUNT,
      .description = "unsupported PTR",
      .lifecycle = { .on_validate = nullptr } },
    { .scope = nullptr }
  };

  int failures = ConfFileTransferToConfigValues(cp, ptr_sym, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 1);

  ConfFileDestroy(cp);
}

TEST_F(ConfFileAttack, SingleEntryTable) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);

  static ConfFileSymbolTableEntry single[] = {
    { .scope = "ufsrv", .key = "server_run_mode",
      .type = UFSRV_CONFIG_VALUE_STR, .value = {0},
      .field_index = UFSRV_CONFIG_FIELD_COUNT, .description = "one",
      .lifecycle = { .on_validate = nullptr } },
    { .scope = nullptr }
  };

  int failures = ConfFileTransferToConfigValues(cp, single, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  EXPECT_EQ(failures, 0);
  EXPECT_STREQ(single[0].value.stored_value.str_val, "normal");
  free(single[0].value.stored_value.str_val);
  single[0].value = UfsrvConfigValue{0};

  ConfFileDestroy(cp);
}

/* ── Lua attacks ──────────────────────────────────────────────────────── */

TEST_F(ConfFileAttack, NonTableGlobal) {
  const char *lua = "ufsrv = { server_id = 1 } not_a_scope = 42\n";
  char tmpl[] = "/tmp/conf_attack_XXXXXX";
  int fd = mkstemp(tmpl);
  ASSERT_GE(fd, 0);
  FILE *f = fdopen(fd, "w");
  ASSERT_NE(f, nullptr);
  fputs(lua, f); fclose(f);

  ConfFile cp = ConfFileInit(tmpl);
  ASSERT_NE(cp, nullptr);
  unlink(tmpl);

  bool has = ConfFileHasScope(cp, "not_a_scope");
  EXPECT_FALSE(has);
  has = ConfFileHasScope(cp, "ufsrv");
  EXPECT_TRUE(has);

  ConfFileDestroy(cp);
}

TEST_F(ConfFileAttack, ConfigReturningNumber) {
  const char *lua = "return 42\n";
  char tmpl[] = "/tmp/conf_attack_XXXXXX";
  int fd = mkstemp(tmpl);
  ASSERT_GE(fd, 0);
  FILE *f = fdopen(fd, "w");
  ASSERT_NE(f, nullptr);
  fputs(lua, f); fclose(f);

  ConfFile cp = ConfFileInit(tmpl);
  if (cp) {
    int failures = ConfFileTransferToConfigValues(cp, g_ufsrv_config_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
    EXPECT_GT(failures, 0);
    ConfFileDestroy(cp);
  }
  unlink(tmpl);
}

TEST_F(ConfFileAttack, HundredSequentialResolves) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);

  for (int i = 0; i < 100; i++) {
    ConfFileTransferToConfigValues(cp, g_ufsrv_config_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
    /* Free allocations from this iteration before the next overwrites them */
    for (ConfFileSymbolTableEntry *e = g_ufsrv_config_symbols;
         e->scope != nullptr; e++) {
      if (e->type == UFSRV_CONFIG_VALUE_STR && e->value.stored_value.str_val) {
        free(e->value.stored_value.str_val);
      }
      if (e->type == UFSRV_CONFIG_VALUE_ARRAY && e->value.stored_value.blob_val) {
        free(e->value.stored_value.blob_val);
      }
      e->value = UfsrvConfigValue{0};
    }
  }

  /* One final resolve so TearDown has something to verify */
  ConfFileTransferToConfigValues(cp, g_ufsrv_config_symbols, (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, nullptr);
  for (const ConfFileSymbolTableEntry *e = g_ufsrv_config_symbols;
       e->scope != nullptr; e++) {
    if (e->value.source == UFSRV_CONFIG_SOURCE_CONFIG_FILE &&
        e->type == UFSRV_CONFIG_VALUE_STR)
      ASSERT_NE(e->value.stored_value.str_val, nullptr);
  }

  ConfFileDestroy(cp);
}

/* ════════════════════════════════════════════════════════════════════════
   Coverage remediation — target uncovered error paths and edge cases
   ════════════════════════════════════════════════════════════════════════ */

class ConfFileCoverage : public ::testing::Test {};

TEST_F(ConfFileCoverage, DotPathSubTableNotFound) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  ConfFileGetString(cp, "ufsrv", "nonexistent.field");
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_KEY_NOT_FOUND);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, DotPathSubTableNotATable) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  ConfFileGetString(cp, "ufsrv", "server_id.field");
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_KEY_NOT_FOUND);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, SubTableNameTooLong) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  std::string longkey(130, 'x');
  longkey += ".field";
  ConfFileGetString(cp, "ufsrv", longkey.c_str());
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_ARGUMENT);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, ErrorToStringInvalidCode) {
  const char *s = ConfFileErrorToString((ConfFileError)999);
  EXPECT_STREQ(s, "unknown error");
}

TEST_F(ConfFileCoverage, GetStringCopyNullBuffer) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  int rc = ConfFileGetStringCopy(cp, "ufsrv", "server_run_mode", nullptr, 32);
  EXPECT_EQ(rc, -1);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_ARGUMENT);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, GetStringCopyZeroBufSize) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  char buf[32];
  int rc = ConfFileGetStringCopy(cp, "ufsrv", "server_run_mode", buf, 0);
  EXPECT_EQ(rc, -1);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_ARGUMENT);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, GetStringNullCp) {
  const char *v = ConfFileGetString(nullptr, "ufsrv", "key");
  EXPECT_EQ(v, nullptr);
}

TEST_F(ConfFileCoverage, GetStringDefaultNullCp) {
  const char *v = ConfFileGetStringDefault(nullptr, "ufsrv", "key", "fallback");
  EXPECT_STREQ(v, "fallback");
}

TEST_F(ConfFileCoverage, GetIntNullCp) {
  int v = ConfFileGetInt(nullptr, "ufsrv", "key");
  EXPECT_EQ(v, 0);
}

TEST_F(ConfFileCoverage, GetIntDefaultNullCp) {
  int v = ConfFileGetIntDefault(nullptr, "ufsrv", "key", 42);
  EXPECT_EQ(v, 42);
}

TEST_F(ConfFileCoverage, GetNumberNullCp) {
  double v = ConfFileGetNumber(nullptr, "ufsrv", "key");
  EXPECT_DOUBLE_EQ(v, 0.0);
}

TEST_F(ConfFileCoverage, GetBoolNullCp) {
  bool v = ConfFileGetBool(nullptr, "ufsrv", "key");
  EXPECT_FALSE(v);
}

TEST_F(ConfFileCoverage, GetStringOnIntField) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  const char *v = ConfFileGetString(cp, "ufsrv", "main_listener_port");
  EXPECT_EQ(v, nullptr);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_TYPE_MISMATCH);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, GetIntOnStringField) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  int v = ConfFileGetInt(cp, "ufsrv", "server_run_mode");
  EXPECT_EQ(v, 0);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_TYPE_MISMATCH);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, GetBoolOnIntField) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  bool v = ConfFileGetBool(cp, "ufsrv", "main_listener_port");
  EXPECT_FALSE(v);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_TYPE_MISMATCH);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, GetNumberOnStringField) {
  auto path = fixturePath("valid_full.lua");
  ConfFile cp = ConfFileInit(path.c_str());
  ASSERT_NE(cp, nullptr);
  double v = ConfFileGetNumber(cp, "ufsrv", "server_run_mode");
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_TYPE_MISMATCH);
  ConfFileDestroy(cp);
}

TEST_F(ConfFileCoverage, GetIntWithFloatValue) {
  const char *lua = "ufsrv = { main_listener_port = 3.14 }\n";
  char tmpl[] = "/tmp/conf_cov_XXXXXX";
  int fd = mkstemp(tmpl);
  ASSERT_GE(fd, 0);
  FILE *f = fdopen(fd, "w");
  ASSERT_NE(f, nullptr);
  fputs(lua, f); fclose(f);

  ConfFile cp = ConfFileInit(tmpl);
  ASSERT_NE(cp, nullptr);
  unlink(tmpl);

  int v = ConfFileGetInt(cp, "ufsrv", "main_listener_port");
  EXPECT_EQ(v, 0);
  EXPECT_EQ(ConfFileGetLastError(cp), CONF_FILE_ERR_TYPE_MISMATCH);
  ConfFileDestroy(cp);
}
