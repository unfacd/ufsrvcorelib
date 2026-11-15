/**
 * @file ufsrv_config_field_descriptor.h
 * @brief Build-time descriptor, runtime field table entry, domain/source/validation enums.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * UfsrvConfigFieldDescriptorForGenerator is the cold catalogue entry type.
 * It is consumed by the ufsrv_config_generator build‑time tool and is NOT
 * linked into the server binary.  The generator emits UfsrvConfigField
 * entries into the runtime field_table[].
 *
 * UfsrvConfigField is the runtime field table entry — value defaults,
 * lifecycle callbacks, and per‑field metadata that survives into the
 * compiled library.
 */

#ifndef UFSRV_CORE_UFSRV_CONFIG_FIELD_DESCRIPTOR_H
#define UFSRV_CORE_UFSRV_CONFIG_FIELD_DESCRIPTOR_H

#include <stddef.h>   /* size_t */
#include <stdbool.h>
#include <uflib/main_types.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_value_type.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/pretty_print_format_enum.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/pretty_print_scope_enum.h>

typedef struct UfsrvConfigFieldDescriptorForGenerator UfsrvConfigFieldDescriptorForGenerator;
// Backward-compat alias for generator
typedef UfsrvConfigFieldDescriptorForGenerator UfsrvConfigFieldDescriptor;

// Config domain — Config (inert values) or Runtime (wired objects)

/*! Configuration items domains: where items originate from and their defining context. */
typedef enum
{
  UFSRV_CONFIG_DOMAIN_CONFIG  = 0, ///< Config item is defined in config files
  UFSRV_CONFIG_DOMAIN_RUNTIME = 1  ///< Config item is a runtime state variable
} UfsrvConfigDomain;

// Backward-compat aliases (pre-v3.0.0) — UfsrvConfigSource now in ufsrv_config_value_type.h
typedef UfsrvConfigSource UfsrvConfigLayer;
#define UFSRV_CONFIG_LAYER_DEFAULTS  UFSRV_CONFIG_SOURCE_DEFAULTS
#define UFSRV_CONFIG_LAYER_FILE      UFSRV_CONFIG_SOURCE_CONFIG_FILE
#define UFSRV_CONFIG_LAYER_CLI       UFSRV_CONFIG_SOURCE_CLI

#if 0
IEC binary quantities
| Symbol| Name     | Value in Bytes                     | Power of2 |
|---    | ---      | ---------------------------------- | ----------|
|  KiB  | Kibibyte | 1,024                              | 2  ^10    |
|  MiB  | Mebibyte | 1,048,576                          | 2  ^20    |
|  GiB  | Gibibyte | 1,073,741,824                      | 2  ^30    |
|  TiB  | Tebibyte | 1,099,511,627,776                  | 2  ^40    |
|  PiB  | Pebibyte | 1,125,899,906,842,624              | 2  ^50    |
|  EiB  | Exbibyte | 1,152,921,504,606,846,976          | 2  ^60    |
|  ZiB  | Zebibyte | 1,180,591,620,717,411,303,424      | 2  ^70    |
|  YiB  | Yobibyte | 1,208,925,819,614,629,174,706,176  | 2  ^80    |

#endif

// Validation

/*! Constraint applied to a Config field by UfsrvConfigValidate(). */
typedef enum
{
  UFSRV_CONFIG_CHECK_NONE                = 0, ///< no validation — always passes
  UFSRV_CONFIG_CHECK_RANGE               = 1, ///< int must be in [min, max]
  UFSRV_CONFIG_CHECK_NOT_EMPTY           = 2, ///< string must be non‑NULL and non‑empty
  UFSRV_CONFIG_CHECK_PATH_EXISTS         = 3, ///< string must be a path that exists on the filesystem
  UFSRV_CONFIG_CHECK_IS_DIRECTORY        = 4, ///< string must be an existing directory
  UFSRV_CONFIG_CHECK_IS_FILE             = 5, ///< string must be an existing regular file
  UFSRV_CONFIG_CHECK_IS_PERCENTAGE       = 6, ///< values expressing percentages as 1-100%
  UFSRV_CONFIG_CHECK_IS_IEC_BINARY_UNITS = 7, ///< standard IEC Binary (Base-2 / Multiples of 1024)
  UFSRV_CONFIG_CHECK_ENUM_RANGE          = 8
  ///<if .value_type      = UFSRV_CONFIG_VALUE_ENUM, check the value is in the enum range for the declared type
} UfsrvConfigCheckType;

