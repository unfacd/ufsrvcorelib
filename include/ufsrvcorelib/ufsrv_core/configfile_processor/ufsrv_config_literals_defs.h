/**
 * @file ufsrv_config_literals_defs.h
 * @brief Single source of truth for all ufsrv Lua config-file key literals.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Every ufsrv config key that appears in a Lua config file MUST be defined
 * here.  Both the configfile_processor symbol table and the ufsrv_config
 * cold catalogue include this header and reference these macros so that
 * there is exactly one canonical spelling for each key.
 *
 * Naming convention:
 *   CONF_FILE_LITERAL_<UPPER_SNAKE>  "literal_string"
 *
 * For dotted keys, each stem gets its own define so that parent groups can
 * be referenced independently:
 *
 *   CONF_FILE_LITERAL_SSL                  "ssl"
 *   CONF_FILE_LITERAL_SSL_CERTIFICATE      "ssl.certificate"
 *   CONF_FILE_LITERAL_SSL_KEY              "ssl.key"
 */

#ifndef UFSRV_CORE_UFSRV_CONFIG_LITERALS_DEFS_H
#define UFSRV_CORE_UFSRV_CONFIG_LITERALS_DEFS_H

// Identity & lifecycle
#define CONF_FILE_LITERAL_SERVER_INSTANCE_ID            "server_instance_id"
#define CONF_FILE_LITERAL_SERVER_RUN_MODE               "server_run_mode"
#define CONF_FILE_LITERAL_SERVER_CPU_AFFINITY            "server_cpu_affinity"
#define CONF_FILE_LITERAL_UFSRV_GEOGROUP                 "ufsrv_geogroup"

// Network listeners
#define CONF_FILE_LITERAL_MAIN_LISTENER_PORT             "main_listener_port"
#define CONF_FILE_LITERAL_MAIN_LISTENER_ADDRESS         "main_listener_address"
#define CONF_FILE_LITERAL_MAIN_LISTENER_PROTOCOL_ID      "main_listener_protocol_id"
#define CONF_FILE_LITERAL_COMMAND_CONSOLE_PORT           "command_console_port"
#define CONF_FILE_LITERAL_COMMAND_CONSOLE_BIND_ADDRESS   "command_console_address"

// Thread pools
#define CONF_FILE_LITERAL_SESSION_WORKERS_THREAD_POOL    "session_workers_thread_pool"
#define CONF_FILE_LITERAL_UFSRV_WORKERS_THREAD_POOL      "ufsrv_workers_thread_pool"
#define CONF_FILE_LITERAL_LISTENER_WORKERS_THREAD_POOL   "listener_workers_thread_pool"

// command console ssl settings
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE                                 "ssl_command_console"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_IS_ENABLED                      "ssl_command_console.is_enabled"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_IS_CLIENT_CERTIFICATE_REQUIRED  "ssl_command_console.is_client_certificate_required"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_CERTIFICATE_FILE                "ssl_command_console.certificate_file"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_KEY_FILE                        "ssl_command_console.key_file"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD                  "ssl_command_console.allow_list_god"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_ALLOW_LIST_BROWSER               "ssl_command_console.allow_list_browser"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_DENY_LIST                      "ssl_command_console.deny_list"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_LOCATION                        "ssl_command_console.location" //not in use

// command console ssl settings
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_CLIENT                                 "ssl_command_console_client"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_CLIENT_IS_ENABLED                      "ssl_command_console_client.is_enabled"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_CLIENT_CERTIFICATE_FILE                "ssl_command_console_client.certificate_file"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_CLIENT_KEY_FILE                        "ssl_command_console_client.key_file"
#define CONF_FILE_LITERAL_SSL_COMMAND_CONSOLE_CLIENT_LOCATION                        "ssl_command_console_client.location" //not in use

// stats_backend
#define CONF_FILE_LITERAL_STATS_BACKEND                  "stats_backend"
#define CONF_FILE_LITERAL_STATS_BACKEND_ADDRESS          "stats_backend.address"
#define CONF_FILE_LITERAL_STATS_BACKEND_PORT             "stats_backend.port"

// instrumentation_backend
#define CONF_FILE_LITERAL_INSTRUMENTATION_BACKEND        "instrumentation_backend"
#define CONF_FILE_LITERAL_INSTRUMENTATION_BACKEND_ADDRESS  "instrumentation_backend.address"
#define CONF_FILE_LITERAL_INSTRUMENTATION_BACKEND_PORT     "instrumentation_backend.port"

// user_timeouts
#define CONF_FILE_LITERAL_USER_TIMEOUTS                  "user_timeouts"
#define CONF_FILE_LITERAL_USER_TIMEOUTS_UNAUTHENTICATED_TIMEOUT  "user_timeouts.unauthenticated_timeout"
#define CONF_FILE_LITERAL_USER_TIMEOUTS_CONNECTED_TIMEOUT        "user_timeouts.connected_timeout"
#define CONF_FILE_LITERAL_USER_TIMEOUTS_SUSPENDED_TIMEOUT        "user_timeouts.suspended_timeout"
#define CONF_FILE_LITERAL_USER_TIMEOUTS_LOCATIONLESS_TIMEOUT     "user_timeouts.locationless_timeout"

