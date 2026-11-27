#ifndef UFSRVCORELIB_SESSIONS_DELEGATOR_TYPE_H
#define UFSRVCORELIB_SESSIONS_DELEGATOR_TYPE_H

#include <pthread.h>
#include <stdatomic.h>

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <uflib/adt/adt_hashtable.h>
#include <uflib/adt/adt_queue.h>
#include <uflib/cdt/cdt_lamport_queue.h>

#include <ufsrv_core/delegator_sessions/console_delegator_channel_type.h>
#include "ratelimit_handle_type.h"
#include <ufsrv_core/delegator_sessions/workers_config_descriptor_type.h>
#include <ufsrv_core/instrumentation/instrumentation_backend.h>
#include <ufsrv_core/jobworkers/worker_job_descriptor_type.h>

/* Backend and recycler types stay in the libraries that define them. The
 * delegator holds pointers only, so none of them becomes a core dependency. */
typedef struct InstanceHolder   InstanceHolder;
typedef InstanceHolder          InstanceHolderForSession;
typedef struct PersistenceBackend PersistenceBackend;
typedef PersistenceBackend      UserMessageCacheBackend;
typedef PersistenceBackend      FenceCacheBackend;
typedef struct MessageQueueBackend MessageQueueBackend;

enum WorkerType
{
  WORKERTYPE_SESSIONWORKER = 1,
  WORKERTYPE_UFSRVWORKER
};

/*! Opaque type, representing user runtime context for delegator-instrumented worker threads */
typedef void UfsrvUserData;

typedef struct ThreadContext ThreadContext;

//basic context data passed to session worker threads at creation time
typedef struct WorkerThreadCreationContext
{
  size_t                    idx;
  UfsrvUserData            *user_data;
  InstanceHolderForSession *ipc_pipe;
  LocklessSpscQueue *       queue;
} WorkerThreadCreationContext;

/*! User provided worker runtime handles which are channelled on user's behalf to each worker's TLS */
typedef struct UfsrvDelegatorWorkersUserData
{
  UfsrvUserData *session_worker;  ///< Contiguous session-worker slots owned by the consumer.
  UfsrvUserData *job_worker;      ///< Contiguous job-worker slots owned by the consumer.
  size_t session_worker_count;   ///< Number of supplied session-worker slots.
  size_t job_worker_count;       ///< Number of supplied job-worker slots.
  size_t slot_size;              ///< Size of each opaque slot in bytes.
  PersistenceBackend *persistence_backend; ///< Dedicated connection for the delegator thread.
  void (*on_worker_start)(ThreadContext *); ///< Optional consumer hook after the TLS slot is set.
} UfsrvDelegatorWorkersUserData;

/*!
 * @brief One-off hooks a consumer registers before the delegator is brought up.
 *
 * Invoked once during bootstrap and not retained afterwards. They exist so the
 * library can ask its consumer to place the session tables without the library
 * knowing the session layout those tables index.
 */
typedef struct SessionsDelegatorInitSpecs
{
  void (*on_init_hashtable)(void); ///< Instantiates the delegator's session tables
  MessageQueueBackend *(*build_msgqueue_backend)(MessageQueueBackend *); ///< consumer builds the publisher backend
  const UfsrvDelegatorWorkersUserData *ufsrv_delegator_userdata; ///< Copied before workers start.
  void (*on_init_crypto)(void); ///< Initialises consumer credentials before core TLS startup.
  PersistenceBackend *(*on_init_main_thread_backends)(RateLimitHandle *); ///< Returns the main persistence connection and supplies the listener rate limiter.
  CallbackWorkExecutor msgqueue_executor;
} SessionsDelegatorInitSpecs;

struct SessionsDelegator
{
  int              epoll_handle;
  _Atomic unsigned up_status;
  int              maxfd;   ///> the highest file descriptor currently registered
  int              setsize; ///> max number of file descriptors tracked by the delegator

  //todo fixme doesn't belong in this domain. unauthenticated_idle=300, connected_idle=300, suspended_idle
  struct
  {
    unsigned unauthenticated, connected, suspended, locationless;
  } user_timeouts;

  struct
  {
    HashTable hashtable;
  } hashed_sessions;

  struct
  {
    HashTable hashtable;
  } hashed_cookies;

  struct
  {
    HashTable hashtable;
  } hashed_userids;

  struct
  {
    HashTable hashtable;
  } hashed_usernames;

