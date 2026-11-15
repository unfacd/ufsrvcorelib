/**
 * @file configfile_processor.h
 * @brief Public API for the namespaced Lua config-file processor.
 *
 * Isolated module under ufsrvcorelib.  Zero dependency on ufnetcorelib
 * or any server-specific type.  Dependency surface is Lua C API + uflib only.
 *
 * @note This module is NOT thread-safe — it is designed for single-threaded
 * startup usage, mirroring the existing config-loading pattern where
 * one thread loads the configuration before the server enters its
 * multi-threaded main loop.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_H
#define UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_H

#include <stdbool.h>
#include "ufsrvcorelib_defs.h"
#include "configfile_processor_type.h"
#include "configfile_processor_error_enum.h"
#include "configfile_processor_symbol_table_type.h"
#include <ufsrvcorelib/ufsrv_core/configfile_processor/configfile_validator_descriptor_type.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_value_type.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>

#include "ufsrv_config_keymap_entry_type.h"

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * @brief Create a configfile processor, loading and executing a Lua config file.
 *
 * The processor opens the Lua file, creates a lua_State, loads the standard
 * Lua libraries, and executes the file.  After this call, all Lua global
 * scope tables are available via the typed accessor functions.
 *
 * The returned handle owns the lua_State.  It must be released with ConfFileGetLastError(cp) != CONF_FILE_OK
 * ConfFileDestroy().
 *
 * On memory allocation failure, returns NULL. Otherwise user must check processing state with
 *
 * @param config_file  Absolute path to the Lua configuration file.
 * @return Heap-allocated opaque handle, or NULL on memory allocation error.
 *
 * @code{.c}
 * ConfFile cp = ConfFileInit("/etc/ufsrv/config.lua");
 * if (!cp) {
 *     fprintf(stderr, "FATAL: could not load config file\n");
 *     exit(EXIT_FAILURE);
 * }
 *
 * const char *name = ConfFileGetString(cp, "ufsrv", "server_run_mode");
 * int port = ConfFileGetInt(cp, "ufsrv", "main_listener_port");
 * if (ConfFileGetLastError(cp) != CONF_FILE_OK) {
 *     // handle missing key
 * }
 *
 * ConfFileDestroy(cp);
 * @endcode
 */
PUBLIC_API ConfFile
ConfFileInit(const char *config_file);

/**
 * @brief Destroy the processor, closing the lua_State and freeing all resources.
 *
 * Safe to call with NULL (no-op).  After this call, all pointers returned
 * by the accessor functions are invalid — the consumer must have already
 * copied any needed values.
 *
 * @param cp  Processor handle (may be NULL).
 */
PUBLIC_API void
ConfFileDestroy(ConfFile cp);

/* ── Error handling ─────────────────────────────────────────────────────── */

/**
 * @brief Return the error code from the most recent operation on this handle.
 *
 * Every accessor function sets the internal error code on failure.  A return
 * value of CONF_FILE_OK means the last operation succeeded.
 *
 * @param cp  Processor handle (returns CONF_FILE_ERR_ARGUMENT if NULL).
 * @return Error code.
 */
PUBLIC_API ConfFileError
ConfFileGetLastError(ConfFile cp);

/**
 * @brief Return a human-readable description of the last error.
 *
 * The returned pointer is valid until the next operation on this handle
 * (including Destroy).  The caller must NOT free it.
 *
 * @param cp  Processor handle.
 * @return Static string describing the error (never NULL for a valid handle).
 */
PUBLIC_API const char *
ConfFileGetLastErrorMessage(ConfFile cp);

/**
 * @brief Convert an error code to its canonical string (for logging).
 *
 * @param err  Error code.
 * @return String literal; do not free.
 */
PUBLIC_API const char *
ConfFileErrorToString(ConfFileError err);

/* ── Generic scalar accessors (scope + key) ─────────────────────────────── */

/**
 * @brief Retrieve a string value from a scope table.
 *
 * The key may use dot-path notation for one level of nesting
 * (e.g. "database.host" reads ufsrv.database.host).
 *
 * On error, returns NULL and sets the handle's last_error.  The returned
 * pointer references Lua-internal memory — it is valid until the next
 * Lua stack operation on this handle.  Copy immediately if the value
 * must outlive the next accessor call (see ConfFileGetStringCopy).
 *
 * @param cp     Processor handle.
 * @param scope  Scope table name (e.g. "ufsrv", "ufsrvwebsock").
 * @param key    Key within the scope table.
 * @return Lua-owned string pointer, or NULL on error (check GetLastError).
 */
