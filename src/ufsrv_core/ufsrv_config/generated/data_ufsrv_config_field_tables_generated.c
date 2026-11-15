/*
 * ufsrv_config_getters_setters_generated.c — GENERATED, DO NOT EDIT
 *
 * Version:    64
 * Generated:  Sat Nov 14 18:43:04 2026
 * Generator:  d3a5e2999eee548a6b4085cdfceb7e4b3147be96
 * Catalogue:  230f5576d045ec937ddf358928b972cbd2b0f89e
 *   Config fields:   52
 *   Runtime fields:  31
 */

#include <stdlib.h>  /* malloc, free */
#include <string.h>  /* memcpy, strncpy */
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
#include "ufsrv_config_generic_callbacks_priv.h"


// ── Config field table — indexed by enum UfsrvConfigField

static UfsrvConfigFieldEntry
ufsrv_config_field_table[UFSRV_CONFIG_FIELD_COUNT] = {
    [UFSRV_CONFIG_FIELD_SERVER_INSTANCE_ID] = {
        .field_name = "server_instance_id",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigServerInstanceIdAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_UFSRV_GEOGROUP] = {
        .field_name = "ufsrv_geogroup",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigUfsrvGeogroupAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SERVER_RUN_MODE] = {
        .field_name = "server_run_mode",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigServerRunModeAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_MAIN_LISTENER_PORT] = {
        .field_name = "main_listener_port",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigMainListenerPortAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS] = {
        .field_name = "main_listener_address",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigMainListenerAddressAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_MAIN_LISTENER_PROTOCOL_ID] = {
        .field_name = "main_listener_protocol_id",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigMainListenerProtocolIdAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_COMMAND_CONSOLE_PORT] = {
        .field_name = "command_console_port",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigCommandConsolePortAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_COMMAND_CONSOLE_ADDRESS] = {
        .field_name = "command_console_address",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigCommandConsoleAddressAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SERVER_CPU_AFFINITY] = {
        .field_name = "server_cpu_affinity",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigServerCpuAffinityAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_KEY_FILE] = {
        .field_name = "ssl_command_console_key_file",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleKeyFileAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_CERTIFICATE_FILE] = {
        .field_name = "ssl_command_console_certificate_file",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleCertificateFileAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_IS_ENABLED] = {
        .field_name = "ssl_command_console_is_enabled",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericBoolInflator,
            .on_value_available = GenericBoolSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleIsEnabledAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD] = {
        .field_name = "ssl_command_console_allow_list_god",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflator,
            .on_value_available = UfsrvConfigSslCommandConsoleAllowListGodSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleAllowListGodAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_BROWSER] = {
        .field_name = "ssl_command_console_allow_list_browser",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflator,
            .on_value_available = UfsrvConfigSslCommandConsoleAllowListBrowserSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleAllowListBrowserAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_DENY_LIST] = {
        .field_name = "ssl_command_console_deny_list",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflator,
            .on_value_available = UfsrvConfigSslCommandConsoleDenyListSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleDenyListAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_CLIENT_KEY_FILE] = {
        .field_name = "ssl_command_console_client_key_file",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleClientKeyFileAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_CLIENT_CERTIFICATE_FILE] = {
        .field_name = "ssl_command_console_client_certificate_file",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleClientCertificateFileAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_CLIENT_IS_ENABLED] = {
        .field_name = "ssl_command_console_client_is_enabled",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericBoolInflator,
            .on_value_available = GenericBoolSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslCommandConsoleClientIsEnabledAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SSL_CONSOLE_IS_CLIENT_CERTIFICATE_REQUIRED] = {
        .field_name = "ssl_console_is_client_certificate_required",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericBoolInflator,
            .on_value_available = GenericBoolSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSslConsoleIsClientCertificateRequiredAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_FILELOADER_ENABLED] = {
        .field_name = "fileloader_enabled",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigFileloaderEnabledAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_FILELOADER_REGISTRY_SIZE] = {
        .field_name = "fileloader_registry_size",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigFileloaderRegistrySizeAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_FILELOADER_REGISTRY_HOT_LOADED_SIZE] = {
        .field_name = "fileloader_registry_hot_loaded_size",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigFileloaderRegistryHotLoadedSizeAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_FILELOADER_BLACK_LISTED_FILES_REGISTRY] = {
        .field_name = "fileloader_black_listed_files_registry",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflator,
            .on_value_available = UfsrvConfigFileloaderBlackListedFilesRegistrySetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigFileloaderBlackListedFilesRegistryAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_FILELOADER_FILES_REGISTRY] = {
        .field_name = "fileloader_files_registry",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflator,
            .on_value_available = UfsrvConfigFileloaderFilesRegistrySetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigFileloaderFilesRegistryAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_LISTENER_WORKERS_THREAD_POOL] = {
        .field_name = "listener_workers_thread_pool",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigListenerWorkersThreadPoolAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_UFSRV_WORKERS_THREAD_POOL] = {
        .field_name = "ufsrv_workers_thread_pool",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigUfsrvWorkersThreadPoolAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SESSION_WORKERS_THREAD_POOL] = {
        .field_name = "session_workers_thread_pool",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSessionWorkersThreadPoolAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_MEMORY_SPECS_SESSIONS_PER_ALLOCATION_GROUP] = {
        .field_name = "memory_specs_sessions_per_allocation_group",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigMemorySpecsSessionsPerAllocationGroupAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_MEMORY_SPECS_ALLOCATION_GROUPS] = {
        .field_name = "memory_specs_allocation_groups",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigMemorySpecsAllocationGroupsAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_MEMORY_SPECS_ALLOCATION_THRESHOLD_TRIGGER] = {
        .field_name = "memory_specs_allocation_threshold_trigger",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigMemorySpecsAllocationThresholdTriggerAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_STATS_BACKEND_ADDRESS] = {
        .field_name = "stats_backend_address",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigStatsBackendAddressAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_STATS_BACKEND_PORT] = {
        .field_name = "stats_backend_port",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigStatsBackendPortAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_USER_TIMEOUTS_UNAUTHENTICATED_TIMEOUT] = {
        .field_name = "user_timeouts_unauthenticated_timeout",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigUserTimeoutsUnauthenticatedTimeoutAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_USER_TIMEOUTS_CONNECTED_TIMEOUT] = {
        .field_name = "user_timeouts_connected_timeout",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigUserTimeoutsConnectedTimeoutAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_USER_TIMEOUTS_SUSPENDED_TIMEOUT] = {
        .field_name = "user_timeouts_suspended_timeout",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigUserTimeoutsSuspendedTimeoutAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_BUFFER_SIZES_INCOMING_BUFFER_SIZE] = {
        .field_name = "buffer_sizes_incoming_buffer_size",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigBufferSizesIncomingBufferSizeAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_BUFFER_SIZES_INCOMING_BUFFER_MAXSIZE] = {
        .field_name = "buffer_sizes_incoming_buffer_maxsize",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigBufferSizesIncomingBufferMaxsizeAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_BUFFER_SIZES_OUTGOING_BUFFER_SIZE] = {
        .field_name = "buffer_sizes_outgoing_buffer_size",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigBufferSizesOutgoingBufferSizeAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_BUFFER_SIZES_HOLDING_BUFFER_SIZE] = {
        .field_name = "buffer_sizes_holding_buffer_size",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflator,
            .on_value_available = GenericIntSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigBufferSizesHoldingBufferSizeAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SYS_RUNTIME_RUN_AS_USER] = {
        .field_name = "sys_runtime_run_as_user",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSysRuntimeRunAsUserAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_SYS_RUNTIME_CHROOT] = {
        .field_name = "sys_runtime_chroot",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigSysRuntimeChrootAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_LOGGER_DRIVER] = {
        .field_name = "logger_driver",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigLoggerDriverAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_LOGGER_FORMAT] = {
        .field_name = "logger_format",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigLoggerFormatAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_LOGGER_PREFIX] = {
        .field_name = "logger_prefix",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigLoggerPrefixAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_RATELIMIT_IS_ENABLED] = {
        .field_name = "ratelimit_is_enabled",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericBoolInflator,
            .on_value_available = GenericBoolSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigRatelimitIsEnabledAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_RATELIMIT_IS_BYPASS_PRIVATE_IP] = {
        .field_name = "ratelimit_is_bypass_private_ip",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericBoolInflator,
            .on_value_available = GenericBoolSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigRatelimitIsBypassPrivateIpAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_OAUTH_ACCESS_TOKEN_FORMAT] = {
        .field_name = "oauth_access_token_format",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigOauthAccessTokenFormatAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_OAUTH_JWT_ISSUER] = {
        .field_name = "oauth_jwt_issuer",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigOauthJwtIssuerAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_OAUTH_JWT_SIGNING_KEY] = {
        .field_name = "oauth_jwt_signing_key",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigOauthJwtSigningKeyAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_OAUTH_JWT_ACTIVE_KID] = {
        .field_name = "oauth_jwt_active_kid",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigOauthJwtActiveKidAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_EMAIL_API_KEY] = {
        .field_name = "email_api_key",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigEmailApiKeyAsString,
        },
    },
    [UFSRV_CONFIG_FIELD_EMAIL_API_URL] = {
        .field_name = "email_api_url",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflator,
            .on_value_available = GenericStrSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_as_string       = UfsrvConfigEmailApiUrlAsString,
        },
    },
};

