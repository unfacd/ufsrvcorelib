#ifndef UFSRVCORELIB_THREAD_CONTEXT_TYPE_H
#define UFSRVCORELIB_THREAD_CONTEXT_TYPE_H

#include <ufsrv_core/delegator_sessions/ratelimit_handle_type.h>
#include <ufsrv_core/instrumentation/instrumentation_backend.h>
#include <ufsrv_core/session/session_io_work_type.h>
#include <ufsrv_core/ufsrvresult_type.h>

#include "sessions_delegator_type.h"

/* Backend types stay in the libraries that define them; the context holds
 * pointers only, so none of them becomes a dependency of this header.       */
typedef struct HopscotchHashtableConfigurable HopscotchHashtableConfigurable;
typedef struct InstanceHolder                InstanceHolder;
typedef struct PersistenceBackend            PersistenceBackend;
typedef PersistenceBackend                   UserMessageCacheBackend;
typedef PersistenceBackend                   FenceCacheBackend;
typedef struct MessageQueueBackend           MessageQueueBackend;
typedef struct _h_connection                 DbBackend;
typedef struct HttpRequestContext            HttpRequestContext;

/**
 * Thread specific context variables that are uniquely instantiated for each thread and then multiplexed through "thread_local" storage scope.
 * Each thread can then refer to the thread_local reference by name and that will reference local instance of the ThreadContext.
 * This type is used with session workers only as a head type, unlike job workers, who use @ref BaseThreadContext
 */
typedef struct ThreadContext {
  unsigned int                    random_state; /** @brief thread specific random state */
  HopscotchHashtableConfigurable *ht_ptr;
  /* Compatibility fields used by services that have not switched to user_data. */
  PersistenceBackend             *persistence_backend;      ///< loaded from thread-specific data at service time
  InstrumentationBackend         *instrumentation_backend;  ///< loaded from thread-specific data at service time
  UserMessageCacheBackend        *usrmsg_cachebackend;
  FenceCacheBackend              *fence_cachebackend;
  MessageQueueBackend            *msgqueue_backend;         ///< msg queue publisher loaded from thread-specific data at service time
  DbBackend                      *db_backend;               ///< sql db handle
  UfsrvUserData                  *user_data;                ///< Consumer-owned runtime slot.
  UFSRVResult                    *res_ptr;                  ///< thread specific independent of Session's
  HttpRequestContext             *http_request_context;
  RateLimitHandle                 ratelimit_handle;
  InstanceHolder                **io_claim;
  SessionIoWork                  *io_claim_work;
} ThreadContext;

extern __thread ThreadContext ufsrv_thread_context;

#define THREAD_CONTEXT									(ufsrv_thread_context)
#define THREAD_CONTEXT_PTR							&(ufsrv_thread_context)
#define THREAD_CONTEXT_OBJECT_STORE(x) 	(x.ht_ptr)
#define THREAD_CONTEXT_UFSRV_RESULT_PTR	(THREAD_CONTEXT.res_ptr)
#define THREAD_CONTEXT_UFSRV_RESULT(x)	(x.res_ptr)
#define THREAD_CONTEXT_UFSRV_RESULT_TYPE(x)	(THREAD_CONTEXT_UFSRV_RESULT(x)->result_type)
#define THREAD_CONTEXT_UFSRV_RESULT_TYPE_	(THREAD_CONTEXT_UFSRV_RESULT(THREAD_CONTEXT)->result_type)
#define THREAD_CONTEXT_UFSRV_RESULT_USERDATA  (THREAD_CONTEXT_UFSRV_RESULT(THREAD_CONTEXT)->result_user_data)
#define THREAD_CONTEXT_UFSRV_RESULT_TYPE_EQUALS(x, y)	(THREAD_CONTEXT_UFSRV_RESULT(x)->result_type == (y))
#define THREAD_CONTEXT_UFSRV_RESULT_CODE(x)	(THREAD_CONTEXT_UFSRV_RESULT(x)->result_code)
#define THREAD_CONTEXT_UFSRV_RESULT_CODE_	(THREAD_CONTEXT_UFSRV_RESULT(THREAD_CONTEXT)->result_code)
#define THREAD_CONTEXT_UFSRV_RESULT_CODE_EQUAL(x, y)	(THREAD_CONTEXT_UFSRV_RESULT_CODE(x) == y)
#define THREAD_CONTEXT_UFSRV_RESULT_CODE_EQUAL_(x)	(THREAD_CONTEXT_UFSRV_RESULT_CODE_ == x)

