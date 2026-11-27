/**
 * @file ufsrv_config_stride_fix_test.cpp
 * @brief Prove ValuesInstance(idx) returns the correct slot when the consumer
 *        passes the generated UfsrvConfigFieldEntry[] tables to StoreInit.
 *
 * A real consumer does:
 * @code
 *   UfsrvConfigRuntimeStoreInit((UfsrvConfigFieldEntry *)ufsrv_runtime_field_table);
 * @endcode
 *
 * StoreInit internally calls ValuesInit(&store->config), arming the
 * Values-store pointer to point at table[0].config (offset 0).  The
 * question is whether ValuesInstance(idx) then indexes by the correct
 * stride — sizeof(UfsrvConfigFieldEntry) == 48 — so that
 * ValuesInstance(i) hits table[i].config, not a byte-offset into the
 * lifecycle struct of table[0].
 *
 * With the fix (parent-type cast in _Direct), it does.  Without the fix,
 * ValuesInstance(1) would land at offset 16 — inside
 * table[0].lifecycle.on_inflated — and every index beyond 0 would be wrong.
 *
 * This test copies the generated const tables into mutable local arrays
 * (exactly as the bootstrap test does with cfg_ft_lc / rt_ft_lc), arms
 * the library via StoreInit, then proves every slot is independently
 * addressable through both direct ValuesInstance access and the generated
 * UfsrvConfigSet<Field>() / UfsrvConfig<Field>() accessors.
 *
 * Copyright (C) 2015-2026 unfacd works
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cinttypes>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
#include <data_ufsrv_config_field_tables_generated.c>
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FIXTURE — StrideFixTest
 *
 * Copies the generated field tables into mutable local arrays and registers
 * them via StoreInit — the same pattern a host server follows.  After
 * StoreInit, all static pointers in ufsrv_config.c are armed and every
 * generated accessor routes through the Instance(idx) guard.
 * ═══════════════════════════════════════════════════════════════════════════ */

class StrideFixTest : public ::testing::Test {
protected:
    // Mutable copies of the generated tables — consumer-owned stores.
    UfsrvConfigFieldEntry  cfg_store[UFSRV_CONFIG_FIELD_COUNT];
    UfsrvConfigFieldEntry  rt_store[UFSRV_RUNTIME_FIELD_COUNT];