// ── Runtime field table — indexed by enum UfsrvRuntimeField

static UfsrvConfigFieldEntry
ufsrv_runtime_field_table[UFSRV_RUNTIME_FIELD_COUNT] = {
    [UFSRV_RUNTIME_FIELD_CONF_FILE_STORE_OPERATING_STATE] = {
        .field_name = "conf_file_store_operating_state",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflatorRuntime,
            .on_value_available = GenericIntSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigConfFileStoreOperatingStateAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_RUNTIME_STORE_OPERATING_STATE] = {
        .field_name = "runtime_store_operating_state",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflatorRuntime,
            .on_value_available = GenericIntSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigRuntimeStoreOperatingStateAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_UFSRV_SECRET_KEY] = {
        .field_name = "ufsrv_secret_key",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = GenericStructSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStructTeardownRuntime,
            .on_as_string       = UfsrvConfigUfsrvSecretKeyAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_UFSRV_SECRET_KEY_STORE] = {
        .field_name = "ufsrv_secret_key_store",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflatorRuntime,
            .on_value_available = GenericStrSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStrTeardownRuntime,
            .on_as_string       = UfsrvConfigUfsrvSecretKeyStoreAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SERVERID_RUNTIME_RANDOM] = {
        .field_name = "serverid_runtime_random",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflatorRuntime,
            .on_value_available = GenericIntSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigServeridRuntimeRandomAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SERVERPID] = {
        .field_name = "serverpid",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflatorRuntime,
            .on_value_available = GenericIntSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigServerpidAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SERVER_CLASS] = {
        .field_name = "server_class",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflatorRuntime,
            .on_value_available = GenericStrSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStrTeardownRuntime,
            .on_as_string       = UfsrvConfigServerClassAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SERVER_DESCRIPTIVE_NAME] = {
        .field_name = "server_descriptive_name",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflatorRuntime,
            .on_value_available = GenericStrSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStrTeardownRuntime,
            .on_as_string       = UfsrvConfigServerDescriptiveNameAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_WHEN] = {
        .field_name = "when",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflatorRuntime,
            .on_value_available = GenericIntSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigWhenAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_UFSRV_TIME] = {
        .field_name = "ufsrv_time",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = GenericStructSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStructTeardownRuntime,
            .on_as_string       = UfsrvConfigUfsrvTimeAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SERVER_PROCESS_MODE] = {
        .field_name = "server_process_mode",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflatorRuntime,
            .on_value_available = GenericIntSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigServerProcessModeAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_LOG_MODE] = {
        .field_name = "log_mode",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflatorRuntime,
            .on_value_available = GenericIntSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigLogModeAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SSL_SUPPORT] = {
        .field_name = "ssl_support",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericIntInflatorRuntime,
            .on_value_available = GenericIntSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigSslSupportAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_CONFIG_DIR] = {
        .field_name = "config_dir",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflatorRuntime,
            .on_value_available = GenericStrSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStrTeardownRuntime,
            .on_as_string       = UfsrvConfigConfigDirAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_CONFIG_FILE] = {
        .field_name = "config_file",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStrInflatorRuntime,
            .on_value_available = GenericStrSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStrTeardownRuntime,
            .on_as_string       = UfsrvConfigConfigFileAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_LUA_HANDLE] = {
        .field_name = "lua_handle",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigLuaHandleAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_INSTRUMENTATION_BACKEND] = {
        .field_name = "instrumentation_backend",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigInstrumentationBackendAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_INSTRUMENTATION_BACKEND_SERVER] = {
        .field_name = "instrumentation_backend_server",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = GenericStructSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStructTeardownRuntime,
            .on_as_string       = UfsrvConfigInstrumentationBackendServerAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SESSIONS_DELEGATOR] = {
        .field_name = "sessions_delegator",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigSessionsDelegatorAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_WORK_DELEGATOR_PIPE] = {
        .field_name = "work_delegator_pipe",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigWorkDelegatorPipeAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_PIPEFDS] = {
        .field_name = "pipefds",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = GenericStructSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigPipefdsAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SSL_COMMAND_CONSOLE] = {
        .field_name = "ssl_command_console",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigSslCommandConsoleAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SSL_CTX] = {
        .field_name = "ssl_ctx",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigSslCtxAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SSL_USER_CTX] = {
        .field_name = "ssl_user_ctx",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigSslUserCtxAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_SSL_MUTEXES] = {
        .field_name = "ssl_mutexes",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigSslMutexesAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_THREAD_HANDLE_TIMER_DELEGATOR] = {
        .field_name = "thread_handle_timer_delegator",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = GenericStructSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = GenericStructTeardownRuntime,
            .on_as_string       = UfsrvConfigThreadHandleTimerDelegatorAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_FILE_LOADER_SERVICE] = {
        .field_name = "file_loader_service",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = GenericStructSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStructTeardownRuntime,
            .on_as_string       = UfsrvConfigFileLoaderServiceAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_OAUTH_SETTINGS] = {
        .field_name = "oauth_settings",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = UfsrvConfigOauthSettingsSetter,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStructTeardownRuntime,
            .on_as_string       = UfsrvConfigOauthSettingsAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_UFSRV_RESULT] = {
        .field_name = "ufsrv_result",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = GenericStructSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStructTeardownRuntime,
            .on_as_string       = UfsrvConfigUfsrvResultAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_THREADS_SUBSYSTEM_UFSRV_ONCE] = {
        .field_name = "threads_subsystem_ufsrv_once",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = GenericStructInflatorRuntime,
            .on_value_available = GenericStructSetterRuntime,
            .on_value_reloaded  = GenericReloadedStub,
            .on_teardown        = GenericStructTeardownRuntime,
            .on_as_string       = UfsrvConfigThreadsSubsystemUfsrvOnceAsString,
        },
    },
    [UFSRV_RUNTIME_FIELD_UF_LOGGER] = {
        .field_name = "uf_logger",
        .config = { 0 },
        .lifecycle = {
            .on_inflated        = NULL,
            .on_value_available = GenericPtrSetterRuntime,
            .on_value_reloaded  = NULL,
            .on_teardown        = NULL,
            .on_as_string       = UfsrvConfigUfLoggerAsString,
        },
    },
};