// buffer_sizes
#define CONF_FILE_LITERAL_BUFFER_SIZES                   "buffer_sizes"
#define CONF_FILE_LITERAL_BUFFER_SIZES_INCOMING_BUFFER_SIZE   "buffer_sizes.incoming_buffer_size"
#define CONF_FILE_LITERAL_BUFFER_SIZES_INCOMING_BUFFER_MAXSIZE "buffer_sizes.incoming_buffer_maxsize"
#define CONF_FILE_LITERAL_BUFFER_SIZES_OUTGOING_BUFFER_SIZE   "buffer_sizes.outgoing_buffer_size"
#define CONF_FILE_LITERAL_BUFFER_SIZES_HOLDING_BUFFER_SIZE    "buffer_sizes.holding_buffer_size"

// memory_specs_for_session
#define CONF_FILE_LITERAL_MEMORY_SPECS_FOR_SESSION        "memory_specs_for_session"
#define CONF_FILE_LITERAL_MEMORY_SPECS_SESSIONS_PER_ALLOC_GROUP   "memory_specs_for_session.sessions_per_allocation_group"
#define CONF_FILE_LITERAL_MEMORY_SPECS_ALLOCATION_GROUPS          "memory_specs_for_session.allocation_groups"
#define CONF_FILE_LITERAL_MEMORY_SPECS_ALLOCATION_THRESHOLD       "memory_specs_for_session.allocation_threshold_trigger"

// sys_runtime
#define CONF_FILE_LITERAL_SYS_RUNTIME                    "sys_runtime"
#define CONF_FILE_LITERAL_SYS_RUNTIME_RUN_AS_USER         "sys_runtime.run_as_user"
#define CONF_FILE_LITERAL_SYS_RUNTIME_CHROOT               "sys_runtime.chroot"

// fileloader
#define CONF_FILE_LITERAL_FILELOADER                     "fileloader"
#define CONF_FILE_LITERAL_FILELOADER_ENABLED             "fileloader.is_enabled"
#define CONF_FILE_LITERAL_FILELOADER_REGISTRY_SIZE       "fileloader.registry_size"
#define CONF_FILE_LITERAL_FILELOADER_REGISTRY_HOT_LOADED_SIZE "fileloader.registry_hot_loaded_size"
#define CONF_FILE_LITERAL_FILELOADER_BLACK_LISTED_FILES_REGISTRY  "fileloader.black_listed_files_registry"
#define CONF_FILE_LITERAL_FILELOADER_FILES_REGISTRY      "fileloader.files_registry"

// logger
#define CONF_FILE_LITERAL_LOGGER                         "logger"
#define CONF_FILE_LITERAL_LOGGER_DRIVER                   "logger.driver"
#define CONF_FILE_LITERAL_LOGGER_FORMAT                   "logger.format"
#define CONF_FILE_LITERAL_LOGGER_PREFIX                   "logger.prefix"

// oauth
#define CONF_FILE_LITERAL_OAUTH                          "oauth"
#define CONF_FILE_LITERAL_OAUTH_ACCESS_TOKEN_FORMAT      "oauth.access_token_format"
#define CONF_FILE_LITERAL_OAUTH_JWT_ISSUER               "oauth.jwt_issuer"
#define CONF_FILE_LITERAL_OAUTH_JWT_SIGNING_KEY          "oauth.jwt_signing_key"
#define CONF_FILE_LITERAL_OAUTH_JWT_ACTIVE_KID           "oauth.jwt_active_kid"

#define CONF_FILE_LITERAL_RATELIMIT                       "ratelimit"
#define CONF_FILE_LITERAL_RATELIMIT_IS_ENABLED            "ratelimit.is_enabled"
#define CONF_FILE_LITERAL_RATELIMIT_IS_BYPASS_PRIVATE_IP  "ratelimit.is_bypass_private_ip"

// cloud service accounts (enc:v1 envelopes of the service-account documents)
#define CONF_FILE_LITERAL_CLOUD_SERVICE_ACCOUNTS                                "cloud_service_accounts"
#define CONF_FILE_LITERAL_CLOUD_SERVICE_ACCOUNTS_GPC_FIREBASE_MESSAGING         "cloud_service_accounts.gpc_firebase_messaging"
#define CONF_FILE_LITERAL_CLOUD_SERVICE_ACCOUNTS_GPC_INTEGRITY                  "cloud_service_accounts.gpc_integrity"

// Media
#define CONF_FILE_LITERAL_UFSRVMEDIA_UPLOAD_URI           "ufsrvmedia.upload_uri"

