/**
 * @file ufsrv_config_types.h
 * @brief Named struct types decomposed from the anonymous struct members of
 *        struct ufsrv (ufnetcorelib/ufsrv_type.h). Contains forward declarations of types that enable ufsrvcore lib
 *        to progress with its build process (majority of these types wiill migrate over to ufsrvcorelib over time.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRV_CORE_UFSRV_CONFIG_TYPES_H
#define UFSRV_CORE_UFSRV_CONFIG_TYPES_H

/* System */
#include <stdalign.h>      /* alignas */
#include <stdbool.h>        /* bool */
#include <sys/types.h>      /* pid_t, time_t */
#include <pthread.h>        /* pthread_t, pthread_once_t, pthread_key_t, pthread_mutex_t */

#include <sys/time.h>           /* struct timeval */
#include <netinet/in.h>         /* struct sockaddr_in */
#include <lua.h>                /* lua_State */
#include <openssl/ssl.h>        /* SSL_CTX */

/* ufsrvcorelib — enum definitions needed by generated accessor return types */
#include <ufsrvcorelib/ufsrv_core/system/server_run_mode_enum.h>       /* enum ServerRunMode */
#include <ufsrvcorelib/ufsrv_core/system/cpu_affinity_policy_enum.h>   /* CpuAffinityPolicy */
#include <ufsrvcorelib/ufsrv_core/system/server_process_mode_enum.h>   /* enum ServerProcessMode */

//dont remove
#include <ufsrvcorelib/ufsrv_core/service/file_loader/file_loader_service_descriptor_type.h>
#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend_type.h>

/* uflib */
#include <uflib/standard_defs.h>                                               /* MINIBUF, MBUF, SBUF */
#include <uflib/standard_net_includes.h>                                       /* struct sockaddr_in */
#include <uflib/file_loader_type.h>                                            /* FileLoader */
#include "uflib/adt/adt_distinct_array_type.h"                                 /* DistinctArray */

typedef struct ec_private_key ec_private_key;
typedef struct ec_public_key ec_public_key;

#ifndef INCLUDE_UFSRVRESULT_TYPE_H_
struct UFSRVResult {
    void *result_user_data;//generic carrier of objects
    //char result_msg[MAX_UFSRV_RESULT_MSG_SIZE];
    unsigned result_code;//enum category into array of return message carefully categorized
    //unsigned result_code_idx; //index within a category
    unsigned result_type;//enum
};

typedef struct UFSRVResult UFSRVResult;
#endif

/* Forward declarations — types defined in ufnetcorelib (kept opaque: their
 * real headers, e.g. session_type.h, transitively include config.h, which
 * is unavailable in the generator compilation; the generated accessors only
 * cast pointers, so an incomplete type suffices).                            */
typedef struct SessionsDelegator        SessionsDelegator;
typedef struct InstanceHolder           InstanceHolder;             // opaque base (matches uflib)
typedef InstanceHolder                  InstanceHolderForSession;   // alias (matches session_type.h:70)

typedef struct CloudAuthorizationToken  CloudAuthorizationToken;
typedef struct PersistenceBackend      PersistenceBackend;
typedef PersistenceBackend             UserMessageCacheBackend;
typedef PersistenceBackend             FenceCacheBackend;
typedef struct MessageQueueBackend     MessageQueueBackend;

/* uflib logger handle — created and destroyed by the consumer, held here borrowed. */
typedef struct UfLogger                UfLogger;

/*!
 * OAuth 2.0 configuration — auto-discovery and protected-resource file loaders.
 * Originally: struct { bool is_enabled; FileLoader auto_discovery; FileLoader protected_resource; } oauth;
 */
typedef struct UfsrvConfigOauth {
    bool       is_enabled;
    FileLoader auto_discovery;
    FileLoader protected_resource;
} UfsrvConfigOauth;

#endif /* UFSRV_CORE_UFSRV_CONFIG_TYPES_H */