PUBLIC_API const char *
ConfFileGetString(ConfFile cp, const char *scope, const char *key);

/**
 * @brief Retrieve a string with a default fallback if the key is missing.
 *
 * Unlike ConfFileGetString, a missing key does NOT set the error state —
 * the default is returned and last_error remains CONF_FILE_OK.
 * Type mismatches still set the error state.
 *
 * @param cp             Processor handle.
 * @param scope          Scope table name.
 * @param key            Key within the scope table.
 * @param default_value  Value returned when the key is nil or missing.
 * @return String value (Lua-owned), or @p default_value if key not found.
 */
PUBLIC_API const char *
ConfFileGetStringDefault(ConfFile cp, const char *scope, const char *key, const char *default_value);

/**
 * @brief Copy a string value to a caller-supplied buffer.
 *
 * Safer than ConfFileGetString() for long-lived values; avoids
 * Lua-interned-string lifetime issues.  The result is always NUL-terminated
 * if @p buf_size > 0.
 *
 * @param cp         Processor handle.
 * @param scope      Scope table name.
 * @param key        Key within the scope.
 * @param buf        Destination buffer (caller-owned).
 * @param buf_size   Size of @p buf.
 * @return Number of bytes written (excluding NUL), or -1 on error
 *         (check ConfFileGetLastError).
 */
PUBLIC_API int
ConfFileGetStringCopy(ConfFile cp, const char *scope, const char *key, char *buf, size_t buf_size);

/**
 * @brief Retrieve a signed integer value from a scope table.
 *
 * On error, returns 0 and sets the handle's last_error.  Since 0 is a
 * valid configuration value, callers MUST check ConfFileGetLastError()
 * after this call to distinguish a genuine 0 from an error.
 *
 * @param cp     Processor handle.
 * @param scope  Scope table name.
 * @param key    Key within the scope table.
 * @return Integer value, or 0 on error (check GetLastError).
 */
PUBLIC_API int
ConfFileGetInt(ConfFile cp, const char *scope, const char *key);

/**
 * @brief Retrieve an integer with a default fallback if the key is missing.
 *
 * Missing keys do NOT set the error state.  Type mismatches do.
 *
 * @param cp             Processor handle.
 * @param scope          Scope table name.
 * @param key            Key within the scope table.
 * @param default_value  Value returned when the key is nil or missing.
 * @return Integer value, or @p default_value if key not found.
 */
PUBLIC_API int
ConfFileGetIntDefault(ConfFile cp, const char *scope, const char *key, int default_value);

/**
 * @brief Retrieve a floating-point (lua_Number) value from a scope table.
 *
 * On error, returns 0.0 and sets the handle's last_error.
 *
 * @param cp     Processor handle.
 * @param scope  Scope table name.
 * @param key    Key within the scope table.
 * @return Number value, or 0.0 on error (check GetLastError).
 */
PUBLIC_API double
ConfFileGetNumber(ConfFile cp, const char *scope, const char *key);

/**
 * @brief Retrieve a boolean value from a scope table.
 *
 * Lua booleans only; numbers are NOT coerced.  On error, returns false
 * and sets the handle's last_error.
 *
 * @param cp     Processor handle.
 * @param scope  Scope table name.
 * @param key    Key within the scope table.
 * @return Boolean value, or false on error (check GetLastError).
 */
PUBLIC_API bool
ConfFileGetBool(ConfFile cp, const char *scope, const char *key);

/* ── Nested table access ────────────────────────────────────────────────── */

/**
 * @brief Check whether a key exists and holds a Lua table.
 *
 * Use before walking nested structures with the dot-path convention
 * or before iterating keys within a sub-table.  Does NOT modify the
 * handle's error state.
 *
 * @param cp     Processor handle.
 * @param scope  Scope table name.
 * @param key    Key within the scope.
 * @return true if the key exists and is a table.
 */
PUBLIC_API bool
ConfFileIsTable(ConfFile cp, const char *scope, const char *key);

/* ── Array / sequence access ────────────────────────────────────────────── */

/**
 * @brief Return the length of a Lua array (1-indexed sequence) at scope.key.
 *
 * Uses the Lua # length operator.  Returns -1 on error (check GetLastError).
 *
 * @param cp     Processor handle.
 * @param scope  Scope table name.
 * @param key    Key of the array within the scope.
 * @return Array length (>= 0), or -1 on error.
 */
