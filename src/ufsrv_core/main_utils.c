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

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include <config_ufsrvcorelib.h>
#endif

/* Host build-config constant; ufsrvcorelib's config template does not carry it,
 * so a translation unit compiling this file still needs the fallback. */
#ifndef CONFIG_CONFIGFILE_DEFAULT_PID_DIR
# define CONFIG_CONFIGFILE_DEFAULT_PID_DIR "/var/run"
#endif

# include <bsd/libutil.h>
# include <errno.h>
# include <fcntl.h>
# include <limits.h>
# include <signal.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <sys/stat.h>
# include <syslog.h>
# include <unistd.h>

# include <uflib/standard_defs.h>
# include <uflib/utils_str.h>
# include <uflib/utils_sys.h>

# include <ufsrvcorelib/ufsrv_core/cli_configurator/command_line_args_config_type.h>
# include <ufsrvcorelib/ufsrv_core/system/server_process_mode_enum.h>
# include <ufsrvcorelib/ufsrv_core/configfile_processor/configfile_processor.h>
# include <ufsrvcorelib/ufsrv_core/configfile_processor/configfile_validator_descriptor_type.h>
# include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>
# include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_value_type.h>
# include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
# include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_secret.h>
# include <ufsrvcorelib/ufsrv_core/system/utils_runtime.h>
# include <ufsrvcorelib/ufsrv_core/system/ufsrv_secret_key_type.h>

#include <ufsrvcorelib/ufsrv_core/main_utils.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_logger.h>

#include <uflib/logger/logger.h>

#include <uflib/file_loader_service_concurrent/file_loader_concurrent_service.h>

const UfLoggerConfig *
ProvideDefaultLoggerConfig(void)
{
  static const UfLoggerConfig default_logger_config = {
      .category        = "ufsrv",
      .minimum_level   = UF_LOGGER_LEVEL_DEFAULT,
      .destination     = UF_LOGGER_DESTINATION_FILE,
      .file_path       = "/opt/ufsrv/log/ufsrv.log",
      .max_file_bytes  = 0u,
      .max_archives    = 0u,
      .syslog_facility = UF_LOGGER_SYSLOG_FACILITY_DEFAULT,
      .format_pattern  = "%d(%F %T) %-8V [%c] %f:%L %m%n",
  };

  return &default_logger_config;
}

/* Process-wide handle: written once by OpenLogger() before worker pools start, read-only afterwards. */
static UfLogger *s_ufsrv_process_logger;

UfLogger *
OpenLogger(const UfLoggerConfig *logger_config)
{
  UfLogger      *logger_ptr = NULL;

  if (UfLoggerCreate(logger_config, &logger_ptr) != UF_LOGGER_STATUS_OK) return NULL;

  s_ufsrv_process_logger = logger_ptr;
  return logger_ptr;
}

UfLogger *
UfsrvLogger(void)
{
  return s_ufsrv_process_logger;
}

/* ── Config validation / reporting ──────────────────────────────────────── */

/**
 * @brief Report a config-validation finding to syslog and stderr.
 */
void
ConfigValidationFinding(const char *scope, const char *key, const char *severity, const char *message)
{
  int prio = (strcmp(severity, "error") == 0) ? LOG_ERR : LOG_WARNING;
  syslog(prio, "config validation: [%s] %s.%s: %s", severity, scope, key, message);
  fprintf(stderr, "CONFIG:%s:%s.%s: %s\n", severity, scope, key, message);
}

/**
 * @brief Bridge a symbol-table transfer error onto the validation-finding path.
 */
void
ConfigReportSymbolError(const ConfFileSymbolTableEntry *entry, const char *message)
{
  ConfigValidationFinding(entry->scope ? entry->scope : "", entry->key ? entry->key : "", "error", message ? message : "");
}

/* ── CLI bootstrap ──────────────────────────────────────────────────────── */

/**
 * @brief Apply CLI-provided config-dir / config-file overrides to the config store.
 *
 * CLI values win over defaults and config-file values. The config store must
 * already have undergone init and inflate state transitions.
 */
