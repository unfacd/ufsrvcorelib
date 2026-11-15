/**
 * @file ufsrv_config.h
 * @brief Singleton lifecycle — store init, Instance(idx) access, teardown.
 *
*  IMPORTANT NOTICE this file is not included in normal build target. Typically, it get incorporated as part
 * of 'composition' at a host server, such as ufsrvswebsock.
 *
 * The consumer allocates two UfsrvConfigFieldEntry arrays (CONFIG + RUNTIME) and (optionally associated  defaults
 * lookup maps of type \p UfsrvConfigValue) and registers them via the Init functions.
 * The data structure model defines two types of config value stores:
 *  - ConfigFile store: holds values read from application's configuration files (lua based)
 *  - Runtime store: holds pre-defined typed values which the application uses during its runtime state management
 *
 * Each store defines three maps:
 *  - Main map of type \p  UfsrvConfigFieldEntry. This is a 'live' map, holding the memory allocation, plus mostly mutable values.
 *  - Values map of type \p UfsrvConfigValue. This is a 'live', dervied map from from the main map, facilitating direct access to
 *    variable, containing stored values. Its indexiation structure is identical with main.
 *  - Defaults map of type \p UfsrvConfigValue. This is standalone map, used as lookup map referring to non-mutable
 *    defaults that are used to initialise live stores or reset their state.
 *
 *  These maps go through the following lifeccyle
 *
 *   1. Consumer allocates maps:
 *        UfsrvConfigFieldEntry config_store[UFSRV_CONFIG_FIELD_COUNT];
 *        UfsrvConfigFieldEntry runtime_store[UFSRV_RUNTIME_FIELD_COUNT];
 *   2. UfsrvConfigConfFileStoreInit(config_store)           — arm Config store
 *   3. UfsrvConfigRuntimeStoreInit(runtime_store)            — arm Runtime store
 *   4. UfsrvConfigConfFileStorePopulateDefaults(false)       — bootstrap CONFIG defaults
 *   5. UfsrvConfigRuntimeStorePopulateDefaults(false)       — bootstrap RUNTIME defaults
 *   6. UfsrvConfigPopulateDefaults(false)                    — super‑composition (4+5)
 *   7. Config file pass, CLI pass                            — populate via setters
 *   8. UfsrvConfigValidate()                                 — verify all fields
 *   9. Spawn worker threads
 *  10. Runtime getter access                                 — branch-free, lock-free
 *  11. UfsrvConfigRuntimeTeardown()                          — at shutdown, after workers join
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_CORE_UFSRV_CONFIG_H
#define UFSRV_CORE_UFSRV_CONFIG_H

#include <stdbool.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_shim.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_field_descriptor.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_value_type.h>

/**
 * IMPORTANT NOTICE this file is not included in normal build target. Typically, it get incorporated as part
 * of 'composition' at a host server, such as ufsrvswebsock.
 */

/*!
 * Register the consumer-allocated Config store array.
 * Must be called exactly once at startup, before any worker threads are spawned.  Before this call, any Instance() access triggers abort().
 *
 * @param store  Consumer-allocated array of UfsrvConfigValue, sized to UFSRV_CONFIG_FIELD_COUNT.
 * @param store_defaults Lookup map containing defaults. Must match the indexation structure of \p store.
 */
PUBLIC_API void UfsrvConfigConfFileValuesInit(UfsrvConfigValue *store, const UfsrvConfigValue *store_defaults);

/**
 * Register the consumer-allocated MAIN config file store array.
 * Must be called exactly once at startup, before any worker threads are spawned.  Before this call, any Instance() access triggers abort().
 *
 * @param store Main config file store array, holding config field items.
 * @param store_defaults Lookup map containing defaults. Must match the indexation structure of \p store.
 */
PUBLIC_API void UfsrvConfigConfFileStoreInit(UfsrvConfigFieldEntry *store, const UfsrvConfigValue *store_defaults);

/*!
 * Register the consumer-allocated runtime config values store array.
 * Must be called exactly once at startup, before any worker threads are spawned.  Before this call, any Instance() access triggers abort().
 *
 * @param store  Consumer-allocated array of UfsrvConfigValue, sized to UFSRV_RUNTIME_FIELD_COUNT.
 * @param store_defaults Lookup map containing defaults. Must match the indexation structure of \p store.
 */