/*! Range constraint for UFSRV_CONFIG_CHECK_RANGE. */
typedef struct
{
  int min; ///< inclusive lower bound
  int max; ///< inclusive upper bound
} UfsrvConfigRange;

/*! Validation rule attached to a Config field. */
typedef struct
{
  UfsrvConfigCheckType type; ///< which check to apply
  union
  {
    UfsrvConfigRange range; ///< for UFSRV_CONFIG_CHECK_RANGE
    /* path checks and not‑empty need no additional data */
  } check;
} UfsrvConfigValidation;

// Validation result

/*! Config validation outcome state. */
typedef enum
{
  UFSRV_CONFIG_OK                 = 0, ///< all fields passed validation
  UFSRV_CONFIG_ERR_RANGE          = 1, ///< int field outside allowed range
  UFSRV_CONFIG_ERR_EMPTY          = 2, ///< required string field is empty
  UFSRV_CONFIG_ERR_PATH_NOT_FOUND = 3, ///< path does not exist
  UFSRV_CONFIG_ERR_NOT_DIRECTORY  = 4, ///< path exists but is not a directory
  UFSRV_CONFIG_ERR_NOT_FILE       = 5  ///< path exists but is not a regular file
} UfsrvConfigResultState;

/*! Result returned by UfsrvConfigValidate(). */
typedef struct
{
  UfsrvConfigResultState state;   ///< OK or specific error code
  const char *           message; ///< human‑readable description (field name + details)
} UfsrvConfigResult;

// Runtime field table entry

/*! One entry in the generated field_table[] — value defaults and lifecycle
 *  callbacks.  Indexed by the unified UfsrvConfigField / UfsrvRuntimeField
 *  enum at runtime.
 *
 *  The generator wires Generic*Setter/Inflator callbacks based on value_type.
 *  Complex Runtime fields (SSL, Lua, backends) override with hand‑rolled
 *  callbacks set directly in the cold catalogue.
 */
typedef struct
{
  /**
 * @warning LAYOUT AT THE HEAD OF THIS STRUCT IS LOAD-BEARING — DO NOT REORDER.
 *
 * The generated field tables (data_ufsrv_config_field_tables_generated.c) are
 * emitted to match this exact member order, and the store dispatch in
 * ufsrv_config.c recovers the entry base from &store[0].config via
 * offsetof(UfsrvConfigFieldEntry, config).  Adding, removing, or reordering any
 * member ahead of `config` (e.g. a new member before `field_name`, or moving
 * `config` itself) shifts config's offset and silently corrupts every
 * UfsrvConfigConfFileValuesInstance() / UfsrvConfigRuntimeValuesInstance()
 * read — the historical failure was type_allocation_sz reading back as 0 and
 * aborting GenericStrSetter at startup.
 *
 * Members may only be appended AFTER `lifecycle`.  If the head layout must
 * change, update the offsetof()-based dispatch in ufsrv_config.c and regenerate
 * the field tables in the same change.
 */
  const char *field_name; ///< human readable identifier, sourced from the input catalogue
  UfsrvConfigValue config; ///< Actual store of a config value
  struct
  {
    /*! Bootstrap — set factory state from the default_value passed by the
     *  dispatch.  @p value_ptr points to field_table[idx].default_value.
     *  @p is_deflated_first when true frees any previously-allocated
     *  storage before allocating fresh memory; false on first bootstrap. */
    void (*on_inflated)(int field_index, const UfsrvConfigValue *value_ptr, bool is_deflated_first);
    /*! Backfill — value arrived from external source (file, CLI, or defaults).
     *  @p source is one of UfsrvConfigSource; injected externally. */
    void (*on_value_available)(int field_index, const UfsrvConfigValue *value_ptr, UfsrvConfigSource source);
    /*! Config file rehashed — field was reloaded.  Generic stub syslogs;
     *  fields needing reload logic override. */
    void (*on_value_reloaded)(int field_index, const UfsrvConfigValue *value_ptr);
    /*! Shutdown — RUNTIME only.  The dispatch passes the current store
     *  value.  The callback casts to the concrete type and calls the
     *  destructor.  The dispatch nullifies the store slot afterwards. */
    void (*on_teardown)(int field_index, UfsrvConfigValue *value_ptr);
    /** Callback for producing formatted representation  of the stored value.
     *  @param field_index enum UfsrvConfigField
     *  @param value_ptr payload representing stored value
     *  @param generator_input_value
     *  @param pretty_print_scope how much information to present
     *  @param buffer_provided user provided buffer to write to
     *  @param pretty_print_format output format
     */
    BufferDescriptor * (*on_as_string)(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided);
    char * (*on_pretty_print)(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *generator_input_value, enum PrettyPrintFormat pretty_print_format, enum PrettyPrintScope pretty_print_scope, BufferDescriptor *buffer_provided);
  } lifecycle;
} UfsrvConfigFieldEntry;