void
ConfigBootstrapCliDefaults(const CommandLineArgsConfig *cli_configuration_provided)
{
  if (IS_STR_LOADED(cli_configuration_provided->config_dir)) {
    UfsrvConfigSetConfigDir(&(UfsrvConfigItem){.value = {.stored_value.str_val =  (char *)cli_configuration_provided->config_dir, .type_allocation_sz = strlen(cli_configuration_provided->config_dir) + 1}}, UFSRV_CONFIG_SOURCE_CLI);

    char credential_path[SBUF] = {0};
    snprintf(credential_path, sizeof(credential_path), "%s/etc/%s/%s", cli_configuration_provided->config_dir, CONFIG_DEFAULT_UFSRV_SECRETS_DIRECTORY, SECRET_CREDENTIAL_ID);
    UfsrvConfigSetUfsrvSecretKeyStore(&(UfsrvConfigItem){.value = {.stored_value.str_val = credential_path, .type_allocation_sz = strlen(credential_path) + 1}}, UFSRV_CONFIG_SOURCE_CLI);
  }

  if (IS_STR_LOADED(cli_configuration_provided->config_file)) {
    UfsrvConfigSetConfigFile(&(UfsrvConfigItem){.value = {.stored_value.str_val =  (char *)cli_configuration_provided->config_file, .type_allocation_sz = strlen(cli_configuration_provided->config_file) + 1}}, UFSRV_CONFIG_SOURCE_CLI);
  }
}

/**
 * @brief Inject the CLI server-process-mode override into the config store.
 */
void
ConfigInjectOverridingCliArguments(const CommandLineArgsConfig *cli_configuration_provided)
{
  UfsrvConfigSetServerInstanceId(&(UfsrvConfigItem){.value = {.stored_value.int_val =  cli_configuration_provided->instance_id}}, UFSRV_CONFIG_SOURCE_CLI);

  if (cli_configuration_provided->server_process_mode != SERVER_PROCESS_MODE_DEFAULT) {
    UfsrvConfigSetServerProcessMode(&(UfsrvConfigItem){.value = {.stored_value.int_val =  cli_configuration_provided->server_process_mode}}, UFSRV_CONFIG_SOURCE_CLI);
  }
}

/* ── Privilege / daemon ─────────────────────────────────────────────────── */

static void
_UfsrvDropRootPrivilegesOnSuccess(void)
{
  syslog(LOG_NOTICE, ">>> UfsrvDropRootPrivileges (user:'%s'): Running with nominated user privileges...", UfsrvConfigSysRuntimeRunAsUser());
}

/**
 * @brief Drop root privileges, if a run-as user has been configured.
 */
void
UfsrvDropRootPrivileges(void)
{
  if (*UfsrvConfigSysRuntimeRunAsUser()) {
    DropRootPrivileges(UfsrvConfigSysRuntimeRunAsUser(), UfsrvConfigSysRuntimeChroot(), _UfsrvDropRootPrivilegesOnSuccess);
  }
  else {
    char current_username[L_cuserid + 1] = {0};
    cuserid(current_username);
    syslog(LOG_NOTICE, ">>> %s (username: '%s'): Keeping current user privileges....", __func__, current_username);
  }
}

/**
 * @brief Open the server pid file (or fail on an already-running instance).
 *
 * @return Pid-file handle, or NULL on error (the process exits on EEXIST).
 */
struct pidfh *
UfsrvAcquirePidFile(void)
{
  pid_t existing_pid;
  char *pid_file;
  asprintf(&pid_file, "%s/%s.pid", CONFIG_CONFIGFILE_DEFAULT_PID_DIR, UfsrvConfigServerClass());
  struct pidfh *pid_handle = pidfile_open(pid_file, 0600, &existing_pid);
  if (pid_handle == NULL) {
    if (errno == EEXIST) {
      fprintf(stderr, "ufsrv is already running: pid: %jd.", (intmax_t)existing_pid);
      free(pid_file);
      _exit(-1);
    }
    perror(STRINGIFY_PARAMETER("Failed to open PID-file: %s", pid_file));
  }
  free(pid_file);
  return pid_handle;
}

/**
 * @brief Legacy SysV double-fork daemonisation (only for SERVER_PROCESS_MODE_DAEMON).
 *
 * @return Pid-file handle for the surviving daemon process.
 */
struct pidfh *
UfsrvGoDaemon(void)
{
  struct pidfh *pid_handle = UfsrvAcquirePidFile();

  if (fork()) {
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);

    _exit(0);
  }

  umask(0);

  if (setsid() == -1) {
    syslog(LOG_ERR, "%s: ERROR: failed to become a session leader(errno=%d): EXITING...", __func__, errno);

    _exit(-1);
  }

  signal(SIGHUP, SIG_IGN);

  int pid = fork();
  if (pid == -1) {
    syslog(LOG_ERR, "%s: ERROR: failed the second fork (errno=%d): EXITING...", __func__, errno);
    _exit(-1);
  }
  else if (pid != 0) {
    _exit(0);
  }

  freopen("/dev/null", "r", stdin);
  freopen("/dev/null", "w", stdout);
  freopen("/dev/null", "w", stderr);

  if (pid_handle != NULL) pidfile_write(pid_handle);
  return pid_handle;
}

