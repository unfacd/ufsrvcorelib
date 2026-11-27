#ifndef UFSRVCORELIB_BASE_THREAD_CONTEXT_DATA_TYPE_H
#define UFSRVCORELIB_BASE_THREAD_CONTEXT_DATA_TYPE_H

#include <uflib/adt/adt_hopscotch_hashtable_type.h>
#include <uflib/scheduled_jobs/scheduled_jobs_type.h>

#include <ufsrv_core/delegator_sessions/http_request_context_type.h>
#include <ufsrv_core/ufsrvresult_type.h>

/* The pool descriptor is held by pointer; its definition stays with the pool. */
typedef struct WorkerPoolDescriptor WorkerPoolDescriptor;
typedef void ThreadContextData; //opaque type for use by interface users

/**
 * Foundational data context object individually allocated to each Ufsrv worker thread (job-worker and session-worker) at thread instantiation time.
 * This was originally conceived for Sfu, and it is meant to replace the old ThreadContext, but the transition is not complete.
 *
 * BaseThreadContext                     ← common to ALL worker types
 *   ├─ embedded: locked_objects_store, http_request_context, ufsrv_result,
 *   │            scheduled_jobs_store, pool_descriptor, events_handle, thread_idx
 *   └─ .worker_thread_context ─────▶ ThreadContext     ← session-worker layer
 *                             ─────▶ ThreadContextSfu  ← SFU layer
 */
typedef struct BaseThreadContext {
  unsigned int                    random_state; /** @brief thread specific random state */
  size_t                          thread_idx; /** @brief fixed serial index number for identifying thread */
  int                             events_handle; /** @brief epoll fd */
  ThreadContextData               *worker_thread_context; /** @brief thread worker specific run time context. Currently alias for ThreadContext data type */
  void                            *user_data; ///< Consumer-owned runtime slot.
  WorkerPoolDescriptor            *pool_descriptor; /** @brief metadata specs for the threads pool, enabling inter-thread signalling */

  HopscotchHashtableConfigurable  locked_objects_store; /** @brief objects whose mutex lock is owned by this thread */
  HttpRequestContext              http_request_context; /** @brief pre-allocated and request-ready http requestor */
  UFSRVResult                     ufsrv_result; /** @brief thread-local result object */
  ScheduledJobs                   scheduled_jobs_store; /** @brief thread specific scheduled jobs, mainly timeout runs */
} BaseThreadContext;

/* One per worker thread; session workers keep it alongside ThreadContext. */
extern __thread BaseThreadContext *base_thread_context;

#endif
