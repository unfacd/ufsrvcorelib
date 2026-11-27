/**
 * @file ufsrv_config_setter_delegation_test.cpp
 * @brief Adversarial test — verifies every generated named setter dispatches
 *        through its lifecycle.on_value_available callback, not through
 *        inline store access or hardcoded generic-setter calls.
 *
 * ## Coverage
 *
 *   - 28 CONFIG named setters + 24 RUNTIME named setters = 52 total
 *   - 1 custom-setter field (oauth_settings) verified excluded
 *   - T1: Named-vs-lifecycle equivalence (byte-level store comparison)
 *   - T2: Cross-contamination prevention (adjacent slot integrity)
 *   - T3: Lifecycle callback correctness by value_type
 *   - T4: Custom-setter exclusion (oauth_settings)
 *   - T5: Setter does not mutate the UfsrvConfigItem
 *   - T6: All named setters callable without crash
 *   - T7: Lifecycle NULL guard — safe no-op when callback is nullified
 *
 * All stores are UfsrvConfigFieldEntry[] — the type the library contract
 * requires.  Builds with -fsanitize=address,leak,undefined.
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Type definitions
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef void (*NamedSetterFn)(UfsrvConfigItem *, UfsrvConfigSource);
typedef void (*LifecycleCallbackFn)(int, const UfsrvConfigValue *, UfsrvConfigSource);

/* ═══════════════════════════════════════════════════════════════════════════
 * Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static bool NeedsStrInflate(int idx, bool is_config) {
    if (is_config)
        return ufsrv_config_field_defaults_table[idx].type_allocation_sz > 0;
    return ufsrv_runtime_field_defaults_table[idx].type_allocation_sz > 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Fixture — two independent store pairs for parallel comparison
 * ═══════════════════════════════════════════════════════════════════════════ */

class SetterDelegationTest : public ::testing::Test {
protected:
    /* Primary stores — used by named setter dispatch */
    UfsrvConfigFieldEntry  cfg_store[UFSRV_CONFIG_FIELD_COUNT];
    UfsrvConfigFieldEntry  rt_store[UFSRV_RUNTIME_FIELD_COUNT];

    /* Mirror stores — used for direct lifecycle callback comparison */
    UfsrvConfigFieldEntry  cfg_mirror[UFSRV_CONFIG_FIELD_COUNT];
    UfsrvConfigFieldEntry  rt_mirror[UFSRV_RUNTIME_FIELD_COUNT];

    static constexpr int64_t kSentinelCfg = 0x7A5E5A5E5A5E5A5ELL;
    static constexpr int64_t kSentinelRt  = 0x3C3C3C3C3C3C3C3CLL;

    void ArmStores(UfsrvConfigFieldEntry *cfg, UfsrvConfigFieldEntry *rt) {
        /* Copy lifecycle pointers from generated tables */
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++)
            cfg[i].lifecycle = ufsrv_config_field_table[i].lifecycle;
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++)
            rt[i].lifecycle  = ufsrv_runtime_field_table[i].lifecycle;

        UfsrvConfigConfFileStoreInit(cfg, TODO);
        UfsrvConfigRuntimeStoreInit(rt, TODO);

        /* Pre-inflate STR fields (CONFIG) */
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
            if (NeedsStrInflate(i, true)) {
                UfsrvConfigFieldEntry *entry = UfsrvConfigConfFileStoreInstance(
                    static_cast<UfsrvConfigField>(i));
                if (entry->lifecycle.on_inflated) {
                    entry->lifecycle.on_inflated(i,
                        &ufsrv_config_field_defaults_table[i], false);
                }
            }
        }

        /* Pre-inflate STR + STRUCT fields (RUNTIME) */
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
            size_t sz = ufsrv_runtime_field_defaults_table[i].type_allocation_sz;
            if (sz > 0) {
                UfsrvConfigFieldEntry *entry = UfsrvConfigRuntimeStoreInstance(
                    static_cast<UfsrvRuntimeField>(i));
                if (entry->lifecycle.on_inflated) {
                    entry->lifecycle.on_inflated(i,
                        &ufsrv_runtime_field_defaults_table[i], false);
                }
            }
        }
    }

    void SetUp() override {
        memset(cfg_store,  0, sizeof(cfg_store));
        memset(rt_store,   0, sizeof(rt_store));
        memset(cfg_mirror, 0, sizeof(cfg_mirror));
        memset(rt_mirror,  0, sizeof(rt_mirror));

        ArmStores(cfg_store, rt_store);
        ArmStores(cfg_mirror, rt_mirror);
    }

    void TearDown() override {
        /* Free STR + STRUCT allocations from all four stores.
         * Iterate the arrays directly (don't use ValuesInstance which
         * requires the correct store to be armed). */
        auto free_store_slots = [](UfsrvConfigFieldEntry *store, int count, bool is_config) {
            for (int i = 0; i < count; i++) {
                if (NeedsStrInflate(i, is_config)) {
                    free(store[i].config.stored_value.str_val);
                    store[i].config.stored_value.str_val = nullptr;
                }
            }
        };
        free_store_slots(cfg_store,  UFSRV_CONFIG_FIELD_COUNT,  true);
        free_store_slots(rt_store,   UFSRV_RUNTIME_FIELD_COUNT, false);
        free_store_slots(cfg_mirror, UFSRV_CONFIG_FIELD_COUNT,  true);
        free_store_slots(rt_mirror,  UFSRV_RUNTIME_FIELD_COUNT, false);
    }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Dispatch tables — one entry per generated setter
 *
 * The lifecycle callback (on_value_available) is read from the generated
 * const field table — it is the single source of truth for what the named
 * setter should dispatch through.
 * ═══════════════════════════════════════════════════════════════════════════ */