/* ── Config-file paths / schema ─────────────────────────────────────────── */

/**
 * @brief Write the resolved config-file path into the supplied buffer.
 */
void
ConfigProvideFilePath(char *buffer_provided, const size_t buffer_provided_sz)
{
  snprintf(buffer_provided, buffer_provided_sz, "%s/etc/%s", UfsrvConfigConfigDir(), UfsrvConfigConfigFile());
}

/**
 * @brief Write the resolved Lua validator-schema path into the supplied buffer.
 */
void
ConfigProvideFileValidator(char *buffer_provided, const size_t buffer_provided_sz)
{
  snprintf(buffer_provided, buffer_provided_sz, "%s/etc/%s/%s", UfsrvConfigConfigDir(), "lua", ConfigFileSchemaValidator());
}

/**
 * @brief Write the resolved Lua core-schema path into the supplied buffer.
 */
void
ConfigProvideFileCoreSchema(char *buffer_provided, const size_t buffer_provided_sz)
{
  snprintf(buffer_provided, buffer_provided_sz, "%s/etc/%s/%s", UfsrvConfigConfigDir(), "lua", ConfigFileCoreSchema());
}

/* ── Config-file processing ─────────────────────────────────────────────── */

/**
 * @brief Populate a config-file validator descriptor with standard paths.
 *
 * @note Non re-entrant: writes into the caller-supplied descriptor.
 */
void
ConfigProvideValidatorDescriptor(ConfigFileValidatorDescriptor *out, bool is_strict_validator,
                                 const char *adjunct_schema, ReportErrorCallback on_error)
{
  memset(out, 0, sizeof(*out));
  out->on_error                   = on_error;
  out->validator_args.is_strict   = is_strict_validator;
  ConfigProvideFilePath(out->config_file, sizeof(out->config_file));
  ConfigProvideFileValidator(out->validator_file, sizeof(out->validator_file));
  ConfigProvideFileCoreSchema(out->core_schema_file, sizeof(out->core_schema_file));
  snprintf(out->adjunct_schema_file, sizeof(out->adjunct_schema_file), "%s/etc/%s/%s", UfsrvConfigConfigDir(), "lua", adjunct_schema);
}

/**
 * @brief Validate the config file and transfer its values into the config store.
 */
void
ConfigProcessFile(const ConfigFileValidatorDescriptor *config_file_validator_descriptor)
{
  syslog(LOG_NOTICE, ">>> %s {config directory: '%s', config file: '%s'} Processing configuration file '%s'...", __func__, UfsrvConfigConfigDir(), UfsrvConfigConfigFile(), config_file_validator_descriptor->config_file);
  syslog(LOG_NOTICE, ">>> %s Processing configuration file: core_schema path: '%s', adjunct_schema path: '%s',  schema_validator path: '%s' ...", __func__, config_file_validator_descriptor->core_schema_file, config_file_validator_descriptor->adjunct_schema_file, config_file_validator_descriptor->validator_file);

  ConfFile cp = ConfFileValidate(config_file_validator_descriptor);
  if (IS_EMPTY(cp)) {
    syslog(LOG_ERR, "Config validation failed with error(s) — exiting");
    exit(EXIT_FAILURE);
  }

  int errors = 0;
  errors += ConfFileTransferToConfigValues(cp, config_file_validator_descriptor->core_symbols, config_file_validator_descriptor->associative_lookup_map, config_file_validator_descriptor->associative_lookup_map_sz, ConfigReportSymbolError);
  errors += ConfFileTransferToConfigValues(cp, config_file_validator_descriptor->adjunct_symbols, config_file_validator_descriptor->associative_lookup_map, config_file_validator_descriptor->associative_lookup_map_sz, ConfigReportSymbolError);
  if (errors) {
    syslog(LOG_ERR, "%s: ERROR; COULD NOT Transfer config file values to config store (errors %d) — EXITING...", __func__, errors);
    ConfFileDestroy(cp);
    exit(EXIT_FAILURE);
  }

  ConfFileDestroy(cp);
}

/**
 * @brief Marshal config-file symbol-table values into the config-file store.
 *
 * @warning ConfFileSymbolTableEntry.field_index must map correctly onto the
 *          config-store field indexation.
 */
