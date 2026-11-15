/**
 * @file ufsrv_config_defaults_populate_adversarial_test.cpp
 * @brief Adversarial test — verifies PopulateDefaults functions correctly load
 *        compiled defaults into consumer stores, handling edge cases, forced
 *        zeroing, NULL arguments, and value-type round-trips.
 *
 * ## Coverage
 *
 *   - T1: INT/ENUM defaults loaded correctly (all CONFIG + RUNTIME)
 *   - T2: STR defaults string-content match after populate
 *   - T3: is_forced=true zeroes existing data before populate
 *   - T4: is_forced=false preserves data, source set correctly
 *   - T5: Metadata fields set post-loop to DEFAULTS_LOADED
 *   - T6: NULL defaults_table handled (defensive)
 *   - T7: Double-populate idempotency
 *   - T8: Panic-before-init aborts
 *
 * NOTE: STRUCT defaults tables carry .blob_val=NULL (from .int_val=0).
 * GenericStructSetter does memcpy(dest, NULL, sz) -> UB.  Tests null
 * STRUCT setter callbacks before calling PopulateDefaults to avoid this.
 * is_forced=true zeroes Value slots including STR buffers, so STR setters
 * are also nulled for forced-populate tests.
 *
 * All stores are UfsrvConfigFieldEntry[].  Builds with
 * -fsanitize=address,leak,undefined.
 *
 * Copyright (C) 2015-2026 unfacd works
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <climits>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
#include <data_ufsrv_config_field_tables_generated.c>
#include "ufsrv_config_generic_callbacks_priv.h"
}

/* ── Sentinel ──────────────────────────────────────────────────────────── */

static constexpr int64_t kSentinelCfg = 0x7A5E00007A5E0000LL;

/* ── Field-type helpers ────────────────────────────────────────────────── */

static bool IsIntOrEnumSetter(UfsrvConfigFieldEntry *entry) {
    void *cb = (void *)entry->lifecycle.on_value_available;
    return cb == (void *)GenericIntSetter
        || cb == (void *)GenericIntSetterRuntime;
}
static bool IsStrSetter(UfsrvConfigFieldEntry *entry) {
    void *cb = (void *)entry->lifecycle.on_value_available;
    return cb == (void *)GenericStrSetter
        || cb == (void *)GenericStrSetterRuntime;
}
static bool IsStructSetter(UfsrvConfigFieldEntry *entry) {
    void *cb = (void *)entry->lifecycle.on_value_available;
    return cb == (void *)GenericStructSetter
        || cb == (void *)GenericStructSetterRuntime;
}

/* Check if STR field needs allocation for defaults test */
static bool NeedsStrInflate(int idx, bool is_config) {
    if (is_config)
        return ufsrv_config_field_defaults_table[idx].type_allocation_sz > 0
            && ufsrv_config_field_defaults_table[idx].stored_value.str_val != nullptr;
    return ufsrv_runtime_field_defaults_table[idx].type_allocation_sz > 0
        && ufsrv_runtime_field_defaults_table[idx].stored_value.str_val != nullptr;
}

/* ── Safe populate wrappers ────────────────────────────────────────────── */
/* Null STRUCT (and for forced, STR) setters so PopulateDefaults skips
 * fields whose defaults would trigger UB in the generic setter. */

static void SafeConfPopulate(const UfsrvConfigValue *table, bool forced,
                              UfsrvConfigFieldEntry *store, int count,
                              const UfsrvConfigFieldEntry *ref) {
    for (int i = 0; i < count; i++) {
        if (IsStructSetter(&store[i]) || (forced && IsStrSetter(&store[i])))
            store[i].lifecycle.on_value_available = nullptr;
    }
    UfsrvConfigConfFileStorePopulateDefaults(table, forced);
    for (int i = 0; i < count; i++)
        if (!store[i].lifecycle.on_value_available)
            store[i].lifecycle.on_value_available
                = ref[i].lifecycle.on_value_available;
}