#define THREAD_CONTEXT_UFSRV_RESULT_TYPE_SUCCESS	(THREAD_CONTEXT_UFSRV_RESULT_TYPE(THREAD_CONTEXT) == RESULT_TYPE_SUCCESS)
#define THREAD_CONTEXT_UFSRV_RESULT_IS_SUCCESS_WITH_BACKEND_DATA (THREAD_CONTEXT_UFSRV_RESULT_TYPE(THREAD_CONTEXT) == RESULT_TYPE_SUCCESS && THREAD_CONTEXT_UFSRV_RESULT_CODE(THREAD_CONTEXT) == RESCODE_BACKEND_DATA)
#define THREAD_CONTEXT_UFSRV_RESULT_IS_SUCCESS_WITH_EMPTYSET_DATA (THREAD_CONTEXT_UFSRV_RESULT_TYPE(THREAD_CONTEXT) == RESULT_TYPE_SUCCESS && THREAD_CONTEXT_UFSRV_RESULT_CODE(THREAD_CONTEXT) == RESCODE_BACKEND_DATA_EMPTYSET)
#define THREAD_CONTEXT_UFSRV_RESULT_IS_EMPTYSET_BACKEND_DATA ((THREAD_CONTEXT_UFSRV_RESULT_TYPE(THREAD_CONTEXT) == RESULT_TYPE_ERR || THREAD_CONTEXT_UFSRV_RESULT_TYPE(THREAD_CONTEXT) == RESULT_TYPE_SUCCESS) && THREAD_CONTEXT_UFSRV_RESULT_CODE(THREAD_CONTEXT) == RESCODE_BACKEND_DATA_EMPTYSET)
#define THREAD_CONTEXT_UFSRV_RESULT_TYPE_ERR	(THREAD_CONTEXT_UFSRV_RESULT_TYPE(THREAD_CONTEXT) == RESULT_TYPE_ERR)
#define THREAD_CONTEXT_DB_BACKEND		(THREAD_CONTEXT.db_backend)
#define THREAD_CONTEXT_PERSISTENCE_CACHEBACKEND(x)		(x.persistence_backend)

#define THREAD_CONTEXT_SESSION_CACHEBACKEND					(THREAD_CONTEXT.persistence_backend)
#define THREAD_CONTEXT_FENCE_CACHEBACKEND					(THREAD_CONTEXT.fence_cachebackend)
#define THREAD_CONTEXT_USRMSG_CACHEBACKEND					(THREAD_CONTEXT.usrmsg_cachebackend)
#define THREAD_CONTEXT_MSGQUEUE_CACHEBACKEND					(THREAD_CONTEXT.msgqueue_backend)
#define THREAD_CONTEXT_INSTRUMENTATION_BACKEND					(THREAD_CONTEXT.instrumentation_backend)
#define THREAD_CONTEXT_HTTP_REQUEST_CONTEXT					(THREAD_CONTEXT.http_request_context)
#define THREAD_CONTEXT_RATELIMIT_HANDLE						(THREAD_CONTEXT.ratelimit_handle)

#define THREAD_CONTEXT_RETURN_RESULT_SUCCESS(x, y)    \
{\
	THREAD_CONTEXT.res_ptr->result_user_data = (void *)x;\
	THREAD_CONTEXT.res_ptr->result_type = RESULT_TYPE_SUCCESS;\
	THREAD_CONTEXT.res_ptr->result_code = y;\
	return THREAD_CONTEXT.res_ptr;\
}

#define THREAD_CONTEXT_RETURN_RESULT_ERROR(x, y)    \
{\
	THREAD_CONTEXT.res_ptr->result_user_data=(void *)x;\
	THREAD_CONTEXT.res_ptr->result_type=RESULT_TYPE_ERR;\
	THREAD_CONTEXT.res_ptr->result_code=y;\
	return THREAD_CONTEXT.res_ptr;\
}

#define THREAD_CONTEXT_LOCAL_RETURN_RESULT_SUCCESS(x, y, z)    \
{\
	x->result_user_data = (void *)y;\
	x->result_type = RESULT_TYPE_SUCCESS;\
	x->result_code = z;\
	return x;\
}

#endif