void
ConfigPopulateConfFileStoreFromLua(const ConfFileSymbolTableEntry *lua_config_values_table)
{
  for (const ConfFileSymbolTableEntry *e = lua_config_values_table; e->scope != NULL; e++) {
    /* Keys the config file never supplied keep their generated default: only
     * values actually transferred from the file are marshalled into the store. */
    if (e->value.source != UFSRV_CONFIG_SOURCE_CONFIG_FILE) continue;

    UfsrvConfigFieldEntry *entry = UfsrvConfigConfFileStoreInstance(e->field_index);
    if (IS_PRESENT(entry->lifecycle.on_value_available)) {
      entry->lifecycle.on_value_available(e->field_index, &e->value, UFSRV_CONFIG_SOURCE_CONFIG_FILE);
      entry->config.source = UFSRV_CONFIG_SOURCE_CONFIG_FILE;
    }
  }

  UfsrvConfigRuntimeValuesInstance(UFSRV_RUNTIME_FIELD_CONF_FILE_STORE_OPERATING_STATE)->stored_value.int_val = UFSRV_CONFIG_STORE_OPERATING_STATE_VALUES_MARSHALLED;
}

void
ConfigBootstrapFileLoaderService(void)
{
  FileLoaderServiceDescriptor descriptor = {0};

  descriptor.is_file_loader_enabled = UfsrvConfigFileloaderEnabled();
  descriptor.uf_logger              = UfsrvLogger();
  descriptor.files_registry         = UfsrvConfigFileloaderFilesRegistryAsPtr();
  descriptor.blacklist_registry     = UfsrvConfigFileloaderBlackListedFilesRegistryAsPtr();

  descriptor.file_loader.service_config_params.registry_size = (uint32_t)UfsrvConfigFileloaderRegistrySize();
  descriptor.file_loader.service_config_params.loaded_size   = (uint32_t)UfsrvConfigFileloaderRegistryHotLoadedSize();

  if (UfsrvConfigRuntimeValuesInstance(UFSRV_RUNTIME_FIELD_FILE_LOADER_SERVICE)->type_allocation_sz != sizeof(descriptor)) {
    syslog(LOG_ERR, "%s: FileLoaderServiceDescriptor size %zu does not match store slot %zu", __func__, sizeof(descriptor),
           UfsrvConfigRuntimeValuesInstance(UFSRV_RUNTIME_FIELD_FILE_LOADER_SERVICE)->type_allocation_sz);
    return;
  }

  UfsrvConfigSetFileLoaderService(&(UfsrvConfigItem){.value = {.stored_value.blob_val = &descriptor, .type_allocation_sz = sizeof(descriptor)}}, UFSRV_CONFIG_SOURCE_RUNTIME);
}

/**
 * @brief Finalise sensitive configuration (currently: load the ufsrv secret key).
 */
void
ConfigFinalizeSensitiveConfig(void)
{
  ConfigLoadUfsrvSecret();
}

/**
 * @brief Load the global ufsrv secret key from the credentials store into memory.
 *
 * @details Considers the process-initiation context under which the server
 * binary was invoked; primarily, if it is under systemd control, the
 * credentials-store location configured in the service file is used.
 */
void
ConfigLoadUfsrvSecret(void)
{
  const char *credentials_dir_by_systemd = UfsrvRuntimeGetSystemdCredentialsDirectory();
  if (IS_STR_LOADED(credentials_dir_by_systemd)) {
    char credential_path[SBUF] = {0}; // must match .type_allocation_sz in config catalogue
    snprintf(credential_path, sizeof(credential_path), "%s/%s", credentials_dir_by_systemd, SECRET_CREDENTIAL_ID);
    UfsrvConfigSetUfsrvSecretKeyStore(&(UfsrvConfigItem){.value = {.stored_value.str_val = credential_path, .type_allocation_sz = strlen(credential_path) + 1}}, UFSRV_CONFIG_SOURCE_RUNTIME);
  }

  unsigned char key_raw[SECRET_KEY_BYTES] = {0};
  SecretConfigReadKeyFromCredential(key_raw, UfsrvConfigUfsrvSecretKeyStore());
  UfsrvConfigSetUfsrvSecretKey(&(UfsrvConfigItem){.value = {.stored_value.blob_val =  key_raw, .type_allocation_sz = SECRET_KEY_BYTES}}, UFSRV_CONFIG_SOURCE_RUNTIME);
}