PUBLIC_API int
ConfFileGetArrayLength(ConfFile cp, const char *scope, const char *key);

/**
 * @brief Retrieve the i-th element (1-indexed Lua convention) as a string.
 *
 * @param cp     Processor handle.
 * @param scope  Scope table name.
 * @param key    Key of the array within the scope.
 * @param index  1-based index (Lua convention).
 * @return String value (Lua-owned), or NULL on error (check GetLastError
 *         for CONF_FILE_ERR_INDEX_OUT_OF_RANGE or CONF_FILE_ERR_TYPE_MISMATCH).
 */
PUBLIC_API const char *
ConfFileGetArrayElement(ConfFile cp, const char *scope, const char *key, int index);

/* ── Scope / key discovery ──────────────────────────────────────────────── */

/**
 * @brief Check whether a scope table exists and is a table.
 *
 * Does NOT modify the handle's error state.
 *
 * @param cp     Processor handle.
 * @param scope  Scope table name.
 * @return true if the scope exists and is a table.
 */
PUBLIC_API bool
ConfFileHasScope(ConfFile cp, const char *scope);

/**
 * @brief Iterate over all top-level scope tables found in the config.
 *
 * Returns scope names in no guaranteed order.  Call with *prev_index = -1
 * to start; each successive call passes the index returned by the previous
 * call (the int pointed to by prev_index is updated in place).
 * Returns NULL when iteration is exhausted.
 *
 * Does NOT modify the handle's error state.
 *
 * @param cp           Processor handle.
 * @param prev_index   Pointer to -1 to start, updated with each call.
 * @return Scope name (Lua-owned, copy if needed), or NULL when done.
 *
 * @code{.c}
 * int idx = -1;
 * const char *scope;
 * while ((scope = ConfFileNextScope(cp, &idx))) {
 *     printf("Found scope: %s\n", scope);
 * }
 * @endcode
 */
PUBLIC_API const char *
ConfFileNextScope(ConfFile cp, int *prev_index);

/**
 * @brief Iterate over all keys within a named scope.
 *
 * Same calling convention as ConfFileNextScope.  Does NOT modify the
 * handle's error state.
 *
 * @param cp           Processor handle.
 * @param scope        Scope table to enumerate.
 * @param prev_index   Pointer to -1 to start, updated with each call.
 * @return Key name (Lua-owned), or NULL when done.
 */
PUBLIC_API const char *
ConfFileNextKey(ConfFile cp, const char *scope, int *prev_index);

/* ── Symbol-table resolution ────────────────────────────────────────────── */

/**
 * @brief Transfer values stored in config file to its associated value slot in the provided symbols map.
 *
 * For each entry, the processor extracts the raw value from scope.key,
 * type-checks it, calls is_valid() (if non-NULL), applies the default if
 * the key is missing or validation fails, copies the resolved value into
 * value storage.
 * The function also establishes an important link between the lua config file domain and ufsrv config domain by associating
 * each slot with the corresponding enum index as defined by teh ufsrv config.
 *
 * Errors are reported per-entry via the error callback rather than stopping
 * at the first failure, so the operator sees all problems at once.
 *
 * @param cp        Processor handle with config already loaded.
 * @param config_file_symbols_map   NULL-terminated array of symbol-table entries. Can be core symbols or adjunct symbols.
 * @param associative_lookup_map   Associative lookup map of {Lua config-key → field_index} to establish config index number bsearch-based
 *                   associated with a given Lua config-key (may be NULL if no key-map is available).
 * @param associative_lookup_map_sz Number of entries in key_map (UFSRV_CONFIG_KEY_MAP_COUNT, or 0 if key_map is NULL).
 * @param on_error   Optional callback invoked for each entry that fails validation or type-checking (may be NULL).
 * @return Number of entries that failed validation (0 = all ok).
 *
 * @code{.c}
 * int failures = ConfFileResolveSymbols(cp, g_ufsrv_config_symbols,
 *     (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, sOnSymbolError);
 * failures    += ConfFileResolveSymbols(cp, g_app_config_symbols,
 *     (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, sOnSymbolError);
 * if (failures > 0) {
 *     fprintf(stderr, "%d config variable(s) failed validation\n", failures);
 *     ConfFileDestroy(cp);
 *     exit(EXIT_FAILURE);
 * }
 * @endcode
 */