  /**
   * A dedicated queue used between worker_delegator_thread and protocol_websocket_thread to register and fetch new connections requests.
   * Currently, the lockless queue is being trialled.
   */
  struct
  {
    LocklessSpscQueue queue;
  } new_connections;

  void *events_container; ///> epoll_event container memory initialised to max requested size upfront

  CallbackWorkExecutor msgqueue_executor;
  UfsrvDelegatorWorkersUserData ufsrv_delegator_userdata; ///> private user supplied data per worker thread

  /**
   * Backend handles for the exclusive use by the delegator. Opaque here: the
   * delegator never dereferences them, their owners do.
   */
  InstrumentationBackend *  instrumentation_backend_ptr; //used by non session worker threads
  UserMessageCacheBackend * usrmsg_cachebackend;         ///> redis usrmsg backend
  FenceCacheBackend *       fence_cachebackend;          ///> redis fence backend
  MessageQueueBackend *     msgqueue_pub_ptr;            ///> MessageQueue publisher not used by session workers.
  struct _h_connection *    db_backend_ptr;              ///> db access

  RateLimitHandle listener_rate_limit; ///< Used exclusively by the main listener.
  InstanceHolderForSession *listener_delegator_ipc;

  ///> reference to the session associated with pipe holding MainListener <-> delegator IPC, Assigned from the ufsrv.work_delegator_pipe
  ConsoleDelegatorChannel   console_channel;

  pthread_t      session_delegator_thread; ///> Delegator thread object. one per ufsrv instance
  pthread_attr_t th_attr;

  WorkersConfigDescriptor *jobworkers_config_descriptor; ///> Each ufsrv instance can register a workers pool

  pthread_t *         session_worker_ths; //workers spawned at once
  pthread_mutex_t     work_queue_mutex;
  //This is the mutex that control the broadcasting of work availability for session workers
  pthread_mutexattr_t work_queue_mutex_attr;

  pthread_barrier_t init_barrier;

  //queue/mutex not implemented yet, we read straight of the socket instead
  Queue worker_delegator_ipc_queue; ///> this is worker->delegator request queue signalling via self-pipe
  pthread_mutex_t worker_delegator_ipc_queue_mutex; ///> store/retrieve session work requests
  InstanceHolderForSession **worker_delegator_ipc;
  ///> this the set of delegator-worker ipc pipe (currently self-pipe implementation
  pthread_key_t worker_delegator_pipe_key;

  //these are for delegator->worker work raising signals
  unsigned session_io_hold; ///< 0 fans work out. 1 leaves submitted cells queued. Touched only under work_queue_mutex.
  unsigned listener_hold; ///< 0 installs queued accepts. 1 leaves them on new_connections. Delegator thread only.
  _Atomic unsigned long listener_refused; ///< Failed new_connections pushes. Written by the accepting thread only.
  _Atomic unsigned new_connections_pending; ///< 1 from a completed push until the next exchange; not a mirror of the ring.
  unsigned long listener_refused_at_pause; ///< listener_refused when pause was raised. Delegator thread only.
  unsigned long listener_refused_hold_delta; ///< Refusals during the last hold, frozen when it was cleared. Delegator thread only.
  pthread_cond_t queue_not_empty_cond;
  pthread_cond_t queue_not_full_cond;
  pthread_cond_t queue_empty_cond;
  //pthread_cond_t work_queue_cond;
  pthread_t      pid;
};

typedef struct SessionsDelegator SessionsDelegator;

/*!
 * @brief The process-wide sessions delegator.
 *
 * Replaces direct reads of the exported `sessions_delegator_ptr`; that symbol
 * remains during the migration and is retired in WP5.
 */
PUBLIC_API SessionsDelegator *SessionsDelegatorPtr(void);

//expects SessionDelegator *sessions_delegator_ptr
#define SESNDELEGATE_SESSIONCACHE(x)	&(x->hashed_sessions.hashtable)
#define SESNDELEGATE_USERIDCACHE(x)		&(x->hashed_userids.hashtable)
#define SESNDELEGATE_COOKIECACHE(x)		&(x->hashed_cookies.hashtable)
#define SESNDELEGATE_USERNAMECACHE(x)		&(x->hashed_usernames.hashtable)

#define THRKEY_WORKER_DELEGATOR_PIPE(x)	(x->worker_delegator_pipe_key)

#endif