// ── Config field compiled defaults — indexed by enum UfsrvConfigField

static const UfsrvConfigValue
ufsrv_config_field_defaults_table[UFSRV_CONFIG_FIELD_COUNT] = {
    [UFSRV_CONFIG_FIELD_SERVER_INSTANCE_ID] = { .stored_value = { .int_val = 1 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_UFSRV_GEOGROUP] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_SERVER_RUN_MODE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_MAIN_LISTENER_PORT] = { .stored_value = { .int_val = 19701 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_MAIN_LISTENER_ADDRESS] = { .stored_value = { .str_val = "127.0.0.1" }, .type_allocation_sz = 257 },
    [UFSRV_CONFIG_FIELD_MAIN_LISTENER_PROTOCOL_ID] = { .stored_value = { .int_val = 1 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_COMMAND_CONSOLE_PORT] = { .stored_value = { .int_val = 19700 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_COMMAND_CONSOLE_ADDRESS] = { .stored_value = { .str_val = "127.0.0.1" }, .type_allocation_sz = 257 },
    [UFSRV_CONFIG_FIELD_SERVER_CPU_AFFINITY] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_KEY_FILE] = { .stored_value = { .str_val = "ufsrv_console_ssl.key" }, .type_allocation_sz = 257 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_CERTIFICATE_FILE] = { .stored_value = { .str_val = "ufsrv_console_certificate.pem" }, .type_allocation_sz = 257 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_IS_ENABLED] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 208 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_BROWSER] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 208 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_DENY_LIST] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 208 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_CLIENT_KEY_FILE] = { .stored_value = { .str_val = "ufsrv_console_client_ssl.key" }, .type_allocation_sz = 257 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_CLIENT_CERTIFICATE_FILE] = { .stored_value = { .str_val = "ufsrv_console_client_certificate.pem" }, .type_allocation_sz = 257 },
    [UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_CLIENT_IS_ENABLED] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_SSL_CONSOLE_IS_CLIENT_CERTIFICATE_REQUIRED] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_FILELOADER_ENABLED] = { .stored_value = { .int_val = 1 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_FILELOADER_REGISTRY_SIZE] = { .stored_value = { .int_val = 128 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_FILELOADER_REGISTRY_HOT_LOADED_SIZE] = { .stored_value = { .int_val = 64 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_FILELOADER_BLACK_LISTED_FILES_REGISTRY] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 32 },
    [UFSRV_CONFIG_FIELD_FILELOADER_FILES_REGISTRY] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 32 },
    [UFSRV_CONFIG_FIELD_LISTENER_WORKERS_THREAD_POOL] = { .stored_value = { .int_val = 1 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_UFSRV_WORKERS_THREAD_POOL] = { .stored_value = { .int_val = 2 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_SESSION_WORKERS_THREAD_POOL] = { .stored_value = { .int_val = 2 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_MEMORY_SPECS_SESSIONS_PER_ALLOCATION_GROUP] = { .stored_value = { .int_val = 1024 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_MEMORY_SPECS_ALLOCATION_GROUPS] = { .stored_value = { .int_val = 10 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_MEMORY_SPECS_ALLOCATION_THRESHOLD_TRIGGER] = { .stored_value = { .str_val = "10%" }, .type_allocation_sz = 5 },
    [UFSRV_CONFIG_FIELD_STATS_BACKEND_ADDRESS] = { .stored_value = { .str_val = "127.0.0.1" }, .type_allocation_sz = 129 },
    [UFSRV_CONFIG_FIELD_STATS_BACKEND_PORT] = { .stored_value = { .int_val = 8125 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_USER_TIMEOUTS_UNAUTHENTICATED_TIMEOUT] = { .stored_value = { .int_val = 60 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_USER_TIMEOUTS_CONNECTED_TIMEOUT] = { .stored_value = { .int_val = 120 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_USER_TIMEOUTS_SUSPENDED_TIMEOUT] = { .stored_value = { .int_val = 60 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_BUFFER_SIZES_INCOMING_BUFFER_SIZE] = { .stored_value = { .int_val = 2048 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_BUFFER_SIZES_INCOMING_BUFFER_MAXSIZE] = { .stored_value = { .int_val = 1024000 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_BUFFER_SIZES_OUTGOING_BUFFER_SIZE] = { .stored_value = { .int_val = 1024 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_BUFFER_SIZES_HOLDING_BUFFER_SIZE] = { .stored_value = { .int_val = 1024 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_SYS_RUNTIME_RUN_AS_USER] = { .stored_value = { .str_val = "ufsrv" }, .type_allocation_sz = 65 },
    [UFSRV_CONFIG_FIELD_SYS_RUNTIME_CHROOT] = { .stored_value = { .str_val = "/" }, .type_allocation_sz = 257 },
    [UFSRV_CONFIG_FIELD_LOGGER_DRIVER] = { .stored_value = { .str_val = "file" }, .type_allocation_sz = 16 },
    [UFSRV_CONFIG_FIELD_LOGGER_FORMAT] = { .stored_value = { .str_val = "%d(%F %T) %-8V [%c] %f:%L %m%n" }, .type_allocation_sz = 4096 },
    [UFSRV_CONFIG_FIELD_LOGGER_PREFIX] = { .stored_value = { .str_val = "ufsrv" }, .type_allocation_sz = 128 },
    [UFSRV_CONFIG_FIELD_RATELIMIT_IS_ENABLED] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_RATELIMIT_IS_BYPASS_PRIVATE_IP] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_CONFIG_FIELD_OAUTH_ACCESS_TOKEN_FORMAT] = { .stored_value = { .str_val = "jwt" }, .type_allocation_sz = 16 },
    [UFSRV_CONFIG_FIELD_OAUTH_JWT_ISSUER] = { .stored_value = { .str_val = "" }, .type_allocation_sz = 256 },
    [UFSRV_CONFIG_FIELD_OAUTH_JWT_SIGNING_KEY] = { .stored_value = { .str_val = "" }, .type_allocation_sz = 1024 },
    [UFSRV_CONFIG_FIELD_OAUTH_JWT_ACTIVE_KID] = { .stored_value = { .str_val = "" }, .type_allocation_sz = 128 },
    [UFSRV_CONFIG_FIELD_EMAIL_API_KEY] = { .stored_value = { .str_val = "" }, .type_allocation_sz = 256 },
    [UFSRV_CONFIG_FIELD_EMAIL_API_URL] = { .stored_value = { .str_val = "" }, .type_allocation_sz = 1024 },
};

// ── Runtime field compiled defaults — indexed by enum UfsrvRuntimeField

static const UfsrvConfigValue
ufsrv_runtime_field_defaults_table[UFSRV_RUNTIME_FIELD_COUNT] = {
    [UFSRV_RUNTIME_FIELD_CONF_FILE_STORE_OPERATING_STATE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_RUNTIME_STORE_OPERATING_STATE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_UFSRV_SECRET_KEY] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 32 },
    [UFSRV_RUNTIME_FIELD_UFSRV_SECRET_KEY_STORE] = { .stored_value = { .str_val = "/opt/ufsrv/etc/secrets/ufsrv_secret_key" }, .type_allocation_sz = 128 },
    [UFSRV_RUNTIME_FIELD_SERVERID_RUNTIME_RANDOM] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_SERVERPID] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_SERVER_CLASS] = { .stored_value = { .str_val = "ufsrvwebsock" }, .type_allocation_sz = 64 },
    [UFSRV_RUNTIME_FIELD_SERVER_DESCRIPTIVE_NAME] = { .stored_value = { .str_val = "" }, .type_allocation_sz = 129 },
    [UFSRV_RUNTIME_FIELD_WHEN] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_UFSRV_TIME] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 16 },
    [UFSRV_RUNTIME_FIELD_SERVER_PROCESS_MODE] = { .stored_value = { .int_val = 2 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_LOG_MODE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_SSL_SUPPORT] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_CONFIG_DIR] = { .stored_value = { .str_val = "/opt/ufsrv" }, .type_allocation_sz = 257 },
    [UFSRV_RUNTIME_FIELD_CONFIG_FILE] = { .stored_value = { .str_val = "ufsrv.config.lua" }, .type_allocation_sz = 257 },
    [UFSRV_RUNTIME_FIELD_LUA_HANDLE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_INSTRUMENTATION_BACKEND] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_INSTRUMENTATION_BACKEND_SERVER] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 16 },
    [UFSRV_RUNTIME_FIELD_SESSIONS_DELEGATOR] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_WORK_DELEGATOR_PIPE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_PIPEFDS] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 8 },
    [UFSRV_RUNTIME_FIELD_SSL_COMMAND_CONSOLE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_SSL_CTX] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_SSL_USER_CTX] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_SSL_MUTEXES] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
    [UFSRV_RUNTIME_FIELD_THREAD_HANDLE_TIMER_DELEGATOR] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 8 },
    [UFSRV_RUNTIME_FIELD_FILE_LOADER_SERVICE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 112 },
    [UFSRV_RUNTIME_FIELD_OAUTH_SETTINGS] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 344 },
    [UFSRV_RUNTIME_FIELD_UFSRV_RESULT] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 16 },
    [UFSRV_RUNTIME_FIELD_THREADS_SUBSYSTEM_UFSRV_ONCE] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 4 },
    [UFSRV_RUNTIME_FIELD_UF_LOGGER] = { .stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },
};