PUBLIC_API int ConfFileTransferToConfigValues(ConfFile cp, const ConfFileSymbolTableEntry config_file_symbols_map[], const void *associative_lookup_map, size_t associative_lookup_map_sz, void (*on_error)(const ConfFileSymbolTableEntry *entry, const char *message));

/* ── JSON serialization ─────────────────────────────────────────────────── */

/**
 * @brief Serialize a resolved symbol table to a JSON file.
 *
 * Walks the symbol table, reading each resolved value from entry->storage,
 * and writes a pretty-printed JSON object to @p output_path.  The output
 * is grouped by scope with 2-space indentation.
 *
 * Dot-path keys (e.g. "database.host") are rendered as nested JSON objects.
 * Keys whose name contains "password", "secret", or "token" are redacted
 * to "(redacted)".
 *
 * @param symbols      NULL-terminated symbol table (already resolved).
 * @param output_path  Absolute or relative path for the output JSON file.
 * @return 0 on success; -1 on file-open or write error (errno is set).
 */
PUBLIC_API int ConfFileDumpJsonx(const ConfFileSymbolTableEntry symbols[], const char *output_path);

/*
 * DumpJson — merge two NULL-terminated ConfFileSymbolTableEntry maps and
 * emit a JSON object grouped by .scope.  Attribute names are resolved via
 * field_index → config_literal (associative map); falls back to .key.
 *
 * json_dump_file == NULL  →  stderr
 * Returns 0 on success, -1 on error.
 */
PUBLIC_API int ConfFileDumpJson(const ConfFileSymbolTableEntry *g_ufsrv_config_symbols, const ConfFileSymbolTableEntry *g_ufsrv_config_app_symbols, const UfsrvConfigKeyMapEntry *map,  size_t map_sz, const char *json_dump_file);

// CLI validation mode

/**
 * @brief Load a config file, validate against core + optional adjunct schemas, resolve against core + optional adjunct symbol tables, and report
 *        every finding to a callback.
 *
 * @param config_file_validator_descriptor  Convenient grouping of function arguments
 *
 */
PUBLIC_API ConfFile  ConfFileValidate(const ConfigFileValidatorDescriptor *config_file_validator_descriptor);

/* ── Adjunct configuration extension ────────────────────────────────────── */

/**
 * @brief Register an adjunct config struct before symbol-table resolution.
 *
 * The application allocates and zero-fills its adjunct struct, then calls
 * this function so the adjunct symbol table's on_val_available callbacks
 * can populate it.  Must be called before ConfFileTransferToConfigValues() for
 * the adjunct table.
 *
 * @param adjunct_ptr  Pointer to the application's adjunct config struct.
 * @param adjunct_sz   sizeof(the adjunct struct) for sanity checking.
 *
 * @code{.c}
 * UfsrvWebsockAdjunctConfig websock_cfg = {0};
 * ConfFileSetAdjunct(&websock_cfg, sizeof(websock_cfg));
 *
 * // Resolve adjunct symbols — on_val_available populates websock_cfg
 * ConfFileResolveSymbols(cp, g_app_config_symbols,
 *     (void *)ufsrv_config_key_map, UFSRV_CONFIG_KEY_MAP_COUNT, NULL);
 *
 * // Now accessible via:
 * UfsrvWebsockAdjunctConfig *ac = ConfFileAdjunct();
 * assert(ac->max_frame_size == 65536);
 * @endcode
 */
PUBLIC_API void ConfFileSetAdjunct(void *adjunct_ptr, size_t adjunct_sz);

/**
 * @brief Return the adjunct config pointer for the calling server class.
 *
 * The caller casts the returned pointer to its server-specific adjunct struct.
 * Returns NULL if ConfFileSetAdjunct() was never called.
 *
 * @return Opaque pointer to adjunct config struct, or NULL.
 */
PUBLIC_API void *ConfFileAdjunct(void);

/**
 *
 * @return default file name for core configuration file schema.
 */
PUBLIC_API const char * ConfigFileCoreSchema();

/**
 *
 * @return default file name for adjunct configuration file schema.
 */
PUBLIC_API const char * ConfigFileAdjunctSchema();

/**
 *
 * @return default file name for core configuration file schema validator.
 */
PUBLIC_API const char * ConfigFileSchemaValidator();

#endif /* UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_H */
