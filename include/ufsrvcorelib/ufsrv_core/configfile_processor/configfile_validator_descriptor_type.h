//
// Created by ayman on 22/08/2026.
//

#ifndef UFSRVCORELIB_CONFIGFILE_VALIDATOR_DESCRIPTOR_TYPE_H
#define UFSRVCORELIB_CONFIGFILE_VALIDATOR_DESCRIPTOR_TYPE_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration */
typedef struct ConfFileSymbolTableEntry ConfFileSymbolTableEntry;

typedef void (*ReportErrorCallback)(const char *scope, const char *key, const char *severity, const char *message);

typedef struct ConfigFileValidatorDescriptor {
    char config_file[_POSIX_PATH_MAX];        ///>  Path to the Lua config file to validate.
    char core_schema_file[_POSIX_PATH_MAX];   ///> Path to the core Lua schema.
    char adjunct_schema_file[_POSIX_PATH_MAX];///> Path to the adjunct Lua schema, or NULL / "" if the server has no adjunct scope.
    char validator_file[_POSIX_PATH_MAX];     ///> Path to core_schema_validator.lua (required).
    const ConfFileSymbolTableEntry *core_symbols;   ///> Core symbol table to resolve, or NULL.
    const ConfFileSymbolTableEntry *adjunct_symbols;    ///> Adjunct symbol table to resolve, or NULL.
    ReportErrorCallback on_error;   ///> Callback invoked for each validation finding. Called with scope, key, severity ("error" | "warning"), and a human-readable message.
    const void *associative_lookup_map; ///> Associative lookup map of {Lua config-key → field_index} to establish config index number bsearch-based associated with a given Lua config-key (may be NULL if no key-map is available).
    size_t associative_lookup_map_sz;   ///> Number of entries in key_map (UFSRV_CONFIG_KEY_MAP_COUNT, or 0 if key_map is NULL).
    struct {
        bool is_strict; ///> '--strict' is passed to the validator script, which will cause it to report error when unknown field is encountered
    } validator_args;
} ConfigFileValidatorDescriptor;

#endif //UFSRVCORELIB_CONFIGFILE_VALIDATOR_DESCRIPTOR_TYPE_H