// Build-time descriptor — consumed by the generator

/*! Describes one Config or Runtime field in the cold catalogue.
 *
 *  Cold catalogue arrays of this type are consumed by the ufsrv_config_generator
 *  build‑time tool.  They are NOT linked into the server binary — the generator
 *  emits the runtime UfsrvConfigField table and the hot stores.
 *
 *  @p c_lang_type is the C type string ("int", "SSL_CTX *", "struct timeval")
 *  used by the generator as the getter return type.  It is descriptor‑only —
 *  the type is baked into the generated code at compile time.
 *
 *  @p validation is consumed by the generator to emit hardcoded checks in
 *  UfsrvConfigValidate().  It does not survive into the runtime table.
 *
 *  @note The @p name pointer must be the first member — the generator detects
 *        the end of the array via a NULL name sentinel.
 */
struct UfsrvConfigFieldDescriptorForGenerator
{
  const char *          name;            ///< field identity — used in error messages and dispatch
  const char *          c_lang_type;     ///< "int", "SSL_CTX *", "struct timeval", etc. — descriptor‑only
  const char *          config_file_key; ///< CONF_FILE_LITERAL_* or NULL — Lua config key mapping
  int                   value_type;      ///< UfsrvConfigValueType — storage class
  int                   domain;          ///< UfsrvConfigDomain — CONFIG or RUNTIME
  int                   enum_index;      ///< enum UfsrvConfigField assigned by generator
  bool                  is_as_ptr_getter;
  ///< construct an additional getter with 'AsPtr' suffix (will not be honoured if is_keep_custom_getter is set, as AsPtr is a getter variant)
  bool                  is_keep_custom_getter;
  ///< Do not generate a standard getter for this item (user-supplied) (if set to 'false', will override, and transitively set is_as_ptr_getter to 'false' as well)
  bool                  is_keep_custom_setter;
  ///< Do not generate a standard setter for this item (user-supplied). Do not set a generic setter in the catalogue if 'true'
  UfsrvConfigFieldEntry config_field_default; ///< default value — carried through to runtime field_table[]
  /*!
   * Lifecycle callback names — symbolic references emitted by the generator
   * into the runtime field table.  NULL or empty means the generator
   * auto‑wires a generic callback based on value_type.
   *
   * These are STRINGS consumed by the generator at build time, NOT function
   * pointers.  The generator emits the corresponding symbol reference into
   * the generated field_table[] entry.
   */
  struct
  {
    const char *on_inflated;        ///< bootstrap — "GenericIntInflator", user-supplied or NULL. Do not set if hint
    const char *on_value_available; ///< backfill — "GenericIntSetter", user-supplied or NULL
    const char *on_value_reloaded;  ///< rehash  — "GenericReloadedStub", user-supplied or NULL
    const char *on_teardown;        ///< shutdown — "ssl_ctx_teardown" or NULL (no teardown)
    const char *on_as_string;    ///< output formatting —
    const char *on_pretty_print;    ///< output formatting —
  } lifecycle;

  UfsrvConfigValidation validation; ///< post‑population constraint — consumed by generator → Validate()
} ;


#endif /* UFSRV_CORE_UFSRV_CONFIG_FIELD_DESCRIPTOR_H */