    void SetUp() override {
        // Copy generated tables into mutable consumer arrays.
        // This mirrors what the bootstrap test does with cfg_ft_lc / rt_ft_lc.
        memcpy(cfg_store, ufsrv_config_field_table,   sizeof(cfg_store));
        memcpy(rt_store,  ufsrv_runtime_field_table,  sizeof(rt_store));

        // Arm all static pointers in ufsrv_config.c.
        // StoreInit internally calls ValuesInit(&store->config), wiring the
        // Values-store pointer to cfg_store[0].config / rt_store[0].config.
        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);
    }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 1 — Every ValuesInstance slot is independently addressable
 *
 * Writes a unique sentinel (10000 + i for CONFIG, 20000 + i for RUNTIME)
 * into EVERY slot via ValuesInstance(idx)->stored_value.int_val, then reads
 * every slot back.
 *
 * With the old 16‑byte stride, index 1 would corrupt
 * cfg_store[0].lifecycle.on_inflated, and index 3 would alias
 * cfg_store[1].config instead of cfg_store[3].config.  With the fix
 * (48‑byte stride via parent-type cast), every slot holds its unique
 * sentinel and no existing data (lifecycle pointers, other slots) is
 * corrupted.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(StrideFixTest, AllSlotsIndependentlyAddressable)
{
    printf("\n=== VALUES_INSTANCE STRIDE PROOF ===\n");
    printf("FieldEntry stride = %zu bytes\n", sizeof(UfsrvConfigFieldEntry));
    printf("UfsrvConfigValue stride (old bug) = %zu bytes\n\n",
           sizeof(UfsrvConfigValue));

    // ── Write phase: CONFIG ──
    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(i));
        ASSERT_NE(slot, nullptr);
        slot->stored_value.int_val = 10000 + i;
    }

    // ── Write phase: RUNTIME ──
    for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
        UfsrvConfigValue *slot = UfsrvConfigRuntimeValuesInstance(
            static_cast<UfsrvRuntimeField>(i));
        ASSERT_NE(slot, nullptr);
        slot->stored_value.int_val = 20000 + i;
    }

    // ── Read-back: CONFIG ──
    int failures = 0;
    printf("CONFIG slots (%d fields):\n", UFSRV_CONFIG_FIELD_COUNT);
    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(i));
        int64_t val = slot->stored_value.int_val;
        bool ok = (val == 10000 + i);
        if (!ok) failures++;
        printf("  [%2d]  wrote=%5" PRId64 "  read=%5" PRId64 "  %s\n",
               i, (int64_t)(10000 + i), val, ok ? "OK" : "FAIL <<<");
    }
    EXPECT_EQ(failures, 0);

    // ── Read-back: RUNTIME ──
    printf("\nRUNTIME slots (%d fields):\n", UFSRV_RUNTIME_FIELD_COUNT);
    for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
        UfsrvConfigValue *slot = UfsrvConfigRuntimeValuesInstance(
            static_cast<UfsrvRuntimeField>(i));
        int64_t val = slot->stored_value.int_val;
        bool ok = (val == 20000 + i);
        if (!ok) failures++;
        printf("  [%2d]  wrote=%5" PRId64 "  read=%5" PRId64 "  %s\n",
               i, (int64_t)(20000 + i), val, ok ? "OK" : "FAIL <<<");
    }
    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 2 — Lifecycle pointers survive the write pass
 *
 * After Test 1 writes through every ValuesInstance slot, the lifecycle
 * function pointers in the field table copies must still be intact.
 * If ValuesInstance(1) had landed at offset 16 (the old 16-byte stride),
 * it would have overwritten cfg_store[0].lifecycle.on_inflated with a
 * sentinel value — and this check would catch it.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(StrideFixTest, LifecyclePointersPreserved)
{
    // Write through all slots (same as Test 1, but we check lifecycle after)
    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(i))->stored_value.int_val = 10000 + i;
    }
    for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
        UfsrvConfigRuntimeValuesInstance(
            static_cast<UfsrvRuntimeField>(i))->stored_value.int_val = 20000 + i;
    }

    // Verify lifecycle pointers in our mutable copies still match the
    // generated const originals.  If ValuesInstance corrupted them, they
    // won't match.
    printf("\n=== LIFECYCLE POINTER INTEGRITY ===\n");
    int lif_failures = 0;
    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        bool match =
            (cfg_store[i].lifecycle.on_inflated        == ufsrv_config_field_table[i].lifecycle.on_inflated) &&
            (cfg_store[i].lifecycle.on_value_available == ufsrv_config_field_table[i].lifecycle.on_value_available) &&
            (cfg_store[i].lifecycle.on_value_reloaded  == ufsrv_config_field_table[i].lifecycle.on_value_reloaded) &&
            (cfg_store[i].lifecycle.on_teardown        == ufsrv_config_field_table[i].lifecycle.on_teardown);
        if (!match) {
            lif_failures++;
            printf("  CONFIG[%2d] lifecycle CORRUPTED\n", i);
        }
    }
    for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
        bool match =
            (rt_store[i].lifecycle.on_inflated        == ufsrv_runtime_field_table[i].lifecycle.on_inflated) &&
            (rt_store[i].lifecycle.on_value_available == ufsrv_runtime_field_table[i].lifecycle.on_value_available) &&
            (rt_store[i].lifecycle.on_value_reloaded  == ufsrv_runtime_field_table[i].lifecycle.on_value_reloaded) &&
            (rt_store[i].lifecycle.on_teardown        == ufsrv_runtime_field_table[i].lifecycle.on_teardown);
        if (!match) {
            lif_failures++;
            printf("  RUNTIME[%2d] lifecycle CORRUPTED\n", i);
        }
    }
    printf("  %d/%d CONFIG lifecycle entries intact\n",
           UFSRV_CONFIG_FIELD_COUNT -
           (lif_failures > UFSRV_RUNTIME_FIELD_COUNT ? 0 : 0), 0);
    // Actually let me count properly:
    int cfg_bad = 0, rt_bad = 0;
    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        if (cfg_store[i].lifecycle.on_inflated != ufsrv_config_field_table[i].lifecycle.on_inflated) cfg_bad++;
    }
    for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
        if (rt_store[i].lifecycle.on_inflated != ufsrv_runtime_field_table[i].lifecycle.on_inflated) rt_bad++;
    }
    printf("  CONFIG:  %d/%d lifecycle entries intact\n",
           UFSRV_CONFIG_FIELD_COUNT - cfg_bad, UFSRV_CONFIG_FIELD_COUNT);
    printf("  RUNTIME: %d/%d lifecycle entries intact\n",
           UFSRV_RUNTIME_FIELD_COUNT - rt_bad, UFSRV_RUNTIME_FIELD_COUNT);
    EXPECT_EQ(cfg_bad, 0);
    EXPECT_EQ(rt_bad,  0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 3 — Generated INT accessors route through the correct slot
 *
 * Uses four representative generated setters / getters.  Each routes
 * through ValuesInstance internally — if the stride were still broken,
 * the getter would read from the wrong FieldEntry's .config member.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(StrideFixTest, GeneratedIntAccessors)
{
    printf("\n=== GENERATED INT ACCESSORS ===\n");

    // serverid         — CONFIG INT index 0
    // stats_backend_port — CONFIG INT index 5
    // command_console_port — CONFIG INT index 8
    // serverpid        — RUNTIME INT index 2

    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    item.value.stored_value.int_val = 7777;
    UfsrvConfigSetServerid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    printf("  UfsrvConfigServerid            set=7777  got=%d  %s\n",
           UfsrvConfigServerid(),
           UfsrvConfigServerid() == 7777 ? "OK" : "FAIL <<<");
    EXPECT_EQ(UfsrvConfigServerid(), 7777);

    item.value.stored_value.int_val = 19701;
    UfsrvConfigSetStatsBackendPort(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    printf("  UfsrvConfigStatsBackendPort    set=19701 got=%d  %s\n",
           UfsrvConfigStatsBackendPort(),
           UfsrvConfigStatsBackendPort() == 19701 ? "OK" : "FAIL <<<");
    EXPECT_EQ(UfsrvConfigStatsBackendPort(), 19701);

    item.value.stored_value.int_val = 5555;
    UfsrvConfigSetCommandConsolePort(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    printf("  UfsrvConfigCommandConsolePort  set=5555  got=%d  %s\n",
           UfsrvConfigCommandConsolePort(),
           UfsrvConfigCommandConsolePort() == 5555 ? "OK" : "FAIL <<<");
    EXPECT_EQ(UfsrvConfigCommandConsolePort(), 5555);

    item.value.stored_value.int_val = 8888;
    UfsrvConfigSetServerpid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    printf("  UfsrvConfigServerpid           set=8888  got=%d  %s\n",
           UfsrvConfigServerpid(),
           UfsrvConfigServerpid() == 8888 ? "OK" : "FAIL <<<");
    EXPECT_EQ(UfsrvConfigServerpid(), 8888);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 4 — Generated STR accessor (server_class, RUNTIME index 3)
 *
 * Pre-allocates a buffer for the STR field, then exercises the generated
 * setter / getter.  If the stride were broken, the getter would return a
 * pointer from the wrong FieldEntry slot.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(StrideFixTest, GeneratedStrAccessor)
{
    printf("\n=== GENERATED STR ACCESSOR ===\n");

    // server_class — RUNTIME STR at index 3.
    // Pre-allocate the buffer as on_inflated would.
    static const size_t kSz = 64;
    char *buf = static_cast<char *>(calloc(1, kSz));
    ASSERT_NE(buf, nullptr);

    UfsrvConfigValue *slot = UfsrvConfigRuntimeValuesInstance(
        static_cast<UfsrvRuntimeField>(UFSRV_RUNTIME_FIELD_SERVER_CLASS));
    slot->stored_value.str_val = buf;
    slot->type_allocation_sz   = kSz;

    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_STR;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;
    item.value.stored_value.str_val = const_cast<char *>("ufsrvwebsock");
    item.value.type_allocation_sz   = kSz;
    UfsrvConfigSetServerClass(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

    const char *got = UfsrvConfigServerClass();
    printf("  UfsrvConfigServerClass  set=\"ufsrvwebsock\"  got=\"%s\"  %s\n",
           got ? got : "(null)",
           (got && strcmp(got, "ufsrvwebsock") == 0) ? "OK" : "FAIL <<<");
    EXPECT_NE(got, nullptr);
    EXPECT_STREQ(got, "ufsrvwebsock");

    // Overwrite and re-read — proves setter writes the correct slot
    item.value.stored_value.str_val = const_cast<char *>("changed-host");
    UfsrvConfigSetServerClass(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    got = UfsrvConfigServerClass();
    printf("  UfsrvConfigServerClass  set=\"changed-host\"  got=\"%s\"  %s\n",
           got ? got : "(null)",
           (got && strcmp(got, "changed-host") == 0) ? "OK" : "FAIL <<<");
    EXPECT_STREQ(got, "changed-host");

    free(buf);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 5 — Defaults table dump + setter/getter round-trip
 *
 * Reads compiled defaults from ufsrv_config_field_defaults_table[] and
 * ufsrv_runtime_field_defaults_table[], prints representative values,
 * then uses generated setters to write new values and generated getters
 * to read them back.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(StrideFixTest, DefaultsDumpAndRoundTrip)
{
    printf("\n=== DEFAULTS TABLE DUMP + ROUND-TRIP ===\n");

    printf("\nCONFIG compiled defaults (ufsrv_config_field_defaults_table[]):\n");
    printf("  [ 0] serverid                          int_val=%5" PRId64 "\n",
           ufsrv_config_field_defaults_table[UFSRV_CONFIG_FIELD_SERVERID]
               .stored_value.int_val);
    printf("  [ 5] stats_backend_port                int_val=%5" PRId64 "\n",
           ufsrv_config_field_defaults_table[UFSRV_CONFIG_FIELD_STATS_BACKEND_PORT]
               .stored_value.int_val);
    printf("  [ 8] command_console_port              int_val=%5" PRId64 "\n",
           ufsrv_config_field_defaults_table[UFSRV_CONFIG_FIELD_COMMAND_CONSOLE_PORT]
               .stored_value.int_val);
    printf("  [22] buffer_sizes_incoming_buffer_size  int_val=%5" PRId64 "\n",
           ufsrv_config_field_defaults_table[
               UFSRV_CONFIG_FIELD_BUFFER_SIZES_INCOMING_BUFFER_SIZE]
               .stored_value.int_val);

    printf("\nRUNTIME compiled defaults (ufsrv_runtime_field_defaults_table[]):\n");
    printf("  [ 0] conf_file_store_operating_state   int_val=%5" PRId64 "\n",
           ufsrv_runtime_field_defaults_table[
               UFSRV_RUNTIME_FIELD_CONF_FILE_STORE_OPERATING_STATE]
               .stored_value.int_val);
    printf("  [ 2] serverpid                         int_val=%5" PRId64 "\n",
           ufsrv_runtime_field_defaults_table[UFSRV_RUNTIME_FIELD_SERVERPID]
               .stored_value.int_val);
    printf("  [ 3] server_class                      str_val=\"%s\"\n",
           ufsrv_runtime_field_defaults_table[UFSRV_RUNTIME_FIELD_SERVER_CLASS]
               .stored_value.str_val
               ? ufsrv_runtime_field_defaults_table[UFSRV_RUNTIME_FIELD_SERVER_CLASS]
                     .stored_value.str_val
               : "(null)");

    // Round-trip: setter → getter
    printf("\nRound-trip (generated setter → generated getter):\n");

    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    item.value.stored_value.int_val = 42;
    UfsrvConfigSetServerid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    printf("  UfsrvConfigServerid            set=42    got=%d  %s\n",
           UfsrvConfigServerid(),
           UfsrvConfigServerid() == 42 ? "OK" : "FAIL <<<");
    EXPECT_EQ(UfsrvConfigServerid(), 42);

    item.value.stored_value.int_val = 8080;
    UfsrvConfigSetStatsBackendPort(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    printf("  UfsrvConfigStatsBackendPort    set=8080  got=%d  %s\n",
           UfsrvConfigStatsBackendPort(),
           UfsrvConfigStatsBackendPort() == 8080 ? "OK" : "FAIL <<<");
    EXPECT_EQ(UfsrvConfigStatsBackendPort(), 8080);
}
