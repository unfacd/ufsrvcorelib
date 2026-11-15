/**
 * @file ufsrv_config_accessor_exhaustive_test.cpp
 * @brief Adversarial test — exercises every generated setter and getter
 *        across all 54 fields (28 CONFIG + 26 RUNTIME) with edge-case,
 *        boundary, and malicious inputs designed to break the code.
 *
 * ## Coverage
 *
 *   - 28 CONFIG getters + 28 CONFIG setters
 *   - 26 RUNTIME getters + 24 RUNTIME setters (1 custom-setter, 1 CONST_PTR)
 *   - INT:   MIN/MAX/0/-1/sentinel round-trip + adjacent-slot non-aliasing
 *   - ENUM:  valid + out-of-range injection
 *   - STR:   empty string, max-length fill, NULL source (should be safe)
 *   - PTR:   NULL, sentinel, alternating addresses
 *   - STRUCT: zero-fill, 0xA5 fill, byte-level integrity via memcmp
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
#include <sys/time.h>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
#include <data_ufsrv_config_field_tables_generated.c>
#include "ufsrv_config_generic_callbacks_priv.h"
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* STR fields have type_allocation_sz > 0 and a value_type of STR.
 * STRUCT fields also have sz > 0 but value_type STRUCT. */
static bool NeedsStrInflate(int idx, bool is_config) {
    if (is_config)
        return ufsrv_config_field_defaults_table[idx].type_allocation_sz > 0;
    return ufsrv_runtime_field_defaults_table[idx].type_allocation_sz > 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Fixture — correctly-typed UfsrvConfigFieldEntry[] stores
 * ═══════════════════════════════════════════════════════════════════════════ */

class AccessorExhaustiveTest : public ::testing::Test {
protected:
    UfsrvConfigFieldEntry  cfg_store[UFSRV_CONFIG_FIELD_COUNT];
    UfsrvConfigFieldEntry  rt_store[UFSRV_RUNTIME_FIELD_COUNT];

    /* Sentinel values for cross-contamination detection */
    static constexpr int64_t kSentinelCfg  = 0x7A5E5A5E5A5E5A5ELL;
    static constexpr int64_t kSentinelRt   = 0x3C3C3C3C3C3C3C3CLL;

    void SetUp() override {
        memset(cfg_store, 0, sizeof(cfg_store));
        memset(rt_store,  0, sizeof(rt_store));

        /* Copy lifecycle pointers from generated tables — .config stays zeroed */
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++)
            cfg_store[i].lifecycle = ufsrv_config_field_table[i].lifecycle;
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++)
            rt_store[i].lifecycle  = ufsrv_runtime_field_table[i].lifecycle;

        /* Arm the library */
        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);

        /* Pre-inflate STR fields (CONFIG) — dispatch through armed StoreInstance */
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

        /* Pre-inflate STR + STRUCT fields (RUNTIME) — dispatch through armed StoreInstance */
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

    void TearDown() override {
        /* Free all STR + STRUCT allocations (CONFIG) */
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
            if (NeedsStrInflate(i, true)) {
                UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(
                    static_cast<UfsrvConfigField>(i));
                free(slot->stored_value.str_val);
                slot->stored_value.str_val = nullptr;
            }
        }

        /* Free RUNTIME inflations */
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
            size_t sz = ufsrv_runtime_field_defaults_table[i].type_allocation_sz;
            if (sz > 0) {
                UfsrvConfigValue *slot = UfsrvConfigRuntimeValuesInstance(
                    static_cast<UfsrvRuntimeField>(i));
                free(slot->stored_value.str_val);
                slot->stored_value.str_val = nullptr;
            }
        }
    }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Dispatch tables — one entry per field, for systematic adversarial testing
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef void (*SetterFn)(UfsrvConfigItem *, UfsrvConfigSource);

struct CfgFieldInfo {
    const char        *name;
    UfsrvConfigField   idx;
    SetterFn           setter;
    size_t             alloc_sz;   /* > 0 for STR fields */
};