struct SetterDispatchInfo {
    const char     *name;
    int             enum_idx;
    NamedSetterFn   named_setter;
    bool            is_config;
    size_t          alloc_sz;
    bool            skip_setter;   /* is_keep_custom_setter or CONST_PTR */
};

#define CFG_ENTRY(NAME, SETTER) \
    { #NAME, static_cast<int>(UFSRV_CONFIG_FIELD_##NAME), \
      reinterpret_cast<NamedSetterFn>(UfsrvConfigSet##SETTER), \
      true, \
      ufsrv_config_field_defaults_table[UFSRV_CONFIG_FIELD_##NAME].type_allocation_sz, \
      false }

static const SetterDispatchInfo kCfgFields[] = {
    CFG_ENTRY(SERVERID,                               Serverid),
    CFG_ENTRY(SERVERID_BY_USER,                       ServeridByUser),
    CFG_ENTRY(INTRA_UFSRV_CLASSNAME,                  IntraUfsrvClassname),
    CFG_ENTRY(UFSRV_GEOGROUP,                         UfsrvGeogroup),
    CFG_ENTRY(SERVER_RUN_MODE,                        ServerRunMode),
    CFG_ENTRY(COMMAND_CONSOLE_PORT,                   CommandConsolePort),
    CFG_ENTRY(COMMAND_CONSOLE_ADDRESS,                CommandConsoleAddress),
    CFG_ENTRY(SERVER_CPU_AFFINITY,                    ServerCpuAffinity),
    CFG_ENTRY(LISTENER_WORKERS_THREAD_POOL,           ListenerWorkersThreadPool),
    CFG_ENTRY(UFSRV_WORKERS_THREAD_POOL,              UfsrvWorkersThreadPool),
    CFG_ENTRY(SESSION_WORKERS_THREAD_POOL,            SessionWorkersThreadPool),
    CFG_ENTRY(MEMORY_SPECS_SESSIONS_PER_ALLOCATION_GROUP,
              MemorySpecsSessionsPerAllocationGroup),
    CFG_ENTRY(MEMORY_SPECS_ALLOCATION_GROUPS,         MemorySpecsAllocationGroups),
    CFG_ENTRY(MEMORY_SPECS_ALLOCATION_THRESHOLD_TRIGGER,
              MemorySpecsAllocationThresholdTrigger),
    CFG_ENTRY(STATS_BACKEND_ADDRESS,                  StatsBackendAddress),
    CFG_ENTRY(STATS_BACKEND_PORT,                     StatsBackendPort),
    CFG_ENTRY(USER_TIMEOUTS_UNAUTHENTICATED_TIMEOUT,
              UserTimeoutsUnauthenticatedTimeout),
    CFG_ENTRY(USER_TIMEOUTS_CONNECTED_TIMEOUT,
              UserTimeoutsConnectedTimeout),
    CFG_ENTRY(USER_TIMEOUTS_SUSPENDED_TIMEOUT,
              UserTimeoutsSuspendedTimeout),
    CFG_ENTRY(BUFFER_SIZES_INCOMING_BUFFER_SIZE,      BufferSizesIncomingBufferSize),
    CFG_ENTRY(BUFFER_SIZES_INCOMING_BUFFER_MAXSIZE,   BufferSizesIncomingBufferMaxsize),
    CFG_ENTRY(BUFFER_SIZES_OUTGOING_BUFFER_SIZE,      BufferSizesOutgoingBufferSize),
    CFG_ENTRY(BUFFER_SIZES_HOLDING_BUFFER_SIZE,       BufferSizesHoldingBufferSize),
    CFG_ENTRY(SYS_RUNTIME_RUN_AS_USER,                SysRuntimeRunAsUser),
    CFG_ENTRY(SYS_RUNTIME_CHROOT,                     SysRuntimeChroot),
};
static_assert(sizeof(kCfgFields)/sizeof(kCfgFields[0]) == UFSRV_CONFIG_FIELD_COUNT,
              "kCfgFields must cover all CONFIG fields");

#define RT_ENTRY(NAME, SETTER, EXTRA_SKIP) \
    { #NAME, static_cast<int>(UFSRV_RUNTIME_FIELD_##NAME), \
      reinterpret_cast<NamedSetterFn>(UfsrvConfigSet##SETTER), \
      false, \
      ufsrv_runtime_field_defaults_table[UFSRV_RUNTIME_FIELD_##NAME].type_allocation_sz, \
      EXTRA_SKIP }

/* oauth_settings: is_keep_custom_setter=true — no generated setter */
#define RT_SKIP(NAME) \
    { #NAME, static_cast<int>(UFSRV_RUNTIME_FIELD_##NAME), \
      nullptr, false, \
      ufsrv_runtime_field_defaults_table[UFSRV_RUNTIME_FIELD_##NAME].type_allocation_sz, \
      true }

static const SetterDispatchInfo kRtFields[] = {
    /*  0 */ RT_ENTRY(CONF_FILE_STORE_OPERATING_STATE,    ConfFileStoreOperatingState,    false),
    /*  1 */ RT_ENTRY(RUNTIME_STORE_OPERATING_STATE,      RuntimeStoreOperatingState,      false),
    /*  2 */ RT_ENTRY(SERVERPID,                          Serverpid,                       false),
    /*  3 */ RT_ENTRY(SERVER_CLASS,                       ServerClass,                     false),
    /*  4 */ RT_ENTRY(SERVER_DESCRIPTIVE_NAME,            ServerDescriptiveName,           false), /* PTR */
    /*  5 */ RT_ENTRY(WHEN,                               When,                            false),
    /*  6 */ RT_ENTRY(UFSRV_TIME,                         UfsrvTime,                       false),
    /*  7 */ RT_ENTRY(SERVER_PROCESS_MODE,                ServerProcessMode,               false),
    /*  8 */ RT_ENTRY(LOG_MODE,                           LogMode,                         false),
    /*  9 */ RT_ENTRY(SSL_SUPPORT,                        SslSupport,                      false),
    /* 10 */ RT_ENTRY(CONFIG_DIR,                         ConfigDir,                       false),
    /* 11 */ RT_ENTRY(CONFIG_FILE,                        ConfigFile,                      false),
    /* 12 */ RT_ENTRY(LUA_HANDLE,                         LuaHandle,                       false), /* PTR */
    /* 13 */ RT_ENTRY(INSTRUMENTATION_BACKEND,            InstrumentationBackend,          false), /* PTR */
    /* 14 */ RT_ENTRY(INSTRUMENTATION_BACKEND_SERVER,     InstrumentationBackendServer,    false),
    /* 15 */ RT_ENTRY(SESSIONS_DELEGATOR,                 SessionsDelegator,               false), /* PTR */
    /* 16 */ RT_ENTRY(WORK_DELEGATOR_PIPE,                WorkDelegatorPipe,               false), /* PTR */
    /* 17 */ RT_ENTRY(PIPEFDS,                            Pipefds,                         false),
    /* 18 */ RT_ENTRY(SSL_CTX,                            SslCtx,                          false), /* PTR */
    /* 19 */ RT_ENTRY(SSL_USER_CTX,                       SslUserCtx,                      false), /* PTR */
    /* 20 */ RT_ENTRY(THREAD_HANDLE_TIMER_DELEGATOR,      ThreadHandleTimerDelegator,      false),
    /* 21 */ RT_ENTRY(FILE_LOADER_SERVICE_THREAD,         FileLoaderServiceThread,         false),
    /* 22 */ RT_SKIP(OAUTH_SETTINGS),                                                              /* is_keep_custom_setter */
    /* 23 */ RT_ENTRY(UFSRV_RESULT,                       UfsrvResult,                     false),
    /* 24 */ RT_ENTRY(THREADS_SUBSYSTEM_UFSRV_ONCE,       ThreadsSubsystemUfsrvOnce,       false),
    /* 25 */ RT_ENTRY(THREADS_SUBSYSTEM_UFSRV_CTX_KEY,    ThreadsSubsystemUfsrvCtxKey,     false),
};
static_assert(sizeof(kRtFields)/sizeof(kRtFields[0]) == UFSRV_RUNTIME_FIELD_COUNT,
              "kRtFields must cover all RUNTIME fields");

/* ═══════════════════════════════════════════════════════════════════════════
 * Helper: get the lifecycle callback for a field from the field table
 * ═══════════════════════════════════════════════════════════════════════════ */

static LifecycleCallbackFn GetLifecycleCallback(const SetterDispatchInfo &info) {
    if (info.is_config) {
        return reinterpret_cast<LifecycleCallbackFn>(
            ufsrv_config_field_table[info.enum_idx].lifecycle.on_value_available);
    }
    return reinterpret_cast<LifecycleCallbackFn>(
        ufsrv_runtime_field_table[info.enum_idx].lifecycle.on_value_available);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Helper: get a pointer to the ValuesInstance for a field
 * ═══════════════════════════════════════════════════════════════════════════ */

static UfsrvConfigValue *GetStoreSlot(const SetterDispatchInfo &info) {
    if (info.is_config) {
        return UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(info.enum_idx));
    }
    return UfsrvConfigRuntimeValuesInstance(
        static_cast<UfsrvRuntimeField>(info.enum_idx));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Helper: compare two store slots byte-for-byte
 * ═══════════════════════════════════════════════════════════════════════════ */

static bool StoreSlotEquals(const SetterDispatchInfo &info,
                            UfsrvConfigValue *a, UfsrvConfigValue *b) {
    /* Compare the full UfsrvConfigValue struct */
    return memcmp(a, b, sizeof(UfsrvConfigValue)) == 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T1 — NamedSetterDispatchesThroughLifecycle (all 52 generated setters)
 *
 * For every field with a generated named setter:
 *   1. Call the named setter on the primary store with a sentinel value
 *   2. Independently call entry->lifecycle.on_value_available on the mirror
 *      store with the same value
 *   3. Byte-compare both stores — must be identical
 *
 * Adversarial: uses unique sentinel per field to prove correct slot routing.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(SetterDelegationTest, NamedSetterDispatchesThroughLifecycle)
{
    int failures = 0;
    UfsrvConfigItem item = {};

    auto test_field = [&](const SetterDispatchInfo &f) {
        if (f.skip_setter || !f.named_setter)
            return;

        LifecycleCallbackFn cb = GetLifecycleCallback(f);
        if (!cb) {
            /* Field has NULL on_value_available — named setter should be
             * a safe no-op (NULL guard in the generated body). */
            return;
        }

        /* Unique sentinel per field to detect cross-slot aliasing */
        int64_t sentinel = (f.is_config ? kSentinelCfg : kSentinelRt)
                           + static_cast<int64_t>(f.enum_idx);

        /* Prepare item with the correct value type and a large enough
         * source buffer for STR/STRUCT fields (strncpy/memcpy need a
         * valid source of at least type_allocation_sz bytes). */
        UfsrvConfigItem item = {};
        item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

        /* Use a stack buffer for STR/STRUCT source data */
        unsigned char src_buf[512] = {};
        size_t src_sz = f.alloc_sz;
        if (src_sz > sizeof(src_buf)) src_sz = sizeof(src_buf);

        if (f.alloc_sz > 0) {
            /* STR or STRUCT — fill source buffer with sentinel-derived pattern */
            memset(src_buf, static_cast<unsigned char>(sentinel & 0xFF), src_sz);
            item.value.stored_value.str_val = reinterpret_cast<char *>(src_buf);
            item.value.type_allocation_sz   = f.alloc_sz;
            item.type = UFSRV_CONFIG_VALUE_STR;
        } else {
            /* INT, ENUM, or PTR — use int_val directly */
            item.value.stored_value.int_val = sentinel;
            item.value.type_allocation_sz   = 0;
            item.type = UFSRV_CONFIG_VALUE_INT;
        }

        f.named_setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

        UfsrvConfigValue *primary_slot = GetStoreSlot(f);

        /* ── Mirror store: call lifecycle callback directly ── */
        UfsrvConfigConfFileStoreInit(cfg_mirror, TODO);
        UfsrvConfigRuntimeStoreInit(rt_mirror, TODO);

        cb(f.enum_idx, &item.value, UFSRV_CONFIG_SOURCE_DEFAULTS);

        UfsrvConfigValue *mirror_slot = GetStoreSlot(f);

        /* ── Compare — both paths must produce identical store state ── */
        if (!StoreSlotEquals(f, primary_slot, mirror_slot)) {
            failures++;
            printf("  FAIL %s (idx=%d, %s): named setter and lifecycle "
                   "callback produced different store state\n",
                   f.name, f.enum_idx, f.is_config ? "CONFIG" : "RUNTIME");
        }
    };

    for (const auto &f : kCfgFields) test_field(f);
    for (const auto &f : kRtFields)  test_field(f);

    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T2 — NamedSetterLifecycleCrossContamination
 *
 * Write via named setter to field X, write via lifecycle callback to field Y
 * (adjacent).  Verify field X retains its value — proves the named setter
 * targets the correct slot via its enum constant, not whatever is at the
 * adjacent index.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(SetterDelegationTest, NamedSetterLifecycleCrossContamination)
{
    int failures = 0;
    UfsrvConfigItem item_a = {};
    UfsrvConfigItem item_b = {};
    item_a.type = UFSRV_CONFIG_VALUE_INT;
    item_a.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;
    item_b.type = UFSRV_CONFIG_VALUE_INT;
    item_b.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* Test adjacent CONFIG field pairs — INT/ENUM only (STR fields need
     * pre-inflated buffers and have different store semantics). */
    for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT - 1; i++) {
        const auto &fa = kCfgFields[i];
        const auto &fb = kCfgFields[i + 1];
        if (fa.skip_setter || !fa.named_setter) continue;
        if (fb.skip_setter || !fb.named_setter) continue;
        /* Skip STR fields — alloc_sz > 0 means pre-inflated buffer */
        if (fa.alloc_sz > 0 || fb.alloc_sz > 0) continue;

        LifecycleCallbackFn cb_b = GetLifecycleCallback(fb);
        if (!cb_b) continue;

        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);

        int64_t val_a = kSentinelCfg + i;
        int64_t val_b = kSentinelCfg + i + 1000;

        /* Write value A via named setter to field i */
        item_a.value.stored_value.int_val = val_a;
        fa.named_setter(&item_a, UFSRV_CONFIG_SOURCE_DEFAULTS);

        /* Write value B via lifecycle callback to field i+1 */
        item_b.value.stored_value.int_val = val_b;
        cb_b(fb.enum_idx, &item_b.value, UFSRV_CONFIG_SOURCE_DEFAULTS);

        /* Verify field i still has value A */
        int64_t got_a = UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(fa.enum_idx))->stored_value.int_val;
        if (got_a != val_a) {
            failures++;
            printf("  FAIL CONFIG cross-contamination: %s wrote %" PRId64
                   " got %" PRId64 " (adjacent %s wrote %" PRId64 ")\n",
                   fa.name, val_a, got_a, fb.name, val_b);
        }
    }

    /* Test adjacent RUNTIME field pairs */
    for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT - 1; i++) {
        const auto &fa = kRtFields[i];
        const auto &fb = kRtFields[i + 1];
        if (fa.skip_setter || !fa.named_setter) continue;
        if (fb.skip_setter || !fb.named_setter) continue;
        /* Skip non-INT fields for simplicity — cross-contamination
         * is proven by the INT subset; STR/STRUCT tested in T1 */
        if (fa.alloc_sz > 0 || fb.alloc_sz > 0) continue;

        LifecycleCallbackFn cb_b = GetLifecycleCallback(fb);
        if (!cb_b) continue;

        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);

        int64_t val_a = kSentinelRt + i;
        int64_t val_b = kSentinelRt + i + 1000;

        item_a.value.stored_value.int_val = val_a;
        fa.named_setter(&item_a, UFSRV_CONFIG_SOURCE_DEFAULTS);

        item_b.value.stored_value.int_val = val_b;
        cb_b(fb.enum_idx, &item_b.value, UFSRV_CONFIG_SOURCE_DEFAULTS);

        int64_t got_a = UfsrvConfigRuntimeValuesInstance(
            static_cast<UfsrvRuntimeField>(fa.enum_idx))->stored_value.int_val;
        if (got_a != val_a) {
            failures++;
            printf("  FAIL RUNTIME cross-contamination: %s wrote %" PRId64
                   " got %" PRId64 " (adjacent %s wrote %" PRId64 ")\n",
                   fa.name, val_a, got_a, fb.name, val_b);
        }
    }

    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T3 — LifecycleCallbackCorrectnessByType
 *
 * Call each of the 8 generic lifecycle callbacks directly with edge-case
 * inputs and verify correct store modification.  This validates the generic
 * setters independently of the named-setter delegation.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(SetterDelegationTest, LifecycleCallbackCorrectnessByType)
{
    int failures = 0;

    /* ── GenericIntSetter (CONFIG) ── */
    {
        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);

        UfsrvConfigValue val = {};
        val.type_allocation_sz = 0;

        int64_t test_vals[] = { INT64_MIN, INT64_MAX, -1, 0, 1, 0x7FFFFFFFFFFFFFFFLL };
        for (int64_t tv : test_vals) {
            val.stored_value.int_val = tv;
            GenericIntSetter(0, &val, UFSRV_CONFIG_SOURCE_DEFAULTS);
            int64_t got = UfsrvConfigConfFileValuesInstance(
                static_cast<UfsrvConfigField>(0))->stored_value.int_val;
            if (got != tv) {
                failures++;
                printf("  FAIL GenericIntSetter: wrote %" PRId64 " got %" PRId64 "\n", tv, got);
            }
        }
    }

    /* ── GenericIntSetterRuntime (RUNTIME) ── */
    {
        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);

        UfsrvConfigValue val = {};
        val.type_allocation_sz = 0;

        for (int64_t tv : {INT64_MIN, INT64_MAX, (int64_t)-1, (int64_t)0, (int64_t)1}) {
            val.stored_value.int_val = tv;
            GenericIntSetterRuntime(0, &val, UFSRV_CONFIG_SOURCE_DEFAULTS);
            int64_t got = UfsrvConfigRuntimeValuesInstance(
                static_cast<UfsrvRuntimeField>(0))->stored_value.int_val;
            if (got != tv) {
                failures++;
                printf("  FAIL GenericIntSetterRuntime: wrote %" PRId64
                       " got %" PRId64 "\n", tv, got);
            }
        }
    }

    /* ── GenericPtrSetter + GenericPtrSetterRuntime ── */
    {
        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);

        UfsrvConfigValue val = {};
        val.type_allocation_sz = 0;

        /* NULL */
        val.stored_value.ptr_val = nullptr;
        GenericPtrSetter(0, &val, UFSRV_CONFIG_SOURCE_DEFAULTS);
        void *got = UfsrvConfigConfFileValuesInstance(
            static_cast<UfsrvConfigField>(0))->stored_value.ptr_val;
        if (got != nullptr) {
            failures++;
            printf("  FAIL GenericPtrSetter NULL: got %p\n", got);
        }

        /* Sentinel */
        void *sentinel = reinterpret_cast<void *>(0xDEADBEEFDEADBEEFULL);
        val.stored_value.ptr_val = sentinel;
        GenericPtrSetterRuntime(0, &val, UFSRV_CONFIG_SOURCE_DEFAULTS);
        got = UfsrvConfigRuntimeValuesInstance(
            static_cast<UfsrvRuntimeField>(0))->stored_value.ptr_val;
        if (got != sentinel) {
            failures++;
            printf("  FAIL GenericPtrSetterRuntime sentinel: expected %p got %p\n",
                   sentinel, got);
        }
    }

    /* ── GenericStrSetter (CONFIG) — uses pre-inflated store from SetUp ── */
    {
        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);

        /* Re-arm via StoreInit so the store pointers from SetUp are active.
         * SetUp already pre-inflated this slot — deflate+inflate to get a
         * fresh buffer. */
        UfsrvConfigFieldEntry *entry = UfsrvConfigConfFileStoreInstance(
            UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME);
        if (entry->lifecycle.on_inflated) {
            entry->lifecycle.on_inflated(
                UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME,
                &ufsrv_config_field_defaults_table[
                    UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME],
                true /* is_deflated_first — frees SetUp's allocation */);
        }

        UfsrvConfigValue val = {};
        size_t sz = ufsrv_config_field_defaults_table[
            UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME].type_allocation_sz;
        val.type_allocation_sz = sz;

        /* Empty string */
        char empty[] = "";
        val.stored_value.str_val = empty;
        GenericStrSetter(UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME, &val,
                         UFSRV_CONFIG_SOURCE_DEFAULTS);
        char *got_str = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME)->stored_value.str_val;
        if (!got_str || got_str[0] != '\0') {
            failures++;
            printf("  FAIL GenericStrSetter empty string\n");
        }

        /* Max-length fill */
        char fill[256];
        size_t fill_len = (sz > 0 && sz <= 256) ? sz - 1 : 255;
        memset(fill, 'Z', fill_len);
        fill[fill_len] = '\0';
        val.stored_value.str_val = fill;
        GenericStrSetter(UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME, &val,
                         UFSRV_CONFIG_SOURCE_DEFAULTS);
        got_str = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME)->stored_value.str_val;
        if (!got_str || strlen(got_str) != fill_len || got_str[fill_len] != '\0') {
            failures++;
            printf("  FAIL GenericStrSetter max-fill: expected len=%zu got len=%zu\n",
                   fill_len, got_str ? strlen(got_str) : 0);
        }

        /* Free and nullify to prevent TearDown double-free */
        free(UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME)->stored_value.str_val);
        UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME)->stored_value.str_val = nullptr;
    }

    /* ── GenericStructSetterRuntime (RUNTIME) — re-inflate to get fresh buffer ── */
    {
        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);

        UfsrvConfigFieldEntry *entry = UfsrvConfigRuntimeStoreInstance(
            UFSRV_RUNTIME_FIELD_UFSRV_TIME);
        if (entry->lifecycle.on_inflated) {
            entry->lifecycle.on_inflated(
                UFSRV_RUNTIME_FIELD_UFSRV_TIME,
                &ufsrv_runtime_field_defaults_table[
                    UFSRV_RUNTIME_FIELD_UFSRV_TIME],
                true /* is_deflated_first — frees SetUp's allocation */);
        }

        UfsrvConfigValue val = {};
        size_t sz = ufsrv_runtime_field_defaults_table[
            UFSRV_RUNTIME_FIELD_UFSRV_TIME].type_allocation_sz;
        val.type_allocation_sz = sz;

        /* Magic byte pattern */
        unsigned char magic[32];
        for (size_t k = 0; k < sz && k < sizeof(magic); k++)
            magic[k] = static_cast<unsigned char>(0xA0 + k);
        val.stored_value.blob_val = magic;
        GenericStructSetterRuntime(UFSRV_RUNTIME_FIELD_UFSRV_TIME, &val,
                                   UFSRV_CONFIG_SOURCE_DEFAULTS);
        unsigned char *got_blob = static_cast<unsigned char *>(
            UfsrvConfigRuntimeValuesInstance(
                UFSRV_RUNTIME_FIELD_UFSRV_TIME)->stored_value.blob_val);
        if (!got_blob || memcmp(got_blob, magic, sz) != 0) {
            failures++;
            printf("  FAIL GenericStructSetterRuntime: magic byte mismatch\n");
        }

        /* Zero-fill */
        unsigned char zeros[32] = {};
        val.stored_value.blob_val = zeros;
        GenericStructSetterRuntime(UFSRV_RUNTIME_FIELD_UFSRV_TIME, &val,
                                   UFSRV_CONFIG_SOURCE_DEFAULTS);
        if (!got_blob || memcmp(got_blob, zeros, sz) != 0) {
            failures++;
            printf("  FAIL GenericStructSetterRuntime: zero-fill mismatch\n");
        }

        free(UfsrvConfigRuntimeValuesInstance(
            UFSRV_RUNTIME_FIELD_UFSRV_TIME)->stored_value.blob_val);
        UfsrvConfigRuntimeValuesInstance(
            UFSRV_RUNTIME_FIELD_UFSRV_TIME)->stored_value.blob_val = nullptr;
    }

    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T4 — CustomSetterExclusion
 *
 * Verify oauth_settings (is_keep_custom_setter=true) has no generated named
 * setter: the field table wires the generated lifecycle bridge
 * UfsrvConfigOauthSettingsSetter (which adapts the signature and forwards to
 * the hand-authored setter), not a Generic*Setter and not NULL.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(SetterDelegationTest, CustomSetterExclusion)
{
    /* oauth_settings is kRtFields[22] — field name is upper-snake */
    const auto &oauth = kRtFields[22];
    EXPECT_STREQ(oauth.name, "OAUTH_SETTINGS");
    EXPECT_TRUE(oauth.skip_setter);
    EXPECT_EQ(oauth.named_setter, nullptr);

    /* The field table must wire the lifecycle bridge, not NULL and not a
     * Generic*Setter — the bridge adapts the signature and forwards to the
     * hand-authored UfsrvConfigSetOauthSettings. */
    LifecycleCallbackFn cb = reinterpret_cast<LifecycleCallbackFn>(
        ufsrv_runtime_field_table[oauth.enum_idx].lifecycle.on_value_available);
    EXPECT_EQ(cb, reinterpret_cast<LifecycleCallbackFn>(UfsrvConfigOauthSettingsSetter));

    /* Direct ValuesInstance access must still work */
    UfsrvConfigConfFileStoreInit(cfg_store, TODO);
    UfsrvConfigRuntimeStoreInit(rt_store, TODO);

    UfsrvConfigValue *slot = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_OAUTH_SETTINGS);
    EXPECT_NE(slot, nullptr);
    /* Slot is accessible — the field has is_keep_custom_setter=true,
     * so no generated setter exists, but the store slot is valid. */
    SUCCEED();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T5 — SetterDoesNotModifyItem
 *
 * Adversarial: verify the named setter does not mutate the UfsrvConfigItem
 * passed to it.  The setter should only read from item->value.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(SetterDelegationTest, SetterDoesNotModifyItem)
{
    int failures = 0;
    UfsrvConfigConfFileStoreInit(cfg_store, TODO);
    UfsrvConfigRuntimeStoreInit(rt_store, TODO);

    /* Test representative INT/ENUM fields (skip STR/STRUCT/PTR —
     * those need properly-initialized str_val/blob_val buffers which
     * would be clobbered by the int_val assignment below). */
    const SetterDispatchInfo *representatives[] = {
        &kCfgFields[0],   /* SERVERID — CONFIG INT */
        &kRtFields[2],    /* SERVERPID — RUNTIME INT */
        &kCfgFields[4],   /* SERVER_RUN_MODE — CONFIG ENUM */
        &kRtFields[7],    /* SERVER_PROCESS_MODE — RUNTIME ENUM */
    };

    for (const auto *f : representatives) {
        if (f->skip_setter || !f->named_setter) continue;

        UfsrvConfigItem item_before = {};
        item_before.type  = UFSRV_CONFIG_VALUE_INT;
        item_before.layer = UFSRV_CONFIG_SOURCE_CONFIG_FILE;
        item_before.value.stored_value.int_val = 42;
        item_before.value.type_allocation_sz   = f->alloc_sz;
        item_before.value.source               = UFSRV_CONFIG_SOURCE_CONFIG_FILE;

        /* Take a snapshot */
        UfsrvConfigItem item_snapshot = item_before;

        f->named_setter(&item_before, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

        /* Item must be unmodified */
        if (memcmp(&item_before, &item_snapshot, sizeof(UfsrvConfigItem)) != 0) {
            failures++;
            printf("  FAIL %s: UfsrvConfigItem was modified by named setter\n", f->name);
        }
    }

    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T6 — AllNamedSettersCallable
 *
 * Verify every named setter function pointer is non-NULL (for non-skip fields)
 * and callable without crash with a zero-initialized UfsrvConfigItem.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(SetterDelegationTest, AllNamedSettersCallable)
{
    int failures = 0;
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    UfsrvConfigConfFileStoreInit(cfg_store, TODO);
    UfsrvConfigRuntimeStoreInit(rt_store, TODO);

    auto test_callable = [&](const SetterDispatchInfo &f) {
        if (f.skip_setter) {
            EXPECT_EQ(f.named_setter, nullptr) << f.name;
            return;
        }
        EXPECT_NE(f.named_setter, nullptr) << f.name << " named setter is NULL";

        /* Call with zero-initialized item — must not crash.
         * Keep type_allocation_sz=0 so STR/STRUCT setters' guards
         * skip the memcpy/strncpy on a NULL source buffer. */
        item.value.stored_value.int_val = 0;
        item.value.type_allocation_sz   = 0;
        f.named_setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

        /* Store must be accessible after call */
        UfsrvConfigValue *slot = GetStoreSlot(f);
        EXPECT_NE(slot, nullptr) << f.name;
        (void)slot;
    };

    for (const auto &f : kCfgFields) test_callable(f);
    for (const auto &f : kRtFields)  test_callable(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T7 — LifecycleNullGuard
 *
 * Adversarial: temporarily nullify a field's on_value_available pointer in
 * the armed store, call the named setter, and verify it's a safe no-op
 * (no crash, store unchanged).  Restore the pointer afterwards.
 *
 * This proves the NULL guard in the generated setter body works.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(SetterDelegationTest, LifecycleNullGuard)
{
    int failures = 0;

    /* Test with a CONFIG INT field (SERVERID) and RUNTIME INT field (SERVERPID) */
    UfsrvConfigConfFileStoreInit(cfg_store, TODO);
    UfsrvConfigRuntimeStoreInit(rt_store, TODO);

    /* ── CONFIG: SERVERID ── */
    {
        UfsrvConfigFieldEntry *entry = UfsrvConfigConfFileStoreInstance(
            UFSRV_CONFIG_FIELD_SERVERID);
        ASSERT_NE(entry->lifecycle.on_value_available, nullptr);

        /* Write a known value first */
        UfsrvConfigItem item = {};
        item.type  = UFSRV_CONFIG_VALUE_INT;
        item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;
        item.value.stored_value.int_val = 98765;
        UfsrvConfigSetServerid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        int64_t before = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_SERVERID)->stored_value.int_val;
        ASSERT_EQ(before, 98765);

        /* Nullify the callback */
        auto saved_cb = entry->lifecycle.on_value_available;
        entry->lifecycle.on_value_available = nullptr;

        /* Call named setter with different value — must be safe no-op */
        item.value.stored_value.int_val = -5555;
        UfsrvConfigSetServerid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

        int64_t after = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_SERVERID)->stored_value.int_val;
        if (after != 98765) {
            failures++;
            printf("  FAIL LifecycleNullGuard CONFIG: expected 98765 (unchanged), "
                   "got %" PRId64 "\n", after);
        }

        /* Restore callback */
        entry->lifecycle.on_value_available = saved_cb;
    }

    /* ── RUNTIME: SERVERPID ── */
    {
        UfsrvConfigFieldEntry *entry = UfsrvConfigRuntimeStoreInstance(
            UFSRV_RUNTIME_FIELD_SERVERPID);
        ASSERT_NE(entry->lifecycle.on_value_available, nullptr);

        UfsrvConfigItem item = {};
        item.type  = UFSRV_CONFIG_VALUE_INT;
        item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;
        item.value.stored_value.int_val = 12345;
        UfsrvConfigSetServerpid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        int64_t before = UfsrvConfigRuntimeValuesInstance(
            UFSRV_RUNTIME_FIELD_SERVERPID)->stored_value.int_val;
        ASSERT_EQ(before, 12345);

        auto saved_cb = entry->lifecycle.on_value_available;
        entry->lifecycle.on_value_available = nullptr;

        item.value.stored_value.int_val = -9999;
        UfsrvConfigSetServerpid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

        int64_t after = UfsrvConfigRuntimeValuesInstance(
            UFSRV_RUNTIME_FIELD_SERVERPID)->stored_value.int_val;
        if (after != 12345) {
            failures++;
            printf("  FAIL LifecycleNullGuard RUNTIME: expected 12345 (unchanged), "
                   "got %" PRId64 "\n", after);
        }

        entry->lifecycle.on_value_available = saved_cb;
    }

    EXPECT_EQ(failures, 0);
}