PUBLIC_API void UfsrvConfigRuntimeValuesInit(UfsrvConfigValue *store, const UfsrvConfigValue *store_defaults);

/*!
 * Register the consumer-allocated MAIN runtime store array.
 *
 * Must be called exactly once at startup, before any worker threads are
 * spawned.  Before this call, any Instance() access triggers abort().
 *
 * @param store  Consumer-allocated array of UfsrvConfigValue, sized to UFSRV_RUNTIME_FIELD_COUNT.
 * @param store_defaults Lookup map containing defaults. Must match the indexation structure of \p store.
 */
PUBLIC_API void UfsrvConfigRuntimeStoreInit(UfsrvConfigFieldEntry *store, const UfsrvConfigValue *store_defaults);

/*!
 * Return a pointer to the Config store slot at the given enum index.
 *
 * Routes through the internal function-pointer guard — if Init has not
 * been called, this triggers abort().  After Init, returns a direct
 * pointer into the consumer's store array.
 *
 * @param idx  Enum index (0 .. UFSRV_CONFIG_FIELD_COUNT-1).
 * @return     Pointer to ufsrv_config_store[idx].
 */
PUBLIC_API UfsrvConfigValue *UfsrvConfigConfFileValuesInstance(enum UfsrvConfigField idx);
PUBLIC_API UfsrvConfigFieldEntry *UfsrvConfigConfFileStoreInstance(enum UfsrvConfigField idx);
/*!
 * Return a pointer to the Runtime store slot at the given UfsrvRuntimeField index.
 *
 * Routes through the internal function-pointer guard — if Init has not
 * been called, this triggers abort().  After Init, returns a direct
 * pointer into the consumer's store array.
 *
 * @param idx  Enum index (0 .. UFSRV_RUNTIME_FIELD_COUNT-1).
 * @return     Pointer to ufsrv_runtime_store[idx].
 */
PUBLIC_API UfsrvConfigValue *UfsrvConfigRuntimeValuesInstance(enum UfsrvRuntimeField idx);
PUBLIC_API UfsrvConfigFieldEntry *UfsrvConfigRuntimeStoreInstance(enum UfsrvRuntimeField idx);

/**
 * Allocate memory to individual config items to enable value storage. \p UfsrvConfigValue.type_allocation_sz MUST be
 * set.
* This assumes the live store has undergone `init` state transition.
 *
 */
PUBLIC_API void UfsrvConfigInflateConfigStore(void);

/**
 * Allocate memory to individual config items to enable value storage. \p UfsrvConfigValue.type_allocation_sz MUST be
 * set.
* This assumes the live store has undergone `init` state transition.
 *
 */
PUBLIC_API void UfsrvConfigInflateRuntimeStore(void);

/*!
 * Teardown all Runtime fields in reverse index order.
 *
 * Dispatches through ufsrv_runtime_field_table[].lifecycle.on_teardown().
 * Called at shutdown, after all worker threads have joined.
 * Safe to call multiple times — nullified slots are skipped.
 */
PUBLIC_API void
UfsrvConfigRuntimeTeardown(void);

/**
 * Populate the (ConfigFile) live store with defaults. This assumes the live store has undergone `init` and 'inflated' state
 * transitions.
 *
 * @param defaults_table Immutable lookup map containing reference default values. Must be of the same indexation structure as
 * the live store.
 * @param is_forced_reset if set, the store's existing populated values will be wiped out. Memory allocation remain
 * unchanged relative to current \p UfsrvConfigValue.type_allocation_sz.
 */
PUBLIC_API void UfsrvConfigConfFileStorePopulateDefaults(const UfsrvConfigValue *defaults_table, bool is_forced_reset);

/**
 * Populate the (Runtime) live store with defaults. This assumes the live store has undergone `init` and 'inflated' state
 * transitions.
 *
 * @param defaults_table Immutable lookup map containing reference default values. Must be of the same indexation structure as
 * the live store.
 * @param is_forced_reset if set, the store's existing populated values will be wiped out. Memory allocation remain
 * unchanged relative to current \p UfsrvConfigValue.type_allocation_sz.
 */
