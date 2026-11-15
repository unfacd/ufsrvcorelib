/**
 * @file ufsrv_config_distinct_array_test.cpp
 * @brief Adversarial tests for the DistinctArray-backed custom setters and
 *        AsString marshallers (ssl_command_console allow/deny lists) and the
 *        CollectionDescriptor-backed fileloader black-list registry.
 *
 * These exercise the hand-authored definitions in
 *   - src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_accessor_defs.c
 *   - src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_as_string.c
 * (the `is_keep_custom_setter` / `_DistinctArray` accessors), which are NOT
 * compiled into the library and must be tested in isolation.
 *
 * ## Coverage
 *
 *   - T1  Round-trip: set → DistinctArray membership → AsString JSON
 *   - T2  Duplicate suppression (DistinctArray dedup)
 *   - T3  Empty / NULL marshalled input (safe no-op)
 *   - T4  Oversized entry (exceeds block_storage_unit_sz) dropped, no crash
 *   - T5  collection_sz / type_allocation_sz mismatch → bounded walk (no OOB)
 *   - T6  Cross-field isolation (no slot bleed between the four fields)
 *   - T7  Source provenance written to the slot
 *   - T8  Setter on an un-inflated slot (blob_val == NULL) is a safe no-op
 *   - T9  AsString with NULL value_ptr / NULL buffer / un-inflated slot
 *   - T10 AsString of an empty array renders "[]"
 *   - T11 Fileloader black-list CollectionDescriptor round-trip
 *   - T12 The three allow-lists round-trip independently
 *   - T13 Re-setting a populated slot replaces contents (no leak / no append)
 *   - T14 Inflated-but-unset field renders "[]" (zeroed, not garbage)
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
#include <cstdint>
#include <string>
#include <vector>
#include <initializer_list>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
#include <uflib/adt/adt_distinct_array.h>
#include <uflib/collection_descriptor_type.h>
#include <uflib/utils_collection_str.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>

/* Declared in ufsrv_config_generic_callbacks_priv.h (co-located private header);
 * declared here rather than including that header, to avoid pulling its C-only
 * transitive includes (uflib/main_types.h) into this C++ TU. */
void GenericStructInflator(int field_index, const UfsrvConfigValue *value_ptr, bool is_deflated_first);
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* 64-char hex fingerprint filled with a single repeated char. */
static std::string Fp(char c) { return std::string(64, c); }

/* Render the expected AsString JSON array for a list of items. */
static std::string JsonArr(std::initializer_list<std::string> items) {
    std::string s = "[";
    bool first = true;
    for (const auto &it : items) {
        if (!first) s += ",";
        s += "\"" + it + "\"";
        first = false;
    }
    s += "]";
    return s;
}

/* Build the ARRAY marshalling blob (consecutive NUL-terminated strings) that
 * configfile_processor.c produces for UFSRV_CONFIG_VALUE_ARRAY.  Returns the
 * total byte size via @p total_out and the entry count via @p count_out. */
static std::vector<char> BuildArrayValue(std::initializer_list<std::string> entries,
                                         size_t *total_out, size_t *count_out) {
    size_t total = 0;
    for (const auto &e : entries) total += e.size() + 1;
    std::vector<char> buf(total ? total : 1, '\0');
    char *p = buf.data();
    for (const auto &e : entries) {
        memcpy(p, e.c_str(), e.size() + 1);
        p += e.size() + 1;
    }
    if (total_out) *total_out = total;
    if (count_out) *count_out = entries.size();
    return buf;
}

/* ── Fixture ─────────────────────────────────────────────────────────────── */

class DistinctArrayAccessorTest : public ::testing::Test {
protected:
    UfsrvConfigFieldEntry cfg_store[UFSRV_CONFIG_FIELD_COUNT];

    void SetUp() override {
        memset(cfg_store, 0, sizeof(cfg_store));
        UfsrvConfigConfFileStoreInit(cfg_store, nullptr);
    }

    /* Allocate a zero-initialised DistinctArray struct in a field's value slot. */
    DistinctArray *AllocDa(enum UfsrvConfigField f) {
        UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(f);
        slot->stored_value.blob_val = calloc(1, sizeof(DistinctArray));
        return static_cast<DistinctArray *>(slot->stored_value.blob_val);
    }

