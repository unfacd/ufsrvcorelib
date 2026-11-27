/**
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef UFSRVCORELIB_UFSRV_CORE_MAIN_UTILS_H
# define UFSRVCORELIB_UFSRV_CORE_MAIN_UTILS_H

# include <stdbool.h>
# include <stddef.h>
# include <limits.h>       /* _POSIX_PATH_MAX (used by configfile_validator_descriptor_type.h) */
# include <bsd/libutil.h>  /* struct pidfh */

# include <uflib/standard_defs.h>  /* __unused (used by server_process_mode_enum.h) */

# include <ufsrvcorelib/ufsrv_core/cli_configurator/command_line_args_config_type.h>
# include <ufsrvcorelib/ufsrv_core/configfile_processor/configfile_validator_descriptor_type.h>

/* ── Config validation / reporting ──────────────────────────────────────── */

/**
 * @brief Report a config-validation finding to syslog and stderr.
 *
 * @param scope    Validation scope (e.g. "core", "adjunct").
 * @param key      Config key that triggered the finding.
 * @param severity Finding severity ("error" or "warning").
 * @param message  Human-readable description of the finding.
 */
void ConfigValidationFinding(const char *scope, const char *key, const char *severity, const char *message);

/**
 * @brief Bridge a symbol-table transfer error onto the validation-finding path.
 *
 * @param entry   Symbol-table entry that failed to resolve.
 * @param message Error message describing the failure.
 */
void ConfigReportSymbolError(const ConfFileSymbolTableEntry *entry, const char *message);

/* ── CLI bootstrap ──────────────────────────────────────────────────────── */

/**
 * @brief Apply CLI-provided config-dir / config-file overrides to the config store.
 *
 * CLI values win over defaults and config-file values. The config store must
 * already have undergone init and inflate state transitions.
 *
 * @param cli_configuration_provided Already-processed CLI configuration.
 */
void ConfigBootstrapCliDefaults(const CommandLineArgsConfig *cli_configuration_provided);

/**
 * @brief Inject the CLI server-process-mode override into the config store.
 *
 * @param cli_configuration_provided Already-processed CLI configuration.
 */
void ConfigInjectOverridingCliArguments(const CommandLineArgsConfig *cli_configuration_provided);

/* ── Privilege / daemon ─────────────────────────────────────────────────── */

/**
 * @brief Drop root privileges, if a run-as user has been configured.
 */
void UfsrvDropRootPrivileges(void);

/**
 * @brief Open the server pid file (or fail on an already-running instance).
 *
 * @return Pid-file handle, or NULL on error (the process exits on EEXIST).
 */
struct pidfh *UfsrvAcquirePidFile(void);

/**
 * @brief Legacy SysV double-fork daemonisation (only for SERVER_PROCESS_MODE_DAEMON).
 *
 * @return Pid-file handle for the surviving daemon process.
 */
struct pidfh *UfsrvGoDaemon(void);

/* ── Config-file paths / schema ─────────────────────────────────────────── */

/**
 * @brief Write the resolved config-file path into the supplied buffer.
 *
 * @param buffer_provided    Destination buffer.
 * @param buffer_provided_sz Size of the destination buffer.
 */
void ConfigProvideFilePath(char *buffer_provided, size_t buffer_provided_sz);

/**
 * @brief Write the resolved Lua validator-schema path into the supplied buffer.
 *
 * @param buffer_provided    Destination buffer.
 * @param buffer_provided_sz Size of the destination buffer.
 */
void ConfigProvideFileValidator(char *buffer_provided, size_t buffer_provided_sz);

/**
 * @brief Write the resolved Lua core-schema path into the supplied buffer.
 *
 * @param buffer_provided    Destination buffer.
 * @param buffer_provided_sz Size of the destination buffer.
 */
void ConfigProvideFileCoreSchema(char *buffer_provided, size_t buffer_provided_sz);

/* ── Config-file processing ─────────────────────────────────────────────── */

/**
 * @brief Populate a config-file validator descriptor with standard paths.
 *
 * Fills config_file, validator_file, core_schema_file and adjunct_schema_file,
 * plus the supplied on_error callback and strict flag. The host is responsible
 * for filling in the remaining fields it owns: core_symbols, adjunct_symbols,
 * associative_lookup_map and associative_lookup_map_sz.
 *
 * @param out                 Descriptor to populate.
 * @param is_strict_validator Whether the validator should run in strict mode.
 * @param adjunct_schema      Adjunct schema filename (host-specific).
 * @param on_error            Validation-finding callback.
 */
void ConfigProvideValidatorDescriptor(ConfigFileValidatorDescriptor *out, bool is_strict_validator,
                                      const char *adjunct_schema, ReportErrorCallback on_error);

/**
 * @brief Validate the config file and transfer its values into the config store.
 *
 * Reads the core/adjunct symbol tables and the associative key map from the
 * (host-populated) descriptor.
 *
 * @param config_file_validator_descriptor Fully-populated validator descriptor.
 */
void ConfigProcessFile(const ConfigFileValidatorDescriptor *config_file_validator_descriptor);

/**
 * @brief Marshal config-file symbol-table values into the config-file store.
 *
 * @warning ConfFileSymbolTableEntry.field_index must map correctly onto the
 *          config-store field indexation.
 *
 * @param lua_config_values_table NULL-terminated symbol-table map.
 */
void ConfigPopulateConfFileStoreFromLua(const ConfFileSymbolTableEntry *lua_config_values_table);

void ConfigBootstrapFileLoaderService(void);

/**
 * @brief Finalise sensitive configuration (currently: load the ufsrv secret key).
 */
void ConfigFinalizeSensitiveConfig(void);

/**
 * @brief Load the global ufsrv secret key from the credentials store into memory.
 *
 * @details Considers the process-initiation context under which the server
 * binary was invoked; primarily, if it is under systemd control, the
 * credentials-store location configured in the service file is used.
 */
void ConfigLoadUfsrvSecret(void);

#endif /* UFSRVCORELIB_UFSRV_CORE_MAIN_UTILS_H */