PUBLIC_API void UfsrvConfigRuntimeStorePopulateDefaults(const UfsrvConfigValue *defaults_table, bool is_forced_reset);

/**
 * Convenient function, combining the population of both the ConfigFile and Runtimes live store with defaults.
 * This assumes the live store has undergone `init` and 'inflated' state transitions. Refer to store-specific functions
 * UfsrvConfigConfFileStorePopulateDefaults() and UfsrvConfigRuntimeStorePopulateDefaults() for more details.
 *
 * @param is_forced_reset if set, the store's existing populated values will be wiped out. Memory allocation remain
 * unchanged relative to current \p UfsrvConfigValue.type_allocation_sz.
 */
PUBLIC_API void UfsrvConfigPopulateDefaults(const UfsrvConfigValue *config_defaults_table, const UfsrvConfigValue *runtime_defaults_table, bool is_forced_reset);

/**
 * Convert a given UfsrvConfigSource to a human readable string.
 * @param src Bounded enum of type UfsrvConfigSource
 * @return source name literal
 */
PUBLIC_API const char * UfsrvConfigSourceNameLiteral(UfsrvConfigSource src);

/**
 * Convert a given UfsrvConfigValueType to a human readable string.
 * @param type Bounded enum of type UfsrvConfigValueType
 * @return type name literal
 */
PUBLIC_API const char * UfsrvConfigTypeLiteral(UfsrvConfigValueType type);

/**
 * Contract
 * --------
 * value_slots: BufferDescriptor value_slots[table_sz], zero-initialised
 *              (data == NULL, size == 0, size_max == 0).
 *
 * on_as_string(field_index, &entry->config, &value_slots[i]):
 *   - receives a zeroed BufferDescriptor *
 *   - allocates value_slots[i].data via the same grow/append rules as
 *     UfsrvConfigBufferReserve / BufferDescriptorAppendFormatted
 *   - on success returns the same pointer (== &value_slots[i])
 *   - on failure returns NULL (slot left empty / partially filled)
 *
 * After the JSON body is built, the caller (or this function) frees
 * each value_slots[i].data with free().
 *
 * @param table         field table
 * @param table_sz      entry count
 * @param fmt           PRETTY_PRINT_JSON (others treated as JSON here)
 * @param scope         DIGEST or FULL
 * @param value_slots   required: BufferDescriptor[table_sz], all zeroed
 *                      on entry; each on_as_string fills slots[i]
 * @param out           required: JSON body; zeroed or already inited
 *
 * @return 0 on success, -1 on failure.
 *         On success: out->data holds JSON (caller BufferDescriptorRelease(out)).
 *         Always: value_slots[i].data freed before return (success or fail).
 *
 *  @code
 *  enum { N = 46 };

   // Optional per-field value slots (fixed)
   static char value_mem[N][512];
   static BufferDescriptor value_slots[N];

   for (int i = 0; i < N; i++) {
     value_slots[i].data     = value_mem[i];
     value_slots[i].size     = 0;
     value_slots[i].size_max = sizeof value_mem[i];
   }

   // Growable JSON body — one BufferDescriptor
   BufferDescriptor out = {0};

   int rc = UfsrvConfigFieldTablePrettyPrint(
       ufsrv_config_field_table,
       N,
       PRETTY_PRINT_JSON,
       PRETTY_PRINT_SCOPE_DIGEST,
       value_slots,   // or NULL
       &out);

   if (rc == 0) {
     fputs(out.data, stdout);
     BufferDescriptorRelease(&out);
   }
  * @endcode
  *
  * Sample json output
  *
  * @code
   {
     "0": {
       "value": "0",
       "source": "unassigned"
     },
     "5": {
       "value": "19701",
       "source": "config_file"
     },
     "6": {
       "value": "127.0.0.1",
       "source": "defaults"
     }
   }
  * @endcode
 */
PUBLIC_API int UfsrvConfigFieldTablePrettyPrint(UfsrvConfigFieldEntry *table, size_t table_sz, enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *value_slots, BufferDescriptor *out);

#endif /* UFSRV_CORE_UFSRV_CONFIG_H */