// Message queue (Redis)
#define CONF_FILE_LITERAL_MSGQUEUE_IS_ENABLED         "msgqueue.is_enabled"
#define CONF_FILE_LITERAL_MSGQUEUE_ADDRESS            "msgqueue.address"
#define CONF_FILE_LITERAL_MSGQUEUE_PORT               "msgqueue.port"

// Geoip server
#define CONF_FILE_LITERAL_GEOIP_BACKEND_ADDRESS            "geoip_service.address"
#define CONF_FILE_LITERAL_GEOIP_BACKEND_PORT               "geoip_service.port"

// Persistence backend
#define CONF_FILE_LITERAL_PERSISTENCE_BACKEND_ADDRESS      "persistence_backend.address"
#define CONF_FILE_LITERAL_PERSISTENCE_BACKEND_PORT         "persistence_backend.port"
#define CONF_FILE_LITERAL_PERSISTENCE_BACKEND_TIMEOUT      "persistence_backend.timeout"

// Cache backends
#define CONF_FILE_LITERAL_CACHE_BACKEND_ADDRESS_USRMSG     "cache_backend_usrmsg.address"
#define CONF_FILE_LITERAL_CACHE_BACKEND_PORT_USRMSG        "cache_backend_usrmsg.port"
#define CONF_FILE_LITERAL_CACHE_BACKEND_TIMEOUT_USRMSG     "cache_backend_usrmsg.timeout"

#define CONF_FILE_LITERAL_CACHE_BACKEND_ADDRESS_FENCE      "cache_backend_fence.address"
#define CONF_FILE_LITERAL_CACHE_BACKEND_PORT_FENCE         "cache_backend_fence.port"
#define CONF_FILE_LITERAL_CACHE_BACKEND_TIMEOUT_FENCE      "cache_backend_fence.timeout"

//db backend
#define CONF_FILE_LITERAL_DB_BACKEND_ADDRESS     "db_backend.address"
#define CONF_FILE_LITERAL_DB_BACKEND_PORT        "db_backend.port"
#define CONF_FILE_LITERAL_DB_BACKEND_USERNAME    "db_backend.username"
#define CONF_FILE_LITERAL_DB_BACKEND_PASSWORD    "db_backend.password"

//outbound e-mail provider (Mailgun-compatible HTTP API)
#define CONF_FILE_LITERAL_EMAIL_API_KEY          "email.api_key"
#define CONF_FILE_LITERAL_EMAIL_API_URL          "email.api_url"

//ufsrvapi — leaf-server adjunct namespace (scope ufsrvapi, schema adjunct_schema_ufsrvapi.lua)
#define CONF_FILE_LITERAL_THIS_SERVER_ORIGIN     "this_server_origin"
#define CONF_FILE_LITERAL_INTRA_UFSRV_CLASSNAME          "intra_ufsrv_classname"
#define CONF_FILE_LITERAL_ALLOWED_ORIGINS        "allowed_origins"
#define CONF_FILE_LITERAL_UFSRVAPI_MEDIA_UPLOAD_URI "ufsrv_media_upload_uri"
#define CONF_FILE_LITERAL_CACHE_AUTHORISATION_SIZE  "cache_authorisation.size"
#define CONF_FILE_LITERAL_CACHE_AUTHORISATION_EVICTION_POLICY  "cache_authorisation.eviction_policy"

#define CONF_FILE_LITERAL_CLOUD_AUTHORIZATION_TOKEN_FCM           "cloud_authorization.token_fcm"
#define CONF_FILE_LITERAL_CLOUD_AUTHORIZATION_TOKEN_INTEGRITY_API "cloud_authorization.token_integrity_api"

#define CONF_FILE_LITERAL_THREADS_SUBSYSTEM_UFSRV_ONCE  "threads_subsystem.ufsrv_once"

//ufsrvwebsock — leaf-server adjunct namespace (scope ufsrvwebsock, schema adjunct_schema_ufsrvwebsock.lua)
#define CONF_FILE_LITERAL_UFSRVWEBSOCK_WEBSOCKET_MAX_FRAME_SIZE "websocket.max_frame_size"
#define CONF_FILE_LITERAL_UFSRVWEBSOCK_WEBSOCKET_MAX_MESSAGE_SIZE "websocket.max_message_size"
#define CONF_FILE_LITERAL_UFSRVWEBSOCK_WEBSOCKET_HEARTBEAT_INTERVAL_SECS "websocket.heartbeat_interval_secs"
#define CONF_FILE_LITERAL_UFSRVWEBSOCK_WEBSOCKET_CLOSE_TIMEOUT_SECS "websocket.close_timeout_secs"
#define CONF_FILE_LITERAL_UFSRVWEBSOCK_WEBSOCKET_MAX_CONNECTIONS_PER_SESSION "websocket.max_connections_per_session"

#endif /* UFSRV_CORE_UFSRV_CONFIG_LITERALS_DEFS_H */