    /* Free a field's DistinctArray struct + inner slab/hash table. */
    void FreeDa(enum UfsrvConfigField f) {
        UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(f);
        if (slot->stored_value.blob_val) {
            DistinctArrayDestruct(static_cast<DistinctArray *>(slot->stored_value.blob_val));
            free(slot->stored_value.blob_val);
            slot->stored_value.blob_val = nullptr;
        }
    }

    /* Number of populated entries in a field's DistinctArray. */
    static size_t DaSize(enum UfsrvConfigField f) {
        DistinctArray *da = static_cast<DistinctArray *>(
            UfsrvConfigConfFileValuesInstance(f)->stored_value.blob_val);
        return (da && da->stored_value_idx) ? da->stored_value_idx->size : 0;
    }

    /* Render a field through its generated AsString accessor. */
    static std::string Render(enum UfsrvConfigField f,
                              BufferDescriptor *(*fn)(int, UfsrvConfigValue *, BufferDescriptor *)) {
        BufferDescriptor bd;
        BufferDescriptorInit(&bd, 1024);
        BufferDescriptor *out = fn(static_cast<int>(f),
                                   UfsrvConfigConfFileValuesInstance(f), &bd);
        std::string s = (out && out->data) ? out->data : "";
        BufferDescriptorRelease(&bd);
        return s;
    }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * T1 — Round-trip: set → membership → AsString JSON
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterRoundTripsViaAsString)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;
    AllocDa(f);

    std::string fa = Fp('a'), fb = Fp('b'), fc = Fp('c'), fz = Fp('z');

    size_t total = 0, count = 0;
    auto blob = BuildArrayValue({fa, fb, fc}, &total, &count);

    UfsrvConfigItem item = {};
    item.type                              = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val       = blob.data();
    item.value.collection_sz               = count;
    item.value.type_allocation_sz          = total;

    UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

    EXPECT_EQ(DaSize(f), 3u);

    DistinctArray *da = UfsrvConfigSslCommandConsoleAllowListGodAsPtr();
    EXPECT_TRUE(DistinctArrayIsItemStored(da,
        reinterpret_cast<uint8_t *>(const_cast<char *>(fa.c_str()))));
    EXPECT_TRUE(DistinctArrayIsItemStored(da,
        reinterpret_cast<uint8_t *>(const_cast<char *>(fc.c_str()))));
    /* Probe must be a full 64-char entry: the hash table hashes fKeySize bytes
     * (65), so a shorter literal would read past its end. */
    EXPECT_FALSE(DistinctArrayIsItemStored(da,
        reinterpret_cast<uint8_t *>(const_cast<char *>(fz.c_str()))));

    EXPECT_EQ(Render(f, UfsrvConfigSslCommandConsoleAllowListGodAsString),
              JsonArr({fa, fb, fc}));

    FreeDa(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T2 — Duplicate suppression: DistinctArray stores each entry once.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterDedupesDuplicates)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_DENY_LIST;
    AllocDa(f);

    size_t total = 0, count = 0;
    auto blob = BuildArrayValue({Fp('a'), Fp('b'), Fp('a'), Fp('a'), Fp('b')},
                                &total, &count);
    EXPECT_EQ(count, 5u); /* marshalled count includes duplicates */

    UfsrvConfigItem item = {};
    item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val  = blob.data();
    item.value.collection_sz          = count;
    item.value.type_allocation_sz     = total;

    UfsrvConfigSetSslCommandConsoleDenyList(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

    EXPECT_EQ(DaSize(f), 2u); /* dedup: only 'a' and 'b' remain */
    EXPECT_EQ(Render(f, UfsrvConfigSslCommandConsoleDenyListAsString),
              JsonArr({Fp('a'), Fp('b')}));

    FreeDa(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T3 — Empty / NULL marshalled input: safe no-op, no crash.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterHandlesEmptyAndNullInputs)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;
    AllocDa(f);

    /* NULL blob, zero count */
    {
        UfsrvConfigItem item = {};
        item.value.stored_value.blob_val = nullptr;
        item.value.collection_sz         = 0;
        item.value.type_allocation_sz    = 0;
        UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        EXPECT_EQ(DaSize(f), 0u);
    }

    /* Empty blob (""), zero count */
    {
        char empty = '\0';
        UfsrvConfigItem item = {};
        item.value.stored_value.blob_val = &empty;
        item.value.collection_sz         = 0;
        item.value.type_allocation_sz    = 0;
        UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);
        EXPECT_EQ(DaSize(f), 0u);
    }

    EXPECT_EQ(Render(f, UfsrvConfigSslCommandConsoleAllowListGodAsString), "[]");
    FreeDa(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T4 — Oversized entry (> 64 chars) exceeds block_storage_unit_sz and is
 *      silently dropped by DistinctArrayPut; the setter must not crash.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterDropsOversizedEntry)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_BROWSER;
    AllocDa(f);

    std::string oversized(100, 'x'); /* 100 chars > 64-byte hex slot */

    size_t total = 0, count = 0;
    auto blob = BuildArrayValue({Fp('a'), oversized, Fp('b')}, &total, &count);

    UfsrvConfigItem item = {};
    item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val  = blob.data();
    item.value.collection_sz          = count;
    item.value.type_allocation_sz     = total;

    UfsrvConfigSetSslCommandConsoleAllowListBrowser(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

    /* The 100-char entry is rejected by DistinctArrayPut (HashError) and
     * dropped; the two 64-char entries remain. */
    EXPECT_EQ(DaSize(f), 2u);
    EXPECT_EQ(Render(f, UfsrvConfigSslCommandConsoleAllowListBrowserAsString),
              JsonArr({Fp('a'), Fp('b')}));

    FreeDa(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T5 — collection_sz > actual NUL-terminated entries in the blob: the setter
 *      bounds its walk by type_allocation_sz and must not read past the blob.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterBoundsWalkToTypeAllocationSz)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;
    AllocDa(f);

    /* Blob holds 2 entries but collection_sz lies and claims 5. */
    std::string a = Fp('a'), b = Fp('b');
    size_t total = a.size() + 1 + b.size() + 1;
    std::vector<char> blob(total, '\0');
    memcpy(blob.data(), a.c_str(), a.size() + 1);
    memcpy(blob.data() + a.size() + 1, b.c_str(), b.size() + 1);

    UfsrvConfigItem item = {};
    item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val  = blob.data();
    item.value.collection_sz          = 5;      /* maliciously larger than reality */
    item.value.type_allocation_sz     = total;  /* honest byte count */

    UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

    /* Only the two real entries are stored — the walk stopped at the buffer
     * boundary rather than reading OOB. */
    EXPECT_EQ(DaSize(f), 2u);
    EXPECT_EQ(Render(f, UfsrvConfigSslCommandConsoleAllowListGodAsString),
              JsonArr({a, b}));

    FreeDa(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T6 — Cross-field isolation: writing one field must not bleed into the
 *      adjacent DistinctArray fields.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterFieldsAreIsolated)
{
    enum UfsrvConfigField god      = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;
    enum UfsrvConfigField browser  = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_BROWSER;
    enum UfsrvConfigField deny     = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_DENY_LIST;

    AllocDa(god); AllocDa(browser); AllocDa(deny);

    size_t total = 0, count = 0;
    auto blob = BuildArrayValue({Fp('a')}, &total, &count);
    UfsrvConfigItem item = {};
    item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val  = blob.data();
    item.value.collection_sz          = count;
    item.value.type_allocation_sz     = total;

    UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

    EXPECT_EQ(DaSize(god), 1u);
    EXPECT_EQ(DaSize(browser), 0u);
    EXPECT_EQ(DaSize(deny), 0u);

    FreeDa(god); FreeDa(browser); FreeDa(deny);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T7 — Source provenance: the setter writes config_source into the slot.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterSetsSource)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;
    AllocDa(f);

    size_t total = 0, count = 0;
    auto blob = BuildArrayValue({Fp('a')}, &total, &count);
    UfsrvConfigItem item = {};
    item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val  = blob.data();
    item.value.collection_sz          = count;
    item.value.type_allocation_sz     = total;

    UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_CLI);

    EXPECT_EQ(static_cast<int>(UfsrvConfigConfFileValuesInstance(f)->source),
              static_cast<int>(UFSRV_CONFIG_SOURCE_CLI));

    FreeDa(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T8 — Setter on an un-inflated slot (blob_val == NULL) is a safe no-op.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterSafeOnUninflatedField)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;
    /* deliberately no AllocDa(f) — slot->stored_value.blob_val stays NULL */

    size_t total = 0, count = 0;
    auto blob = BuildArrayValue({Fp('a')}, &total, &count);
    UfsrvConfigItem item = {};
    item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val  = blob.data();
    item.value.collection_sz          = count;
    item.value.type_allocation_sz     = total;

    /* Must not dereference the NULL DistinctArray*. */
    UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

    EXPECT_EQ(UfsrvConfigConfFileValuesInstance(f)->stored_value.blob_val, nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T9 — AsString defensive: NULL value_ptr / NULL buffer / un-inflated slot.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, AsStringNullAndUninflated)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;

    /* Un-inflated slot (blob_val NULL) → returns NULL after Release. */
    {
        BufferDescriptor bd;
        BufferDescriptorInit(&bd, 128);
        BufferDescriptor *out = UfsrvConfigSslCommandConsoleAllowListGodAsString(
            static_cast<int>(f), UfsrvConfigConfFileValuesInstance(f), &bd);
        EXPECT_EQ(out, nullptr);
        BufferDescriptorRelease(&bd);
    }

    /* NULL value_ptr → NULL. */
    {
        BufferDescriptor bd;
        BufferDescriptorInit(&bd, 128);
        EXPECT_EQ(UfsrvConfigSslCommandConsoleAllowListGodAsString(
                      static_cast<int>(f), nullptr, &bd), nullptr);
        BufferDescriptorRelease(&bd);
    }

    /* NULL buffer_provided → NULL (no crash). */
    {
        AllocDa(f);
        EXPECT_EQ(UfsrvConfigSslCommandConsoleAllowListGodAsString(
                      static_cast<int>(f), UfsrvConfigConfFileValuesInstance(f), nullptr),
                  nullptr);
        FreeDa(f);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T10 — AsString of an empty (inflated, zero entries) array renders "[]".
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, AsStringEmptyArray)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;

    /* Inflate + initialise via a NULL set (empty) then render. */
    AllocDa(f);
    UfsrvConfigItem item = {};
    item.value.stored_value.blob_val = nullptr;
    item.value.collection_sz         = 0;
    UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_DEFAULTS);

    EXPECT_EQ(Render(f, UfsrvConfigSslCommandConsoleAllowListGodAsString), "[]");
    FreeDa(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T11 — Fileloader black-list round-trip through the CollectionDescriptor
 *       custom setter.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, FileloaderBlacklistRoundTrip)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_FILELOADER_BLACK_LISTED_FILES_REGISTRY;
    UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(f);
    slot->stored_value.blob_val = calloc(1, sizeof(CollectionDescriptor));

    size_t total = 0, count = 0;
    auto blob = BuildArrayValue({"/etc/passwd", "/var/lib/secret.dat"}, &total, &count);

    UfsrvConfigItem item = {};
    item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val  = blob.data();
    item.value.collection_sz          = count;
    item.value.type_allocation_sz     = total;

    UfsrvConfigSetFileloaderBlackListedFilesRegistry(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

    CollectionDescriptor *cd = UfsrvConfigFileloaderBlackListedFilesRegistryAsPtr();
    ASSERT_NE(cd, nullptr);
    EXPECT_EQ(cd->collection_sz, 2u);
    EXPECT_STREQ(CollectionDescriptorStringAt(cd, 0), "/etc/passwd");
    EXPECT_STREQ(CollectionDescriptorStringAt(cd, 1), "/var/lib/secret.dat");
    EXPECT_EQ(Render(f, UfsrvConfigFileloaderBlackListedFilesRegistryAsString),
              JsonArr({"/etc/passwd", "/var/lib/secret.dat"}));

    free(slot->stored_value.blob_val);
    slot->stored_value.blob_val = nullptr;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T12 — The three allow/deny lists round-trip independently.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, AllThreeAllowListsRoundTripIndependently)
{
    struct Cfg { enum UfsrvConfigField f;
                 void (*setter)(UfsrvConfigItem *, UfsrvConfigSource);
                 BufferDescriptor *(*asstr)(int, UfsrvConfigValue *, BufferDescriptor *); };

    const Cfg fields[] = {
        { UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD,
          UfsrvConfigSetSslCommandConsoleAllowListGod,
          UfsrvConfigSslCommandConsoleAllowListGodAsString },
        { UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_BROWSER,
          UfsrvConfigSetSslCommandConsoleAllowListBrowser,
          UfsrvConfigSslCommandConsoleAllowListBrowserAsString },
        { UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_DENY_LIST,
          UfsrvConfigSetSslCommandConsoleDenyList,
          UfsrvConfigSslCommandConsoleDenyListAsString },
    };

    for (const auto &cfg : fields) {
        AllocDa(cfg.f);
        size_t total = 0, count = 0;
        auto blob = BuildArrayValue({Fp('0'), Fp('f')}, &total, &count);
        UfsrvConfigItem item = {};
        item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
        item.value.stored_value.blob_val  = blob.data();
        item.value.collection_sz          = count;
        item.value.type_allocation_sz     = total;

        cfg.setter(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

        EXPECT_EQ(DaSize(cfg.f), 2u);
        EXPECT_EQ(Render(cfg.f, cfg.asstr), JsonArr({Fp('0'), Fp('f')}));
        FreeDa(cfg.f);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T13 — Re-setting a populated slot replaces its contents (does not append)
 *       and does not leak the prior slab / index / hash table.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, SetterReplacesOnReSet)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;
    AllocDa(f);

    std::string a = Fp('a'), d = Fp('d');

    size_t total = 0, count = 0;
    auto blob1 = BuildArrayValue({a, Fp('b'), Fp('c')}, &total, &count);
    UfsrvConfigItem item = {};
    item.type                         = UFSRV_CONFIG_VALUE_ARRAY;
    item.value.stored_value.blob_val  = blob1.data();
    item.value.collection_sz          = count;
    item.value.type_allocation_sz     = total;

    UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);
    EXPECT_EQ(DaSize(f), 3u);

    /* Re-set the same slot with a single entry: must replace, not append. */
    auto blob2 = BuildArrayValue({d}, &total, &count);
    item.value.stored_value.blob_val  = blob2.data();
    item.value.collection_sz          = count;
    item.value.type_allocation_sz     = total;

    UfsrvConfigSetSslCommandConsoleAllowListGod(&item, UFSRV_CONFIG_SOURCE_CONFIG_FILE);

    EXPECT_EQ(DaSize(f), 1u);
    EXPECT_EQ(Render(f, UfsrvConfigSslCommandConsoleAllowListGodAsString),
              JsonArr({d}));
    EXPECT_FALSE(DistinctArrayIsItemStored(
        UfsrvConfigSslCommandConsoleAllowListGodAsPtr(),
        reinterpret_cast<uint8_t *>(const_cast<char *>(a.c_str()))));

    FreeDa(f);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * T14 — Regression: the STRUCT inflator must zero the slot it allocates.
 *       An inflated-but-never-set DistinctArray field (e.g. allow-list key
 *       absent from the Lua config) must read as a well-defined empty array —
 *       not crash AsString/AsPtr on uninitialised DistinctArray fields.
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST_F(DistinctArrayAccessorTest, InflatedButUnsetFieldRendersEmptyArray)
{
    enum UfsrvConfigField f = UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD;

    /* Inflate through the real production inflator (calloc) and do NOT set. */
    UfsrvConfigValue default_value = {};
    default_value.type_allocation_sz = sizeof(DistinctArray);
    GenericStructInflator(static_cast<int>(f), &default_value, false);

    DistinctArray *da = UfsrvConfigSslCommandConsoleAllowListGodAsPtr();
    ASSERT_NE(da, nullptr);
    EXPECT_EQ(da->stored_value_idx, nullptr);   /* zeroed, not garbage */

    EXPECT_EQ(Render(f, UfsrvConfigSslCommandConsoleAllowListGodAsString), "[]");

    std::string probe = Fp('a');
    EXPECT_FALSE(DistinctArrayIsItemStored(
        da, reinterpret_cast<uint8_t *>(const_cast<char *>(probe.c_str()))));

    FreeDa(f);
}