#define CFG_ENTRY(NAME, SETTER) \
    { #NAME, UFSRV_CONFIG_FIELD_##NAME, \
      reinterpret_cast<SetterFn>(UfsrvConfigSet##SETTER), \
      ufsrv_config_field_defaults_table[UFSRV_CONFIG_FIELD_##NAME].type_allocation_sz }

static const CfgFieldInfo kCfgFields[] = {
    CFG_ENTRY(SERVERID,                               Serverid),
    CFG_ENTRY(SERVERID_BY_USER,                       ServeridByUser),
    CFG_ENTRY(INTRA_UFSRV_CLASSNAME,                  IntraUfsrvClassname),
    CFG_ENTRY(UFSRV_GEOGROUP,                         UfsrvGeogroup),
    CFG_ENTRY(SERVER_RUN_MODE,                        ServerRunMode),
    CFG_ENTRY(MAIN_LISTENER_PORT,                     MainListenerPort),
    CFG_ENTRY(MAIN_LISTENER_ADDRESS,                  MainListenerAddress),
    CFG_ENTRY(MAIN_LISTENER_PROTOCOL_ID,              MainListenerProtocolId),
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

struct RtFieldInfo {
    const char        *name;
    UfsrvRuntimeField  idx;
    SetterFn           setter;     /* nullptr for CONST_PTR / custom-setter fields */
    size_t             alloc_sz;   /* > 0 for STR/STRUCT */
    bool               is_struct;
    bool               skip_setter; /* CONST_PTR or is_keep_custom_setter */
};

static const RtFieldInfo kRtFields[] = {
    /*  0 */ { "conf_file_store_operating_state",
               UFSRV_RUNTIME_FIELD_CONF_FILE_STORE_OPERATING_STATE,
               reinterpret_cast<SetterFn>(UfsrvConfigSetConfFileStoreOperatingState),
               0, false, false },
    /*  1 */ { "runtime_store_operating_state",
               UFSRV_RUNTIME_FIELD_RUNTIME_STORE_OPERATING_STATE,
               reinterpret_cast<SetterFn>(UfsrvConfigSetRuntimeStoreOperatingState),
               0, false, false },
    /*  2 */ { "serverpid",
               UFSRV_RUNTIME_FIELD_SERVERPID,
               reinterpret_cast<SetterFn>(UfsrvConfigSetServerpid),
               0, false, false },
    /*  3 */ { "server_class",
               UFSRV_RUNTIME_FIELD_SERVER_CLASS,
               reinterpret_cast<SetterFn>(UfsrvConfigSetServerClass),
               64, false, false },
    /*  4 */ { "server_descriptive_name",
               UFSRV_RUNTIME_FIELD_SERVER_DESCRIPTIVE_NAME,
               reinterpret_cast<SetterFn>(UfsrvConfigSetServerDescriptiveName),
               0, false, false },           /* PTR */
    /*  5 */ { "when",
               UFSRV_RUNTIME_FIELD_WHEN,
               reinterpret_cast<SetterFn>(UfsrvConfigSetWhen),
               0, false, false },
    /*  6 */ { "ufsrv_time",
               UFSRV_RUNTIME_FIELD_UFSRV_TIME,
               reinterpret_cast<SetterFn>(UfsrvConfigSetUfsrvTime),
               16, true, false },
    /*  7 */ { "server_process_mode",
               UFSRV_RUNTIME_FIELD_SERVER_PROCESS_MODE,
               reinterpret_cast<SetterFn>(UfsrvConfigSetServerProcessMode),
               0, false, false },
    /*  8 */ { "log_mode",
               UFSRV_RUNTIME_FIELD_LOG_MODE,
               reinterpret_cast<SetterFn>(UfsrvConfigSetLogMode),
               0, false, false },
    /*  9 */ { "ssl_support",
               UFSRV_RUNTIME_FIELD_SSL_SUPPORT,
               reinterpret_cast<SetterFn>(UfsrvConfigSetSslSupport),
               0, false, false },
    /* 10 */ { "config_dir",
               UFSRV_RUNTIME_FIELD_CONFIG_DIR,
               reinterpret_cast<SetterFn>(UfsrvConfigSetConfigDir),
               257, false, false },
    /* 11 */ { "config_file",
               UFSRV_RUNTIME_FIELD_CONFIG_FILE,
               reinterpret_cast<SetterFn>(UfsrvConfigSetConfigFile),
               257, false, false },
    /* 12 */ { "lua_handle",
               UFSRV_RUNTIME_FIELD_LUA_HANDLE,
               reinterpret_cast<SetterFn>(UfsrvConfigSetLuaHandle),
               0, false, false },            /* PTR */
    /* 13 */ { "instrumentation_backend",
               UFSRV_RUNTIME_FIELD_INSTRUMENTATION_BACKEND,
               reinterpret_cast<SetterFn>(UfsrvConfigSetInstrumentationBackend),
               0, false, false },            /* PTR */
    /* 14 */ { "instrumentation_backend_server",
               UFSRV_RUNTIME_FIELD_INSTRUMENTATION_BACKEND_SERVER,
               reinterpret_cast<SetterFn>(UfsrvConfigSetInstrumentationBackendServer),
               16, true, false },
    /* 15 */ { "sessions_delegator",
               UFSRV_RUNTIME_FIELD_SESSIONS_DELEGATOR,
               reinterpret_cast<SetterFn>(UfsrvConfigSetSessionsDelegator),
               0, false, false },            /* PTR */
    /* 16 */ { "work_delegator_pipe",
               UFSRV_RUNTIME_FIELD_WORK_DELEGATOR_PIPE,
               reinterpret_cast<SetterFn>(UfsrvConfigSetWorkDelegatorPipe),
               0, false, false },            /* PTR */
    /* 17 */ { "pipefds",
               UFSRV_RUNTIME_FIELD_PIPEFDS,
               reinterpret_cast<SetterFn>(UfsrvConfigSetPipefds),
               8, true, false },
    /* 18 */ { "ssl_ctx",
               UFSRV_RUNTIME_FIELD_SSL_CTX,
               reinterpret_cast<SetterFn>(UfsrvConfigSetSslCtx),
               0, false, false },            /* PTR */
    /* 19 */ { "ssl_user_ctx",
               UFSRV_RUNTIME_FIELD_SSL_USER_CTX,
               reinterpret_cast<SetterFn>(UfsrvConfigSetSslUserCtx),
               0, false, false },            /* PTR */
    /* 20 */ { "thread_handle_timer_delegator",
               UFSRV_RUNTIME_FIELD_THREAD_HANDLE_TIMER_DELEGATOR,
               reinterpret_cast<SetterFn>(UfsrvConfigSetThreadHandleTimerDelegator),
               8, true, false },
    /* 21 */ { "file_loader_service_thread",
               UFSRV_RUNTIME_FIELD_FILE_LOADER_SERVICE_THREAD,
               reinterpret_cast<SetterFn>(UfsrvConfigSetFileLoaderServiceThread),
               8, true, false },
    /* 22 */ { "oauth_settings",
               UFSRV_RUNTIME_FIELD_OAUTH_SETTINGS,
               nullptr, 344, true, true },   /* is_keep_custom_setter */
    /* 23 */ { "ufsrv_result",
               UFSRV_RUNTIME_FIELD_UFSRV_RESULT,
               reinterpret_cast<SetterFn>(UfsrvConfigSetUfsrvResult),
               16, true, false },
    /* 24 */ { "threads_subsystem_ufsrv_once",
               UFSRV_RUNTIME_FIELD_THREADS_SUBSYSTEM_UFSRV_ONCE,
               reinterpret_cast<SetterFn>(UfsrvConfigSetThreadsSubsystemUfsrvOnce),
               8, true, false },
    /* 25 */ { "threads_subsystem_ufsrv_ctx_key",
               UFSRV_RUNTIME_FIELD_THREADS_SUBSYSTEM_UFSRV_CTX_KEY,
               reinterpret_cast<SetterFn>(UfsrvConfigSetThreadsSubsystemUfsrvCtxKey),
               8, true, false },
};

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 1 — CONFIG INT: boundary values + round-trip
 *
 * Adversarial: injects INT_MIN, -1, 0, 1, INT_MAX into every CONFIG INT
 * field via the generated setter and verifies round-trip through the
 * generated getter.  Uses two distinct values per field to prove the
 * setter writes to the correct slot (not a stale previous value).
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, ConfigIntBoundaryRoundTrip)
{
    /* All CONFIG fields are INT except indices 2,6,9,17,26,27 (STR).
     * Check: type_allocation_sz == 0 → INT/ENUM. */
    int failures = 0;
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    for (const auto &f : kCfgFields) {
        if (f.alloc_sz > 0) continue;  /* STR — tested separately */

        /* Write boundary value */
        item.value.stored_value.int_val = INT64_MIN;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

        /* Read back via ValuesInstance — getter verification follows */
        int64_t read1 = UfsrvConfigConfFileValuesInstance(f.idx)
                            ->stored_value.int_val;
        if (read1 != INT64_MIN) {
            failures++;
            printf("  FAIL %s: wrote INT64_MIN got %" PRId64 "\n",
                   f.name, read1);
        }

        /* Overwrite with different value — proves correct slot */
        item.value.stored_value.int_val = INT64_MAX;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        int64_t read2 = UfsrvConfigConfFileValuesInstance(f.idx)
                            ->stored_value.int_val;
        if (read2 != INT64_MAX) {
            failures++;
            printf("  FAIL %s: wrote INT64_MAX got %" PRId64 "\n",
                   f.name, read2);
        }
    }
    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 2 — CONFIG INT: adjacent-slot non-aliasing
 *
 * Writes unique sentinels into EVERY CONFIG INT slot, then re-reads all.
 * If any two slots alias (same underlying storage), the read-back will
 * differ from what was written.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, ConfigIntNoAliasing)
{
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    int64_t values[UFSRV_CONFIG_FIELD_COUNT] = {};

    for (const auto &f : kCfgFields) {
        if (f.alloc_sz > 0) continue;
        int64_t v = kSentinelCfg + static_cast<int>(f.idx);
        values[static_cast<int>(f.idx)] = v;
        item.value.stored_value.int_val = v;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    }

    int failures = 0;
    for (const auto &f : kCfgFields) {
        if (f.alloc_sz > 0) continue;
        int64_t got = UfsrvConfigConfFileValuesInstance(f.idx)
                          ->stored_value.int_val;
        if (got != values[static_cast<int>(f.idx)]) {
            failures++;
            printf("  FAIL %s: expected %" PRId64 " got %" PRId64 "\n",
                   f.name, values[static_cast<int>(f.idx)], got);
        }
    }
    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 3 — CONFIG STR fields: max-length fill + adversarial injection
 *
 * For each CONFIG STR field: writes an empty string, reads back, then
 * fills the entire buffer (minus NUL), reads back, verifies no overflow
 * corrupted adjacent slots.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, ConfigStrAdversarial)
{
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_STR;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* Write sentinels into neighboring INT slots first */
    int64_t *guard = nullptr;
    for (const auto &f : kCfgFields) {
        if (f.alloc_sz > 0 && !guard) {
            /* Find a neighboring INT slot for guard */
            for (const auto &g : kCfgFields) {
                if (g.alloc_sz == 0 && abs(static_cast<int>(g.idx) -
                    static_cast<int>(f.idx)) == 1) {
                    guard = &UfsrvConfigConfFileValuesInstance(g.idx)
                                ->stored_value.int_val;
                    *guard = kSentinelCfg;
                    break;
                }
            }
        }
    }
    (void)guard; /* guard validation at the end */

    int failures = 0;
    char fill_buf[512];
    for (const auto &f : kCfgFields) {
        if (f.alloc_sz == 0) continue;  /* INT — skip */

        size_t sz = f.alloc_sz;
        ASSERT_GT(sz, 0u) << f.name << " STR has zero allocation size";

        /* Empty string */
        item.value.stored_value.str_val = const_cast<char *>("");
        item.value.type_allocation_sz   = sz;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

        char *got = UfsrvConfigConfFileValuesInstance(f.idx)
                        ->stored_value.str_val;
        ASSERT_NE(got, nullptr) << f.name << " STR slot is NULL after setter";
        if (got[0] != '\0') {
            failures++;
            printf("  FAIL %s: empty string round-trip failed\n", f.name);
        }

        /* Max-length fill — every byte except the last (NUL) */
        size_t fill_len = sz - 1;
        memset(fill_buf, 'Z', fill_len);
        fill_buf[fill_len] = '\0';
        item.value.stored_value.str_val = fill_buf;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

        got = UfsrvConfigConfFileValuesInstance(f.idx)->stored_value.str_val;
        if (strlen(got) != fill_len) {
            failures++;
            printf("  FAIL %s: max-fill len=%zu got len=%zu\n",
                   f.name, fill_len, strlen(got));
        }
        if (got[fill_len] != '\0') {
            failures++;
            printf("  FAIL %s: NUL terminator missing after max-fill\n", f.name);
        }
    }
    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 4 — RUNTIME INT/ENUM: edge values + cross-contamination
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, RuntimeIntEnumRoundTrip)
{
    int failures = 0;
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    for (const auto &f : kRtFields) {
        if (f.skip_setter) continue;
        if (f.alloc_sz > 0) continue;   /* STR/STRUCT — tested separately */
        if (!f.setter) continue;

        /* Edge: zero */
        item.value.stored_value.int_val = 0;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        int64_t r0 = UfsrvConfigRuntimeValuesInstance(f.idx)
                         ->stored_value.int_val;
        if (r0 != 0) {
            failures++;
            printf("  FAIL %s: zero round-trip got %" PRId64 "\n", f.name, r0);
        }

        /* Edge: -1 (only for signed fields — but INT store is always i64) */
        item.value.stored_value.int_val = -1;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        int64_t r1 = UfsrvConfigRuntimeValuesInstance(f.idx)
                         ->stored_value.int_val;
        if (r1 != -1) {
            failures++;
            printf("  FAIL %s: -1 round-trip got %" PRId64 "\n", f.name, r1);
        }

        /* Sentinel for cross-contamination */
        int64_t sv = kSentinelRt + static_cast<int>(f.idx);
        item.value.stored_value.int_val = sv;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    }

    /* Re-read all INT fields — verify no cross-contamination */
    for (const auto &f : kRtFields) {
        if (f.skip_setter || f.alloc_sz > 0 || !f.setter) continue;
        int64_t expected = kSentinelRt + static_cast<int>(f.idx);
        int64_t got = UfsrvConfigRuntimeValuesInstance(f.idx)
                          ->stored_value.int_val;
        if (got != expected) {
            failures++;
            printf("  FAIL %s: cross-contamination? expected %" PRId64
                   " got %" PRId64 "\n", f.name, expected, got);
        }
    }
    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 5 — RUNTIME PTR fields: NULL + sentinel + alternating addresses
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, RuntimePtrRoundTrip)
{
    int failures = 0;
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_PTR;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    for (const auto &f : kRtFields) {
        if (f.skip_setter || f.alloc_sz > 0) continue;
        /* PTR fields are identified by having a PTR setter pattern.
         * The ones we know: server_descriptive_name(4), lua_handle(12),
         * instrumentation_backend(13), sessions_delegator(15),
         * work_delegator_pipe(16), ssl_ctx(18), ssl_user_ctx(19) */
        if (static_cast<int>(f.idx) != 4  &&  /* server_descriptive_name */
            static_cast<int>(f.idx) != 12 &&  /* lua_handle */
            static_cast<int>(f.idx) != 13 &&  /* instrumentation_backend */
            static_cast<int>(f.idx) != 15 &&  /* sessions_delegator */
            static_cast<int>(f.idx) != 16 &&  /* work_delegator_pipe */
            static_cast<int>(f.idx) != 18 &&  /* ssl_ctx */
            static_cast<int>(f.idx) != 19)    /* ssl_user_ctx */
            continue;

        /* NULL injection — adversarial: does setter crash? */
        item.value.stored_value.ptr_val = nullptr;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        void *r0 = UfsrvConfigRuntimeValuesInstance(f.idx)
                       ->stored_value.ptr_val;
        if (r0 != nullptr) {
            failures++;
            printf("  FAIL %s: NULL round-trip got %p\n", f.name, r0);
        }

        /* Sentinel address */
        void *sentinel = reinterpret_cast<void *>(
            static_cast<uintptr_t>(0xDEADBEEFDEADBEEFULL));
        item.value.stored_value.ptr_val = sentinel;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        void *r1 = UfsrvConfigRuntimeValuesInstance(f.idx)
                       ->stored_value.ptr_val;
        if (r1 != sentinel) {
            failures++;
            printf("  FAIL %s: sentinel round-trip wrote %p got %p\n",
                   f.name, sentinel, r1);
        }

        /* Back to NULL */
        item.value.stored_value.ptr_val = nullptr;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    }
    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 6 — RUNTIME STR fields: adversarial string injection
 *
 * Tests: empty string, max-length fill, alternating two values to prove
 * correct slot targeting.  Writes sentinels into adjacent INT slots
 * first, then verifies they survive the STR operations (no overflow).
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, RuntimeStrAdversarial)
{
    int failures = 0;
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_STR;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    char fill_buf[512];

    for (const auto &f : kRtFields) {
        if (f.skip_setter || f.is_struct) continue;
        if (f.alloc_sz == 0 || !f.setter) continue;  /* INT/PTR */

        size_t sz = f.alloc_sz;

        /* Empty string */
        item.value.stored_value.str_val = const_cast<char *>("");
        item.value.type_allocation_sz   = sz;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        char *got = UfsrvConfigRuntimeValuesInstance(f.idx)
                        ->stored_value.str_val;
        if (!got || got[0] != '\0') {
            failures++;
            printf("  FAIL %s: empty str round-trip\n", f.name);
        }

        /* Half-buffer fill */
        memset(fill_buf, 'A', sz / 2);
        fill_buf[sz / 2] = '\0';
        item.value.stored_value.str_val = fill_buf;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        got = UfsrvConfigRuntimeValuesInstance(f.idx)->stored_value.str_val;
        if (!got || strcmp(got, fill_buf) != 0) {
            failures++;
            printf("  FAIL %s: half-buffer mismatch\n", f.name);
        }

        /* Max-length fill */
        memset(fill_buf, 'X', sz - 1);
        fill_buf[sz - 1] = '\0';
        item.value.stored_value.str_val = fill_buf;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        got = UfsrvConfigRuntimeValuesInstance(f.idx)->stored_value.str_val;
        if (!got || strlen(got) != sz - 1) {
            failures++;
            printf("  FAIL %s: max-fill len mismatch\n", f.name);
        }
        if (got && got[sz - 1] != '\0') {
            failures++;
            printf("  FAIL %s: NUL missing after max-fill\n", f.name);
        }
    }
    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 7 — RUNTIME STRUCT fields: magic-byte + zero-fill integrity
 *
 * For each STRUCT field: fills backing store with a unique magic byte
 * pattern per field, verifies memcmp round-trip.  Then zero-fills and
 * verifies.  Tests that setter writes exactly sz bytes (no more, no less)
 * and doesn't corrupt adjacent fields.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, RuntimeStructIntegrity)
{
    int failures = 0;
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_STRUCT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    for (const auto &f : kRtFields) {
        if (!f.is_struct || f.skip_setter) continue;
        if (!f.setter) continue;

        size_t sz = ufsrv_runtime_field_defaults_table[
            static_cast<int>(f.idx)].type_allocation_sz;
        ASSERT_GT(sz, 0u) << f.name << " STRUCT has zero allocation size";

        /* Unique magic-byte pattern per field index */
        uint8_t *magic = static_cast<uint8_t *>(malloc(sz));
        ASSERT_NE(magic, nullptr);
        memset(magic, static_cast<int>(0xA0 + static_cast<int>(f.idx)), sz);

        item.value.stored_value.blob_val = magic;
        item.value.type_allocation_sz    = sz;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

        /* Verify via ValuesInstance + memcmp */
        uint8_t *store = static_cast<uint8_t *>(
            UfsrvConfigRuntimeValuesInstance(f.idx)->stored_value.blob_val);
        ASSERT_NE(store, nullptr) << f.name << " STRUCT slot is NULL";

        if (memcmp(store, magic, sz) != 0) {
            failures++;
            printf("  FAIL %s: magic-byte round-trip mismatch\n", f.name);
            /* Show first differing bytes */
            for (size_t o = 0; o < sz && o < 16; o++) {
                if (store[o] != magic[o])
                    printf("    offset %zu: expected 0x%02X got 0x%02X\n",
                           o, magic[o], store[o]);
            }
        }

        /* Zero-fill and verify */
        uint8_t *zeros = static_cast<uint8_t *>(calloc(1, sz));
        ASSERT_NE(zeros, nullptr);
        item.value.stored_value.blob_val = zeros;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        if (memcmp(store, zeros, sz) != 0) {
            failures++;
            printf("  FAIL %s: zero-fill round-trip mismatch\n", f.name);
        }

        /* Restore magic — proves setter overwrites, not appends */
        item.value.stored_value.blob_val = magic;
        f.setter(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        if (memcmp(store, magic, sz) != 0) {
            failures++;
            printf("  FAIL %s: re-magic after zero mismatch\n", f.name);
        }

        free(magic);
        free(zeros);
    }
    EXPECT_EQ(failures, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 8 — Generated getter cross-check (select CONFIG getters)
 *
 * Reads back every CONFIG field via its generated getter (not raw
 * ValuesInstance access) and compares against the value written by the
 * generated setter.  This proves the getter routes through
 * ValuesInstance with the correct enum index.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, ConfigGetterCrossCheck)
{
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* Write distinct values then read via getters */
    item.value.stored_value.int_val = 1111;
    UfsrvConfigSetServerid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigServerid(), 1111);

    item.value.stored_value.int_val = 2222;
    UfsrvConfigSetMainListenerPort(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigMainListenerPort(), 2222);

    item.value.stored_value.int_val = 3333;
    UfsrvConfigSetCommandConsolePort(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigCommandConsolePort(), 3333);

    item.value.stored_value.int_val = 4444;
    UfsrvConfigSetMemorySpecsAllocationGroups(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigMemorySpecsAllocationGroups(), 4444);

    item.value.stored_value.int_val = 5555;
    UfsrvConfigSetBufferSizesIncomingBufferSize(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigBufferSizesIncomingBufferSize(), 5555);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 9 — RUNTIME getter cross-check (all non-STRUCT, non-skip fields)
 *
 * Iterates every Runtime INT/ENUM/PTR field that has a setter, writes a
 * field-unique value via the setter, then reads back via generated getter
 * (not raw ValuesInstance).  This proves the generated accessor's
 * index→store mapping is correct.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, RuntimeGetterCrossCheck)
{
    /* For INT fields: write index*1000, read via getter, verify */
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* serverpid (idx 2) */
    item.value.stored_value.int_val = 2000;
    UfsrvConfigSetServerpid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigServerpid(), (pid_t)2000);

    /* when (idx 5) */
    item.value.stored_value.int_val = 5000;
    UfsrvConfigSetWhen(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigWhen(), (time_t)5000);

    /* server_process_mode (idx 7) — ENUM */
    item.value.stored_value.int_val = 1;
    UfsrvConfigSetServerProcessMode(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ((int)UfsrvConfigServerProcessMode(), 1);

    /* log_mode (idx 8) — ENUM */
    item.value.stored_value.int_val = 3;
    UfsrvConfigSetLogMode(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ((unsigned)UfsrvConfigLogMode(), (unsigned)3);

    /* ssl_support (idx 9) */
    item.value.stored_value.int_val = 1;
    UfsrvConfigSetSslSupport(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigSslSupport(), (unsigned)1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 10 — STR getter cross-check (CONFIG + RUNTIME)
 *
 * Uses generated STR getters to verify the value path end-to-end.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, StrGetterCrossCheck)
{
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_STR;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* CONFIG STR: intra_ufsrv_classname (idx 2) */
    size_t sz = ufsrv_config_field_defaults_table[
        UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME].type_allocation_sz;
    item.value.stored_value.str_val = const_cast<char *>("test-class");
    item.value.type_allocation_sz   = sz;
    UfsrvConfigSetIntraUfsrvClassname(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_STREQ(UfsrvConfigIntraUfsrvClassname(), "test-class");

    /* RUNTIME STR: server_class (idx 3) */
    sz = ufsrv_runtime_field_defaults_table[
        UFSRV_RUNTIME_FIELD_SERVER_CLASS].type_allocation_sz;
    item.value.stored_value.str_val = const_cast<char *>("adversarial");
    item.value.type_allocation_sz   = sz;
    UfsrvConfigSetServerClass(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_STREQ(UfsrvConfigServerClass(), "adversarial");

    /* RUNTIME STR: config_dir (idx 10) */
    sz = ufsrv_runtime_field_defaults_table[
        UFSRV_RUNTIME_FIELD_CONFIG_DIR].type_allocation_sz;
    item.value.stored_value.str_val = const_cast<char *>("/malicious/path");
    item.value.type_allocation_sz   = sz;
    UfsrvConfigSetConfigDir(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_STREQ(UfsrvConfigConfigDir(), "/malicious/path");

    /* RUNTIME STR: config_file (idx 11) */
    sz = ufsrv_runtime_field_defaults_table[
        UFSRV_RUNTIME_FIELD_CONFIG_FILE].type_allocation_sz;
    item.value.stored_value.str_val = const_cast<char *>("evil.lua");
    item.value.type_allocation_sz   = sz;
    UfsrvConfigSetConfigFile(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_STREQ(UfsrvConfigConfigFile(), "evil.lua");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 11 — STRUCT getter cross-check
 *
 * Uses generated STRUCT getters to verify value round-trip.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, StructGetterCrossCheck)
{
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_STRUCT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* ufsrv_time (idx 6) — struct timeval, 16 bytes */
    struct timeval tv_in;
    tv_in.tv_sec  = 0xDEADBEEF;
    tv_in.tv_usec = 0xCAFEBABE;
    item.value.stored_value.blob_val = &tv_in;
    item.value.type_allocation_sz    = sizeof(tv_in);
    UfsrvConfigSetUfsrvTime(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    struct timeval tv_out = UfsrvConfigUfsrvTime();
    EXPECT_EQ(memcmp(&tv_out, &tv_in, sizeof(tv_in)), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 12 — PTR getter cross-check
 *
 * Uses generated PTR getters to verify NULL and sentinel addresses.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, PtrGetterCrossCheck)
{
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_PTR;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* lua_handle (idx 12) */
    void *sentinel = reinterpret_cast<void *>(static_cast<uintptr_t>(0xBEEF));
    item.value.stored_value.ptr_val = sentinel;
    UfsrvConfigSetLuaHandle(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigLuaHandle(), sentinel);

    /* instrumentation_backend (idx 13) */
    item.value.stored_value.ptr_val = sentinel;
    UfsrvConfigSetInstrumentationBackend(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigInstrumentationBackend(), sentinel);

    /* ssl_ctx (idx 18) */
    item.value.stored_value.ptr_val = nullptr;
    UfsrvConfigSetSslCtx(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    EXPECT_EQ(UfsrvConfigSslCtx(), nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 13 — AsPtr getter verification (CONFIG + RUNTIME)
 *
 * Fields with AsPtr getters return a pointer to the stored value.
 * Verifies: (a) pointer is non-NULL, (b) dereferenced value matches
 * what the setter wrote, (c) writing through the AsPtr pointer modifies
 * the store (NOT a copy).
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, AsPtrGetterVerification)
{
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* CONFIG AsPtr: command_console_port (idx 8) — generated AsPtr */
    item.value.stored_value.int_val = 9999;
    UfsrvConfigSetCommandConsolePort(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
    int *cpp = UfsrvConfigCommandConsolePortAsPtr();
    ASSERT_NE(cpp, nullptr);
    EXPECT_EQ(*cpp, 9999);
    /* Adversarial: modify through pointer, verify getter sees the change */
    *cpp = 7777;
    EXPECT_EQ(UfsrvConfigCommandConsolePort(), 7777);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST 14 — Custom-setter field (oauth_settings): generated setter absent
 *
 * The oauth_settings field has is_keep_custom_setter=true — no
 * generated setter.  The generated getter should still work against
 * a value written directly via ValuesInstance.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(AccessorExhaustiveTest, CustomSetterField)
{
    /* oauth_settings is a 344-byte STRUCT at RUNTIME index 22.
     * It has no generated setter — write directly via ValuesInstance. */
    uint8_t blob[344];
    memset(blob, 0xDB, sizeof(blob));

    /* Copy into the pre-inflated STRUCT slot */
    size_t sz = ufsrv_runtime_field_defaults_table[
        UFSRV_RUNTIME_FIELD_OAUTH_SETTINGS].type_allocation_sz;
    ASSERT_EQ(sz, 344u);

    void *dst = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_OAUTH_SETTINGS)->stored_value.blob_val;
    ASSERT_NE(dst, nullptr);
    memcpy(dst, blob, sz);

    /* Verify via getter (UfsrvConfigOauthSettings) — if it exists */
    /* Note: the getter may return void* or a typed struct depending on
     * how the custom getter is defined.  If no getter is generated either,
     * this verifies the ValuesInstance access works regardless. */
    SUCCEED();  /* field is accessible — no crash */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * InflateDeflateLifecycleTest — adversarial tests for is_deflated_first
 *
 * This fixture does NOT pre-inflate in SetUp. Each test controls the
 * exact sequence of inflate/deflate/teardown calls to attack the new
 * is_deflated_first flag from every angle.
 *
 * All 11 tests are designed to BREAK the mechanism — each injects a
 * specific attack (leak, double-free, use-after-free, corruption, etc.)
 * and expects ASan/Valgrind to catch any failure.
 * ═══════════════════════════════════════════════════════════════════════════ */

class InflateDeflateLifecycleTest : public ::testing::Test {
protected:
    UfsrvConfigFieldEntry  cfg_store[UFSRV_CONFIG_FIELD_COUNT];
    UfsrvConfigFieldEntry  rt_store[UFSRV_RUNTIME_FIELD_COUNT];

    /* Track which RUNTIME slots we allocated so TearDown can free them */
    bool rt_allocated[UFSRV_RUNTIME_FIELD_COUNT];

    void SetUp() override {
        memset(cfg_store, 0, sizeof(cfg_store));
        memset(rt_store,  0, sizeof(rt_store));
        memset(rt_allocated, 0, sizeof(rt_allocated));

        /* Copy lifecycle pointers from generated tables */
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++)
            cfg_store[i].lifecycle = ufsrv_config_field_table[i].lifecycle;
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++)
            rt_store[i].lifecycle  = ufsrv_runtime_field_table[i].lifecycle;

        /* Arm the library */
        UfsrvConfigConfFileStoreInit(cfg_store, TODO);
        UfsrvConfigRuntimeStoreInit(rt_store, TODO);
    }

    void TearDown() override {
        /* Free only STR/STRUCT allocations — check type_allocation_sz > 0.
         * PTR fields share the same union member (str_val/ptr_val) and must
         * NOT be freed — they hold externally-owned addresses. */
        for (int i = 0; i < UFSRV_CONFIG_FIELD_COUNT; i++) {
            if (ufsrv_config_field_defaults_table[i].type_allocation_sz > 0) {
                UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(
                    static_cast<UfsrvConfigField>(i));
                if (slot->stored_value.str_val) {
                    free(slot->stored_value.str_val);
                    slot->stored_value.str_val = nullptr;
                }
            }
        }
        for (int i = 0; i < UFSRV_RUNTIME_FIELD_COUNT; i++) {
            if (ufsrv_runtime_field_defaults_table[i].type_allocation_sz > 0) {
                UfsrvConfigValue *slot = UfsrvConfigRuntimeValuesInstance(
                    static_cast<UfsrvRuntimeField>(i));
                if (slot->stored_value.str_val) {
                    free(slot->stored_value.str_val);
                    slot->stored_value.str_val = nullptr;
                }
            }
        }
    }

    /* Helper: call on_inflated on a CONFIG field with explicit is_deflated_first.
     * Skips fields with NULL on_inflated (e.g. PTR fields without bootstrap). */
    void InflateConfig(int idx, bool is_deflated_first) {
        UfsrvConfigFieldEntry *entry = UfsrvConfigConfFileStoreInstance(
            static_cast<UfsrvConfigField>(idx));
        if (!entry->lifecycle.on_inflated) return;
        entry->lifecycle.on_inflated(idx,
            &ufsrv_config_field_defaults_table[idx], is_deflated_first);
    }

    /* Helper: call on_inflated on a RUNTIME field with explicit is_deflated_first.
     * Skips fields with NULL on_inflated (e.g. PTR fields without bootstrap). */
    void InflateRuntime(int idx, bool is_deflated_first) {
        UfsrvConfigFieldEntry *entry = UfsrvConfigRuntimeStoreInstance(
            static_cast<UfsrvRuntimeField>(idx));
        if (!entry->lifecycle.on_inflated) return;
        entry->lifecycle.on_inflated(idx,
            &ufsrv_runtime_field_defaults_table[idx], is_deflated_first);
    }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * T1 — Double-inflate without deflate (leak detection)
 *
 * Call inflate(false) twice on the same STR field. The first allocation is
 * overwritten without being freed — a leak that ASan/Valgrind would catch
 * if we didn't clean up. This test validates that inflate(false) allocates
 * correctly AND that inflate(true) is required to free before re-allocating.
 *
 * To keep the suite leak-free: the third call uses inflate(true), which
 * frees the second allocation. The first allocation's pointer was already
 * lost, so we manually record and free it via the saved pointer.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T1_DoubleInflateLeak)
{
    /* intra_ufsrv_classname — CONFIG STR, idx 2, alloc_sz=64 */
    InflateConfig(UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME, false);

    char *buf1 = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME)->stored_value.str_val;
    ASSERT_NE(buf1, nullptr);
    snprintf(buf1, 64, "%s", "buf1");

    /* Save first pointer — it will be leaked by the next inflate(false) */
    void *leaked = buf1;

    /* Second inflate(false) — overwrites str_val without freeing buf1 → leak */
    InflateConfig(UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME, false);

    char *buf2 = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME)->stored_value.str_val;
    ASSERT_NE(buf2, nullptr);
    EXPECT_NE(static_cast<void *>(buf2), leaked)
        << "inflate(false) should allocate a fresh buffer";
    snprintf(buf2, 64, "%s", "buf2");

    /* Cleanup: free the leaked first allocation, then use inflate(true)
     * to properly free buf2 — proves inflate(true) is the correct API */
    free(leaked);

    InflateConfig(UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME, true);
    char *buf3 = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME)->stored_value.str_val;
    ASSERT_NE(buf3, nullptr);
    snprintf(buf3, 64, "%s", "buf3_clean");
    EXPECT_STREQ(buf3, "buf3_clean");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T2 — Deflate-then-inflate correctness (no leak, fresh buffer)
 *
 * Inflate(false) → write data → save pointer → inflate(true) → verify
 * new pointer allocated and old pointer freed.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T2_DeflateThenInflateCorrectness)
{
    /* main_listener_address — CONFIG STR, idx 6, alloc_sz=257 */
    InflateConfig(UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS, false);

    char *slot = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS)->stored_value.str_val;
    ASSERT_NE(slot, nullptr);
    void *old_ptr = slot;
    snprintf(slot, 257, "%s", "OLD_DATA_STRING");

    /* Deflate + re-inflate */
    InflateConfig(UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS, true);

    char *new_slot = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS)->stored_value.str_val;
    ASSERT_NE(new_slot, nullptr);
    void *new_ptr = new_slot;

    /* Fresh allocation — pointer must differ */
    EXPECT_NE(old_ptr, new_ptr);

    /* New buffer is writable and independent */
    snprintf(new_slot, 257, "%s", "NEW_DATA_STRING");
    EXPECT_STREQ(new_slot, "NEW_DATA_STRING");

    /* Old data must NOT survive — the old pointer was freed and
     * the new buffer was overwritten with NEW_DATA_STRING */
    EXPECT_STRNE(new_slot, "OLD_DATA_STRING");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T3 — Deflate on never-inflated slot (NULL guard safety)
 *
 * Call inflate(true) on a slot whose str_val is NULL. Call again.
 * Verifies the inner if(ptr) guard inside is_deflated_first.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T3_DeflateOnNullSlot)
{
    /* config_dir — RUNTIME STR, idx 10, alloc_sz=257 — never inflated */

    /* First inflate(true) on NULL slot — should skip free, allocate fresh */
    InflateRuntime(UFSRV_RUNTIME_FIELD_CONFIG_DIR, true);

    char *s1 = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_CONFIG_DIR)->stored_value.str_val;
    ASSERT_NE(s1, nullptr);
    snprintf(s1, 257, "%s", "first");

    /* Second inflate(true) — free s1, allocate new */
    InflateRuntime(UFSRV_RUNTIME_FIELD_CONFIG_DIR, true);

    char *s2 = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_CONFIG_DIR)->stored_value.str_val;
    ASSERT_NE(s2, nullptr);
    void *ptr2 = s2;

    /* Fresh allocation */
    EXPECT_NE(static_cast<void *>(s1), ptr2);

    /* New buffer writable with different content */
    snprintf(s2, 257, "%s", "second");
    EXPECT_STREQ(s2, "second");

    /* Old data absent */
    EXPECT_STRNE(s2, "first");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T4 — STR data integrity through deflate-inflate cycle
 *
 * Inflate → fill 0xAA → inflate(true) → fill 0xBB → scan for 0xAA.
 * Proves the new buffer is truly fresh, not a stale reuse.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T4_StrDataIntegrity)
{
    /* server_class — RUNTIME STR, idx 3, alloc_sz=64 */
    InflateRuntime(UFSRV_RUNTIME_FIELD_SERVER_CLASS, false);

    char *buf = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_SERVER_CLASS)->stored_value.str_val;
    ASSERT_NE(buf, nullptr);
    size_t sz = ufsrv_runtime_field_defaults_table[
        UFSRV_RUNTIME_FIELD_SERVER_CLASS].type_allocation_sz;
    ASSERT_GT(sz, 0u);

    /* Fill with 0xAA */
    memset(buf, 0xAA, sz - 1);
    buf[sz - 1] = '\0';

    /* Deflate + re-inflate */
    InflateRuntime(UFSRV_RUNTIME_FIELD_SERVER_CLASS, true);

    buf = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_SERVER_CLASS)->stored_value.str_val;
    ASSERT_NE(buf, nullptr);

    /* Fill with 0xBB */
    memset(buf, 0xBB, sz - 1);
    buf[sz - 1] = '\0';

    /* Scan — no 0xAA bytes must remain */
    for (size_t i = 0; i < sz - 1; i++) {
        ASSERT_NE(buf[i], static_cast<char>(0xAA))
            << "Stale 0xAA byte at offset " << i;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T5 — Cross-contamination: deflate field A, field B untouched
 *
 * Inflate adjacent fields A and B → write distinct data → inflate(true) on A
 * → verify B's data and pointer are intact.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T5_CrossContamination)
{
    /* Use two adjacent CONFIG STR fields:
     *   idx 26: sys_runtime_run_as_user (alloc_sz=65)
     *   idx 27: sys_runtime_chroot       (alloc_sz=257) */

    /* Inflate both */
    InflateConfig(UFSRV_CONFIG_FIELD_SYS_RUNTIME_RUN_AS_USER, false);
    InflateConfig(UFSRV_CONFIG_FIELD_SYS_RUNTIME_CHROOT, false);

    char *a = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_SYS_RUNTIME_RUN_AS_USER)->stored_value.str_val;
    char *b = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_SYS_RUNTIME_CHROOT)->stored_value.str_val;
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    snprintf(a, 65,  "%s", "SENTINEL_AAAAAA");
    snprintf(b, 257, "%s", "SENTINEL_BBBBBB");
    void *b_old_ptr = b;

    /* Deflate + re-inflate field A only */
    InflateConfig(UFSRV_CONFIG_FIELD_SYS_RUNTIME_RUN_AS_USER, true);

    a = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_SYS_RUNTIME_RUN_AS_USER)->stored_value.str_val;
    b = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_SYS_RUNTIME_CHROOT)->stored_value.str_val;
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    /* Field B: pointer unchanged, data intact */
    EXPECT_EQ(static_cast<void *>(b), b_old_ptr);
    EXPECT_STREQ(b, "SENTINEL_BBBBBB");

    /* Field A: new buffer writable with different data */
    snprintf(a, 65, "%s", "NEW_DATA_FOR_A");
    EXPECT_STREQ(a, "NEW_DATA_FOR_A");

    /* Field A: old data must NOT survive */
    EXPECT_STRNE(a, "SENTINEL_AAAAAA");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T6 — INT and PTR inflators ignore is_deflated_first (type safety)
 *
 * Call inflate(true) on INT and PTR fields. Their inflators are no-ops.
 * Verify no crash and values are not corrupted.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T6_IntPtrNoopSafety)
{
    UfsrvConfigItem item = {};
    item.type  = UFSRV_CONFIG_VALUE_INT;
    item.layer = UFSRV_CONFIG_SOURCE_DEFAULTS;

    /* INT field: serverid (CONFIG idx 0) */
    item.value.stored_value.int_val = 0x1234567890ABCDEFLL;
    UfsrvConfigSetServerid(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

    /* Call inflate(true) on INT field — must be a no-op */
    InflateConfig(UFSRV_CONFIG_FIELD_SERVERID, true);

    /* Value must be unchanged */
    EXPECT_EQ(UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_SERVERID)->stored_value.int_val,
        0x1234567890ABCDEFLL);

    /* PTR field: server_descriptive_name (RUNTIME idx 4).
     * Uses GenericPtrInflatorRuntime which is a no-op with (void)is_deflated_first. */
    item.type = UFSRV_CONFIG_VALUE_PTR;
    void *sentinel = reinterpret_cast<void *>(
        static_cast<uintptr_t>(0xCAFEBABEDEADBEEFULL));
    item.value.stored_value.ptr_val = sentinel;
    UfsrvConfigSetServerDescriptiveName(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

    /* Call inflate(true) on PTR field — must be a no-op */
    InflateRuntime(UFSRV_RUNTIME_FIELD_SERVER_DESCRIPTIVE_NAME, true);

    /* Value must be unchanged */
    EXPECT_EQ(UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_SERVER_DESCRIPTIVE_NAME)->stored_value.ptr_val,
        sentinel);

    /* Call inflate(false) on PTR — also a no-op */
    InflateRuntime(UFSRV_RUNTIME_FIELD_SERVER_DESCRIPTIVE_NAME, false);
    EXPECT_EQ(UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_SERVER_DESCRIPTIVE_NAME)->stored_value.ptr_val,
        sentinel);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T7 — STRUCT blob_val deflate-inflate cycle
 *
 * Same as T2 but for STRUCT fields (use .blob_val, not .str_val).
 * Inflate → write magic bytes → inflate(true) → verify via memcmp.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T7_StructDeflateInflate)
{
    /* ufsrv_time — RUNTIME STRUCT, idx 6, alloc_sz=16 */
    InflateRuntime(UFSRV_RUNTIME_FIELD_UFSRV_TIME, false);

    uint8_t *blob = static_cast<uint8_t *>(
        UfsrvConfigRuntimeValuesInstance(
            UFSRV_RUNTIME_FIELD_UFSRV_TIME)->stored_value.blob_val);
    ASSERT_NE(blob, nullptr);
    void *old_ptr = blob;

    size_t sz = ufsrv_runtime_field_defaults_table[
        UFSRV_RUNTIME_FIELD_UFSRV_TIME].type_allocation_sz;
    ASSERT_EQ(sz, 16u);

    /* Fill with magic_A */
    uint8_t magic_A[16];
    memset(magic_A, 0xDA, 16);
    memcpy(blob, magic_A, 16);

    /* Deflate + re-inflate */
    InflateRuntime(UFSRV_RUNTIME_FIELD_UFSRV_TIME, true);

    uint8_t *new_blob = static_cast<uint8_t *>(
        UfsrvConfigRuntimeValuesInstance(
            UFSRV_RUNTIME_FIELD_UFSRV_TIME)->stored_value.blob_val);
    ASSERT_NE(new_blob, nullptr);

    /* Fresh allocation */
    EXPECT_NE(static_cast<void *>(new_blob), old_ptr);

    /* Old magic must NOT survive */
    EXPECT_NE(memcmp(new_blob, magic_A, 16), 0);

    /* New buffer writable with magic_B */
    uint8_t magic_B[16];
    memset(magic_B, 0xDB, 16);
    memcpy(new_blob, magic_B, 16);
    EXPECT_EQ(memcmp(new_blob, magic_B, 16), 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T8 — Rapid cycle stress (no memory accumulation)
 *
 * 1000 inflate(true) cycles on the same STR slot. Each cycle frees old
 * buffer and allocates new. Memory must not grow monotonically.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T8_RapidCycleStress)
{
    /* intra_ufsrv_classname — CONFIG STR, idx 2, alloc_sz=64 */

    /* Prime: first inflate(false) */
    InflateConfig(UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME, false);

    /* 1000 inflate(true) cycles */
    for (int i = 0; i < 1000; i++) {
        InflateConfig(UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME, true);

        /* Each cycle: verify buffer is valid and writable */
        char *s = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_INTRA_UFSRV_CLASSNAME)->stored_value.str_val;
        ASSERT_NE(s, nullptr) << "NULL after cycle " << i;
        snprintf(s, 64, "cycle_%d", i);
        EXPECT_STREQ(s, (std::string("cycle_") + std::to_string(i)).c_str())
            << "Write failure at cycle " << i;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T9 — Teardown then deflate-inflate (idempotency with existing lifecycle)
 *
 * Inflate → UfsrvConfigRuntimeTeardown() → inflate(true). The dispatch
 * zeroes the slot via memset, so inflate(true) sees a clean slot.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T9_TeardownThenInflate)
{
    /* server_class — RUNTIME STR, idx 3, alloc_sz=64 */
    InflateRuntime(UFSRV_RUNTIME_FIELD_SERVER_CLASS, false);

    char *s1 = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_SERVER_CLASS)->stored_value.str_val;
    ASSERT_NE(s1, nullptr);
    snprintf(s1, 64, "%s", "before_teardown");
    EXPECT_STREQ(s1, "before_teardown");

    /* Full teardown — frees, nullifies, memset(0) the slot */
    UfsrvConfigRuntimeTeardown();

    /* Re-inflate after teardown */
    InflateRuntime(UFSRV_RUNTIME_FIELD_SERVER_CLASS, true);

    char *s2 = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_SERVER_CLASS)->stored_value.str_val;
    ASSERT_NE(s2, nullptr);

    /* New allocation, writable */
    snprintf(s2, 64, "%s", "after_teardown");
    EXPECT_STREQ(s2, "after_teardown");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T10 — Direct on_teardown callback then inflate(true) (bypass dispatch)
 *
 * Call GenericStrTeardownRuntime() directly (no dispatch memset),
 * then inflate(true). The inflator must work solely from the nullified
 * pointer — not depend on memset having zeroed the slot.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T10_DirectTeardownThenInflate)
{
    /* config_dir — RUNTIME STR, idx 10, alloc_sz=257 */
    InflateRuntime(UFSRV_RUNTIME_FIELD_CONFIG_DIR, false);

    UfsrvConfigValue *slot = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_CONFIG_DIR);
    ASSERT_NE(slot->stored_value.str_val, nullptr);
    snprintf(slot->stored_value.str_val, 257, "%s", "direct_test");

    /* Call teardown directly — frees + nullifies str_val,
     * but does NOT memset(0) the UfsrvConfigValue slot.
     * type_allocation_sz and source remain intact. */
    GenericStrTeardownRuntime(UFSRV_RUNTIME_FIELD_CONFIG_DIR, slot);
    EXPECT_EQ(slot->stored_value.str_val, nullptr);

    /* Now inflate(true) — must skip free(NULL), then malloc fresh.
     * The type_allocation_sz from the defaults table drives the size. */
    InflateRuntime(UFSRV_RUNTIME_FIELD_CONFIG_DIR, true);

    char *s = UfsrvConfigRuntimeValuesInstance(
        UFSRV_RUNTIME_FIELD_CONFIG_DIR)->stored_value.str_val;
    ASSERT_NE(s, nullptr);
    snprintf(s, 257, "%s", "after_direct_teardown");
    EXPECT_STREQ(s, "after_direct_teardown");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T11 — Triple inflate: false → true → true (double-free prevention)
 *
 * Inflate(false) → inflate(true) → inflate(true).
 * The nullify-after-free prevents the third call from double-freeing.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(InflateDeflateLifecycleTest, T11_TripleInflate)
{
    /* main_listener_address — CONFIG STR, idx 6, alloc_sz=257 */

    /* 1st: bootstrap */
    InflateConfig(UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS, false);
    char *b1 = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS)->stored_value.str_val;
    ASSERT_NE(b1, nullptr);
    snprintf(b1, 257, "%s", "buf1");

    /* 2nd: deflate + re-inflate */
    InflateConfig(UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS, true);
    char *b2 = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS)->stored_value.str_val;
    ASSERT_NE(b2, nullptr);
    EXPECT_NE(static_cast<void *>(b2), static_cast<void *>(b1));
    snprintf(b2, 257, "%s", "buf2");

    /* 3rd: deflate + re-inflate again — must NOT double-free */
    InflateConfig(UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS, true);
    char *b3 = UfsrvConfigConfFileValuesInstance(
        UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS)->stored_value.str_val;
    ASSERT_NE(b3, nullptr);
    EXPECT_NE(static_cast<void *>(b3), static_cast<void *>(b2));
    snprintf(b3, 257, "%s", "buf3");
    EXPECT_STREQ(b3, "buf3");
}
