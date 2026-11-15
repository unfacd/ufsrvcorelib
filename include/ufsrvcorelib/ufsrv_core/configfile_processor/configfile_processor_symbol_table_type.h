/**
 * @file configfile_processor_symbol_table_type.h
 * @brief Symbol-table types for the configfile_processor module.
 *
 * Defines the symbol-table entry struct.  The canonical value type is
 * UfsrvConfigValue from the ufsrv_config module — the old ConfFileVarValue /
 * ConfFileVarType discriminated union has been retired.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_SYMBOL_TABLE_TYPE_H
#define UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_SYMBOL_TABLE_TYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_value_type.h>

/* Forward declaration */
typedef struct ConfFileSymbolTableEntry ConfFileSymbolTableEntry;

/*!
 * Validation callback — inspect the extracted value before it is accepted.
 *
 * @param entry   The symbol-table entry (provides scope, key, type, default).
 * @param value   The raw value extracted by the processor (type already checked).
 * @param errbuf  Caller-supplied buffer for an error message on failure.
 * @param errsz   Size of @p errbuf.
 * @return true if the value is acceptable; false to reject.
 */
typedef bool (*ConfigFileVarIsValidFn)(const ConfFileSymbolTableEntry *entry, const UfsrvConfigValue *value,
                                       char *                          errbuf, size_t                 errsz);
/*!
 * Post-processing callback — invoked after the value passes validation.
 *
 * Use this hook to log the resolved value, derive secondary state, register
 * with a subsystem, or populate a target C struct field.
 *
 * @param entry   The symbol-table entry.
 * @param value   The final, validated value.
 */
typedef void (*ConfFileVarOnValAvailableFn)(const ConfFileSymbolTableEntry *entry, const UfsrvConfigValue  *value);

/*!
 * One entry in the config symbol table.
 *
 * The table is a compile-time constant NULL-terminated array.  Each entry maps
 * a Lua scope + key to an expected value type and holds the resolved value
 * after extraction from the config file.
 *
 * Default values, validation callbacks, and storage targets have been moved
 * to the ufsrv_config generated field tables and cold catalogue.  Lifecycle
 * dispatch (on_value_available) is handled by the ufsrv_config module.
 */
struct ConfFileSymbolTableEntry
{
  const char *         scope;       ///< Scope table name (e.g. "ufsrv")
  const char *         key;         ///< Key within the scope (supports dot-path: "database.host")
  UfsrvConfigValueType type;        ///< Expected value type
  UfsrvConfigValue     value;       ///< Resolved value (populated by ResolveSymbols or bridge)
  int                  field_index; ///< UfsrvConfigField enum — int-typed to avoid circular include
  const char *         description; ///< Human-readable description (for JSON dump / --help-config)
  bool                 is_required; ///< is the attribute key manodotary in the config file
  struct {
    ConfigFileVarIsValidFn        on_validate;        ///< Domain-specific validation (may be NULL)
    ConfFileVarOnValAvailableFn   on_value_available; ///< Post-processing hook (may be NULL)
  } lifecycle;
};


#endif /* UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_SYMBOL_TABLE_TYPE_H */
