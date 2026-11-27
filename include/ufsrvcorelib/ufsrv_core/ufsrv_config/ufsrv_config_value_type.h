/**
 * @file ufsrv_config_value_type.h
 * @brief Canonical value type — UfsrvConfigValue, UfsrvConfigValueType enum,
 *        and UfsrvConfigItem.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * UfsrvConfigValue is the canonical value representation used everywhere —
 * cold catalogue defaults, hot store slots, and values passed to lifecycle
 * callbacks.  The @p stored_value union carries the value itself; @p
 * type_allocation_sz carries the byte count for BLOB memcpy or the buffer
 * size for STR fields.
 */

#ifndef UFSRV_CORE_UFSRV_CONFIG_VALUE_TYPE_H
#define UFSRV_CORE_UFSRV_CONFIG_VALUE_TYPE_H

#include <stddef.h>   /* size_t */
#include <stdint.h>   /* int64_t */

// Value type — storage class tag

/*! Storage class of a config value — determines which generic setter to wire. */
typedef enum UfsrvConfigValueType {
  UFSRV_CONFIG_VALUE_TYPE_UNSPECIFIED = 0,  ///< Unassigned value, or invalid type
  UFSRV_CONFIG_VALUE_INT              = 1,  ///< int64_t — direct scalar store/load
  UFSRV_CONFIG_VALUE_STR              = 2,  ///< fixed char[] buffer — copy-by-value into str_buf
  UFSRV_CONFIG_VALUE_PTR              = 3,  ///< void * — copy-by-reference (teardown optional)
  UFSRV_CONFIG_VALUE_STRUCT           = 4,  ///< memcpy() both directions, caller owns the copy
  UFSRV_CONFIG_VALUE_CONST_PTR        = 5,  ///< const pointer — getter only, no public setter eg preallocated str as 'char *'
  UFSRV_CONFIG_VALUE_ENUM             = 6,   ///< int store, validated against named constants
  UFSRV_CONFIG_VALUE_ARRAY            = 7,   ///< Specialised blob type for semantic clarity
  UFSRV_CONFIG_VALUE_BOOL             = 8,    ///< standard c bool type
  UFSRV_CONFIG_VALUE_DOUBLE           = 9,   ///< standard C double type
  UFSRV_CONFIG_VALUE_COLLECTION       = 10   ///< Lua array of named scalar rows, transferred to a custom setter
} UfsrvConfigValueType;

// Config sourcing provenance

/*! Provenance of a config value — passed to on_value_available as the source
 *  parameter.  Injected externally so the callback is agnostic to caller. */
typedef enum UfsrvConfigSource{
  UFSRV_CONFIG_SOURCE_UNASSIGNED   = 0,  ///< Source unspecified
  UFSRV_CONFIG_SOURCE_DEFAULTS     = 1,  ///< compiled defaults — bootstrap pass
  UFSRV_CONFIG_SOURCE_CONFIG_FILE  = 2,  ///< Lua config file — second pass
  UFSRV_CONFIG_SOURCE_CLI          = 3,   ///< command-line override — final pass
  UFSRV_CONFIG_SOURCE_RUNTIME      = 4   ///< regular runtime manipulation
} UfsrvConfigSource;

// Canonical value

/*! Value stored in one slot of ufsrv_config_store[], carried in cold
 *  catalogue defaults, and passed to lifecycle callbacks.
 *
 *  The active member of @p stored_value is determined by the corresponding
 *  cold catalogue entry's @p value_type field — it is NOT tagged at runtime.
 *  The generated accessor function knows the correct member to access by
 *  construction.
 *
 *  @p type_allocation_sz carries the buffer size for STR fields and the byte count
 *  for BLOB/STRUCT fields (used by GenericBlobSetter for memcpy).  It is
 *  0 for INT, PTR, and ENUM fields.
 */
typedef struct {
  union {
    int64_t     int_val;       ///< INT, ENUM — 64-bit scalar value, also for bool
    char       *str_val;       ///< STR — copy-by-value string buffer
    void       *ptr_val;       ///< PTR — copy-by-reference
    void       *blob_val;      ///< STRUCT — copy-by-value (memcpy source / malloc'd target), and array storage
    double      double_value;
  } stored_value;
  size_t  type_allocation_sz;  ///< byte count for memcpy / buffer size for STR; 0 for scalars
  size_t  collection_sz;      ///< arrays, lists: total allocation: type_allocation_sz * collection_sz
  UfsrvConfigSource source;     ///< repository source of stored value
} UfsrvConfigValue;

typedef struct ConfFileSymbolTableEntry ConfFileSymbolTableEntry;

/** A row's entries carry key, type and value. Iterate by field_count. */
typedef struct {
  size_t field_count;
  const ConfFileSymbolTableEntry *fields;
} ConfFileCollectionRow;

/** COLLECTION payload at stored_value.blob_val. All rows, entries and strings
 * are held in one allocation. Rows preserve Lua array order. Fields are named
 * and have no positional ordering. */
typedef struct {
  size_t row_count;
  ConfFileCollectionRow rows[];
} ConfFileCollection;

// Setter item — operating-state interface

/*! Value container passed to UfsrvConfigSet<Field>() functions.
 *
 *  Wraps a UfsrvConfigValue with a type tag and source layer for the
 *  operating-state setter interface.  Not used during bootstrap/backfill —
 *  lifecycle callbacks receive UfsrvConfigValue directly.
 *
 *  @code{.c}
 *  UfsrvConfigItem item = {
 *      .type  = UFSRV_CONFIG_VALUE_INT,
 *      .layer = UFSRV_CONFIG_LAYER_CLI,
 *      .value = { .stored_value = { .int_val = 42 } }
 *  };
 *  UfsrvConfigSetServerid(&item);
 *  @endcode
 */
typedef struct {
  UfsrvConfigValueType type;        ///< what kind of value this item carries
  UfsrvConfigSource    layer;       ///< sourcing provenance
  UfsrvConfigValue     value;       ///< the value itself
} UfsrvConfigItem;

#endif /* UFSRV_CORE_UFSRV_CONFIG_VALUE_TYPE_H */