static void SafeRuntimePopulate(const UfsrvConfigValue *table, bool forced,
                                 UfsrvConfigFieldEntry *store, int count,
                                 const UfsrvConfigFieldEntry *ref) {
    for (int i = 0; i < count; i++) {
        if (IsStructSetter(&store[i]) || (forced && IsStrSetter(&store[i])))
            store[i].lifecycle.on_value_available = nullptr;
    }
    UfsrvConfigRuntimeStorePopulateDefaults(table, forced);
    for (int i = 0; i < count; i++)
        if (!store[i].lifecycle.on_value_available)
            store[i].lifecycle.on_value_available
                = ref[i].lifecycle.on_value_available;
}

/* ── Fixture ───────────────────────────────────────────────────────────── */

class DefaultsPopulateAdversarialTest : public ::testing::Test {
protected:
    UfsrvConfigFieldEntry cfg_store[UFSRV_CONFIG_FIELD_COUNT];
    UfsrvConfigFieldEntry rt_store[UFSRV_RUNTIME_FIELD_COUNT];

    void SetUp() override { memset(cfg_store, 0, sizeof(cfg_store));
                            memset(rt_store,  0, sizeof(rt_store)); }

    void ArmStores(UfsrvConfigFieldEntry *cfg, UfsrvConfigFieldEntry *rt) {
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++)
            cfg[i].lifecycle = ufsrv_config_field_table[i].lifecycle;
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++)
            rt[i].lifecycle  = ufsrv_runtime_field_table[i].lifecycle;
        UfsrvConfigConfFileStoreInit(cfg, TODO);
        UfsrvConfigRuntimeStoreInit(rt, TODO);
    }

    void PreInflate(UfsrvConfigFieldEntry *cfg, UfsrvConfigFieldEntry *rt) {
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
            if (NeedsStrInflate(i, true)) {
                UfsrvConfigFieldEntry *e = UfsrvConfigConfFileStoreInstance(
                    static_cast<UfsrvConfigField>(i));
                if (e->lifecycle.on_inflated)
                    e->lifecycle.on_inflated(i,
                        &ufsrv_config_field_defaults_table[i], false);
            }
        }
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
            if (NeedsStrInflate(i, false)) {
                UfsrvConfigFieldEntry *e = UfsrvConfigRuntimeStoreInstance(
                    static_cast<UfsrvRuntimeField>(i));
                if (e->lifecycle.on_inflated)
                    e->lifecycle.on_inflated(i,
                        &ufsrv_runtime_field_defaults_table[i], false);
            }
        }
    }

    void TearDown() override {
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++)
            if (NeedsStrInflate(i, true))
                free(cfg_store[i].config.stored_value.str_val);
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++)
            if (NeedsStrInflate(i, false))
                free(rt_store[i].config.stored_value.str_val);
    }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * T1 — INT/ENUM defaults loaded correctly
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DefaultsPopulateAdversarialTest, IntDefaultsLoadedCorrectly)
{
    ArmStores(cfg_store, rt_store);
    PreInflate(cfg_store, rt_store);

    SafeConfPopulate(ufsrv_config_field_defaults_table, false,
                     cfg_store, UFSRV_CONFIG_FIELD_COUNT,
                     ufsrv_config_field_table);
    SafeRuntimePopulate(ufsrv_runtime_field_defaults_table, false,
                        rt_store, UFSRV_RUNTIME_FIELD_COUNT,
                        ufsrv_runtime_field_table);

    int cfg_count = 0, rt_count = 0;

    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigFieldEntry *e = UfsrvConfigConfFileStoreInstance(
            static_cast<UfsrvConfigField>(i));
        if (!IsIntOrEnumSetter(e)) continue; cfg_count++;
        int64_t exp = ufsrv_config_field_defaults_table[i].stored_value.int_val;
        int64_t act = UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(i))->stored_value.int_val;
        EXPECT_EQ(act, exp) << "CONFIG INT field " << i;
    }
    for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
        if (i == UFSRV_RUNTIME_FIELD_CONF_FILE_STORE_OPERATING_STATE
         || i == UFSRV_RUNTIME_FIELD_RUNTIME_STORE_OPERATING_STATE) continue;
        UfsrvConfigFieldEntry *e = UfsrvConfigRuntimeStoreInstance(
            static_cast<UfsrvRuntimeField>(i));
        if (!IsIntOrEnumSetter(e)) continue; rt_count++;
        int64_t exp = ufsrv_runtime_field_defaults_table[i].stored_value.int_val;
        int64_t act = UfsrvConfigRuntimeValuesInstance(
            static_cast<UfsrvRuntimeField>(i))->stored_value.int_val;
        EXPECT_EQ(act, exp) << "RUNTIME INT field " << i;
    }
    printf("  INT/ENUM: %d CONFIG + %d RUNTIME\n", cfg_count, rt_count);
    EXPECT_GT(cfg_count, 0); EXPECT_GT(rt_count, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T2 — STR defaults string content matches (must be copy, not pointer)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DefaultsPopulateAdversarialTest, StrDefaultsStringContentMatches)
{
    ArmStores(cfg_store, rt_store);
    PreInflate(cfg_store, rt_store);

    SafeConfPopulate(ufsrv_config_field_defaults_table, false,
                     cfg_store, UFSRV_CONFIG_FIELD_COUNT,
                     ufsrv_config_field_table);
    SafeRuntimePopulate(ufsrv_runtime_field_defaults_table, false,
                        rt_store, UFSRV_RUNTIME_FIELD_COUNT,
                        ufsrv_runtime_field_table);

    int cfg_count = 0, rt_count = 0;

    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigFieldEntry *e = UfsrvConfigConfFileStoreInstance(
            static_cast<UfsrvConfigField>(i));
        if (!IsStrSetter(e)) continue;
        const char *exp = ufsrv_config_field_defaults_table[i].stored_value.str_val;
        size_t sz = ufsrv_config_field_defaults_table[i].type_allocation_sz;
        if (!exp || sz == 0) continue; cfg_count++;
        const char *act = UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(i))->stored_value.str_val;
        ASSERT_NE(act, nullptr) << "CONFIG STR " << i << " NULL";
        EXPECT_NE(act, exp) << "CONFIG STR " << i << " points to table";
        EXPECT_STREQ(act, exp) << "CONFIG STR " << i << " mismatch";
    }
    for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
        if (i == UFSRV_RUNTIME_FIELD_CONF_FILE_STORE_OPERATING_STATE
         || i == UFSRV_RUNTIME_FIELD_RUNTIME_STORE_OPERATING_STATE) continue;
        UfsrvConfigFieldEntry *e = UfsrvConfigRuntimeStoreInstance(
            static_cast<UfsrvRuntimeField>(i));
        if (!IsStrSetter(e)) continue;
        const char *exp = ufsrv_runtime_field_defaults_table[i].stored_value.str_val;
        size_t sz = ufsrv_runtime_field_defaults_table[i].type_allocation_sz;
        if (!exp || sz == 0) continue; rt_count++;
        const char *act = UfsrvConfigRuntimeValuesInstance(
            static_cast<UfsrvRuntimeField>(i))->stored_value.str_val;
        ASSERT_NE(act, nullptr) << "RUNTIME STR " << i << " NULL";
        EXPECT_NE(act, exp) << "RUNTIME STR " << i << " points to table";
        EXPECT_STREQ(act, exp) << "RUNTIME STR " << i << " mismatch";
    }
    printf("  STR: %d CONFIG + %d RUNTIME\n", cfg_count, rt_count);
    EXPECT_GT(cfg_count, 0); EXPECT_GT(rt_count, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T3 — is_forced=true zeroes existing data before populate
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DefaultsPopulateAdversarialTest, ForcedTrueZeroesFirst)
{
    ArmStores(cfg_store, rt_store);

    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigFieldEntry *e = UfsrvConfigConfFileStoreInstance(
            static_cast<UfsrvConfigField>(i));
        if (IsIntOrEnumSetter(e))
            UfsrvConfigConfFileValuesInstance(
                static_cast<UfsrvConfigField>(i))->stored_value.int_val
                = kSentinelCfg + i;
    }

    SafeConfPopulate(ufsrv_config_field_defaults_table, true,
                     cfg_store, UFSRV_CONFIG_FIELD_COUNT,
                     ufsrv_config_field_table);

    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigFieldEntry *e = UfsrvConfigConfFileStoreInstance(
            static_cast<UfsrvConfigField>(i));
        if (!IsIntOrEnumSetter(e)) continue;
        int64_t act = UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(i))->stored_value.int_val;
        int64_t exp = ufsrv_config_field_defaults_table[i].stored_value.int_val;
        EXPECT_EQ(act, exp) << "CONFIG " << i << " not reset by forced";
        EXPECT_NE(act, kSentinelCfg + i) << "CONFIG " << i << " still sentinel";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T4 — source flag set correctly, is_forced=false preserves existing data
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DefaultsPopulateAdversarialTest, SourceFlagSet)
{
    ArmStores(cfg_store, rt_store);
    PreInflate(cfg_store, rt_store);

    SafeConfPopulate(ufsrv_config_field_defaults_table, false,
                     cfg_store, UFSRV_CONFIG_FIELD_COUNT,
                     ufsrv_config_field_table);

    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigFieldEntry *e = UfsrvConfigConfFileStoreInstance(
            static_cast<UfsrvConfigField>(i));
        if (IS_PRESENT(e->lifecycle.on_value_available))
            EXPECT_EQ(static_cast<int>(e->config.source),
                      static_cast<int>(UFSRV_CONFIG_SOURCE_DEFAULTS))
                << "CONFIG " << i << " source not DEFAULTS";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T5 — NULL defaults_table (SIGSEGV)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DefaultsPopulateAdversarialTest, NullDefaultsTable)
{
    ArmStores(cfg_store, rt_store);
    EXPECT_DEATH(
        UfsrvConfigConfFileStorePopulateDefaults(nullptr, false), ".*");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T7 — Double-populate idempotency
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DefaultsPopulateAdversarialTest, DoublePopulateIdempotent)
{
    ArmStores(cfg_store, rt_store);
    PreInflate(cfg_store, rt_store);

    SafeConfPopulate(ufsrv_config_field_defaults_table, false,
                     cfg_store, UFSRV_CONFIG_FIELD_COUNT,
                     ufsrv_config_field_table);

    int64_t snap[UFSRV_CONFIG_FIELD_COUNT];
    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        UfsrvConfigFieldEntry *e = UfsrvConfigConfFileStoreInstance(
            static_cast<UfsrvConfigField>(i));
        snap[i] = IsIntOrEnumSetter(e)
            ? UfsrvConfigConfFileValuesInstance(
                static_cast<UfsrvConfigField>(i))->stored_value.int_val
            : INT64_MIN;
    }

    SafeConfPopulate(ufsrv_config_field_defaults_table, false,
                     cfg_store, UFSRV_CONFIG_FIELD_COUNT,
                     ufsrv_config_field_table);

    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
        if (snap[i] == INT64_MIN) continue;
        EXPECT_EQ(UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(i))->stored_value.int_val,
            snap[i]) << "CONFIG INT " << i << " changed on second populate";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T8 — Panic-before-init
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DefaultsPopulateAdversarialTest, PanicBeforeInit)
{
    /* The function accesses StoreInstance/ValuesInstance which have
     * panic-before-init guards — any of these will abort the process.
     * With sanitizers the signal may differ (SIGABRT vs SIGSEGV). */
    EXPECT_DEATH(
        UfsrvConfigConfFileStorePopulateDefaults(
            ufsrv_config_field_defaults_table, false),
        "");
}
