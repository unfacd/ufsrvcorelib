/*

 Copyright (c) 2015-2026 unfacd works

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

/**
 * This is the main I/O event listener/dispatcher thread, delegating work to session based worker threads.
 */
#include "ufsrv_core/delegator_sessions/thread_context_type.h"
#include "ufsrv_core/session/ufsrv_session_type.h"
#ifdef HAVE_CONFIG_UFNETCORELIB_H
# include "config_ufnetcorelib.h"
#endif

#include <recycler/recycler.h>
#include <limits.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <ufsrv_core/delegator_sessions/session_io_work.h>
#include "ufsrv_core/session/sessions_utils.h"
#include <uflib/utils_threads.h>
#include "ufsrv_core/protocol/protocol.h"
#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend.h>
#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>
#include <ufsrv_core/delegator_sessions/delegator_session_worker_thread.h>
#include <ufsrv_core/delegator_sessions/workers/sessionworkers_utils.h>
#include <uflib/utils_time.h>
#include "ufsrv_core/delegator_sessions/base_thread_context_data_type.h"
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_shim.h>
#include <ufsrv_core/delegator_sessions/command/session_delegator_command.h>
#include <ufsrv_core/delegator_sessions/command/session_delegator_command_handler.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>

// #include "utils_cpu.h" //todo fixme this is cannot be pulled in here, because it breaks dependency inversion rules: long term fix: this module needs to be in ufsrcorelib.

static void *sThreadWorkerDelegator(void *session_delegator);
static int sAddWorkEvent(SessionsDelegator *sd_ptr, InstanceHolderForSession *instance_sesn_ptr);
static int sRemoveWorkEvent(SessionsDelegator *sd_ptr, InstanceHolderForSession *instance_sesn_ptr);
static int RearmSessionInMonitoredWorkEvents(SessionsDelegator *sd_ptr, InstanceHolderForSession *instance_sesn_ptr) __attribute__((unused));

static void sAddListerToDelegatorPipeConnectionToMonitoredEvents(SessionsDelegator *sd_ptr, InstanceHolderForSession* work_delegator_type);
static void sAddConsoleToDelegatorPipe(SessionsDelegator *sd_ptr);
static void sReadListenerPipe(UfsrvSession *sesn_ptr_ipc);
static void sDequeueNewConnections(SessionsDelegator *sd_ptr);

/* The delegator object itself now lives in ufsrvcorelib; this file still
 * names it directly, which the accessor preserves. */
#define sessions_delegator (*SessionsDelegatorPtr())

//fixme the pointer sessions_delegator_ptr is widely shared around in within the codebase as an 'extern' reference, causing havoc. Access needs to be restricted.

//todo: use _Thread_local.
//important C11, if a variable is declared with _Thread_local, every extern declaration in other TUs must also include the _Thread_local keyword
//In C11, if a variable is declared with _Thread_local, every extern declaration in other TUs must also include the _Thread_local keyword.
//In order separate declaration from actual definition (this file is teh site of that), if using _Thread_local, you must also initialise these variables.
//extern _Thread_local ufsrv_thread_context = {0}; extern _Thread_local base_thread_context = NULL;


//todo all implementations should move over to this generic pointer implementation, which each protocol type defines
//for itself and with pre-allocated storage. That storage should be pointed at this thread-local pointer for each thread.
//__thread ThreadContextData *ufsrv_thread_context_data;

static const char *sEpollControlError(int error);

int
LaunchSessionsDelegatorThread(SessionsDelegator *sd_ptr)
{
	syslog(LOG_INFO, ">> %s: Creating Sessions Delegator thread...", __func__);
	pthread_create(&sd_ptr->session_delegator_thread, NULL, sThreadWorkerDelegator, sd_ptr);

	return 1;

}

/**
 * @brief Load values currently in the config backing store, which maybe defaults, or configfile sourced.
 */
static void
sSessionDelegatorBootstrapConfigValue(void)
{
	sessions_delegator.setsize = UfsrvConfigSessionWorkersThreadPool();
	// sessions_delegator.session_workers_thread_pool.setsize = = UfsrvConfigSessionWorkersThreadPool(); //in sfu style SessionDelegator

	sessions_delegator.user_timeouts.connected				= UfsrvConfigUserTimeoutsConnectedTimeout();
	sessions_delegator.user_timeouts.unauthenticated	=	UfsrvConfigUserTimeoutsUnauthenticatedTimeout();
	sessions_delegator.user_timeouts.suspended				= UfsrvConfigUserTimeoutsSuspendedTimeout();
	sessions_delegator.user_timeouts.locationless			= 300;//UfsrvConfigUserTimeoutLocationless();
}

/**
 * @brief Called early in the ufsrv bootstrapping sequence. Some configuration values are loaded earlier through
 * config file parsing stage, eg "session_workers_thread_pool", feeding into sessions_delegator.setsize. This will need to change.
 * The delegator has another ipc channel with the main listener.
 * todo revise description below for accuracy
 * This facet of Delegator initialisation sequence sets up the delegator's dedicated connection handles to some network-deployed
 * data repositories (db, redis etc). Additionally, it configures the I/O worker threads through the work_queue work_queue is sourced from poll_events.
 * each worker deque and services the request based on Session referenced in the individual queue entries.
 * all worker threads block on cond work_queue->nEntries==0
 * @param ufsrv_type_ptr opaque type representing the singleton ufsrv structure.
 * @return
 */
SessionsDelegator *
InitialiseDelegator(SessionsDelegatorInitSpecs *spec)
{
  size_t session_count = UfsrvConfigSessionWorkersThreadPool();
  size_t job_count = UfsrvConfigUfsrvWorkersThreadPool();
  const UfsrvDelegatorWorkersUserData *data = spec ? spec->ufsrv_delegator_userdata : NULL;
  if (!data || !data->slot_size || !data->persistence_backend ||
      !session_count || session_count >= INT_MAX ||
      data->session_worker_count != session_count || !data->session_worker ||
      data->job_worker_count != job_count || (job_count && !data->job_worker) ||
      session_count > SIZE_MAX / data->slot_size || job_count > SIZE_MAX / data->slot_size) {
    syslog(LOG_ERR, "%s: Invalid worker backend slots for the configured thread pools", __func__);
    return NULL;
  }

	sSessionDelegatorBootstrapConfigValue();

	sessions_delegator.events_container = malloc(sizeof(struct epoll_event) * sessions_delegator.setsize);// fixme messily assigned value "session_workers_thread_pool" read during configfile processing

  sessions_delegator.epoll_handle = epoll_create1(0);
  if (sessions_delegator.epoll_handle == -1) {
    syslog(LOG_ERR, "%s: ERROR: COULD NOT CREATE EPOLL HANDLE: EXITING", __func__ );
    return NULL;
  }

  pthread_barrier_init(&sessions_delegator.init_barrier, NULL, sessions_delegator.setsize + 1); //extra for delegator to reach the same barrier

	{//mutexes and cond vars
		//TOO: MIGRATE TO THE USE OF THE VARS defined below in ufsrv_thread_pool
		pthread_mutexattr_init(&sessions_delegator.work_queue_mutex_attr);
		pthread_mutexattr_settype(&sessions_delegator.work_queue_mutex_attr,  PTHREAD_MUTEX_ADAPTIVE_NP);//PTHREAD_MUTEX_ERRORCHECK);//Opportunistic "spinklock"
    int result = pthread_mutex_init (&sessions_delegator.work_queue_mutex, &sessions_delegator.work_queue_mutex_attr);

		result = pthread_cond_init(&sessions_delegator.queue_not_empty_cond, NULL);
		result = pthread_cond_init(&sessions_delegator.queue_not_full_cond, NULL);
		result = pthread_cond_init(&sessions_delegator.queue_empty_cond, NULL);

		if (result != 0) {
			char error_str[MEDIUMBUF];
			strerror_r(errno, error_str, MEDIUMBUF);
			syslog(LOG_ERR, "InitialiseWorkerDelegator: TERMINATING (errno: '%d'): COULD NOT INITIALISE mutex and cond vars: error: '%s'...", errno, error_str);

      return NULL;
		}

    pthread_key_create(&(SessionsDelegatorPtr()->worker_delegator_pipe_key), NULL);//TODO: no clean up callback

		syslog(LOG_INFO, ">> %s: SUCCESSFULLY Initialised mutex and cond vars", __func__);
	}

	{
		//initialise aux instrumentation backend object for non-session worker use
		syslog(LOG_INFO, "InitialiseWorkerDelegator: Initialising Auxiliary Instrumentation Backend for Session Delegator thread");
    InstrumentationBackend *instr_ptr = InstrumentationBackendInit(NULL, NULL, UfsrvConfigServerClass(), UfsrvConfigServerInstanceId());//no namespace. last two args will be ignored
		if (IS_PRESENT(instr_ptr)) {
			sessions_delegator.instrumentation_backend_ptr = instr_ptr;
		} else {
			syslog(LOG_INFO, "InitialiseWorkerDelegator: ERROR: COULD NOT INITIALISE INSTRUMENTATION for Session Delegator thread");
			sessions_delegator.instrumentation_backend_ptr = NULL;
		}
	}

  if (ProtocolRegistryPublishesMessages()) {
		//initialise aux MessgeQueueBackend object for non-session worker use
		syslog(LOG_INFO, "%s: Initialising MessageQueue Backend Publisher Semantics for Session Delegator thread...", __func__);
    MessageQueueBackend *msgq_ptr = NULL;
    if (spec != NULL && spec->build_msgqueue_backend != NULL) msgq_ptr = spec->build_msgqueue_backend(NULL);
		if (IS_PRESENT(msgq_ptr)) {
			sessions_delegator.msgqueue_pub_ptr = msgq_ptr;
		} else {
			syslog(LOG_INFO, "%s: ERROR: COULD NOT INITIALISE Auxiliary MessageQueueBackend for Session Delegator thread", __func__);
			sessions_delegator.msgqueue_pub_ptr = NULL;
		}
	}

  sessions_delegator.ufsrv_delegator_userdata = *data;
  sessions_delegator.msgqueue_executor = spec->msgqueue_executor;
  LaunchSessionWorkerThreads(&sessions_delegator, sizeof(ThreadContext));

  return &sessions_delegator;

}

void
RegisterJobWorkersConfigurationDescriptor(WorkersConfigDescriptor *workers_config_descriptor)
{
  sessions_delegator.jobworkers_config_descriptor = workers_config_descriptor;
}

__pure WorkersConfigDescriptor *
GetJobWorkersConfigurationDescriptor()
{
  return sessions_delegator.jobworkers_config_descriptor;
}

/**
 * @brief Initiate a graceful shutdown of the worker pools.
 *
 * Sets the delegator's up_status to 0 (session workers observe this and exit
 * after their current unit of work) and, when a ufsrv worker pool is
 * registered, its pool state to POOL_STATE_DOWN.
 *
 * Both stores are published and their condition variables broadcast while
 * holding the corresponding work queue mutex.  Workers evaluate the status as
 * part of the predicate that guards pthread_cond_wait() on that same mutex, so
 * doing the two under the lock is what prevents a broadcast from being lost
 * between a worker's final status read and its going to sleep.
 */
void
UfsrvInitiateShutdown(void)
{
  pthread_mutex_lock(&sessions_delegator.work_queue_mutex);
  sessions_delegator.up_status = 0;
  pthread_cond_broadcast(&sessions_delegator.queue_not_empty_cond);
  pthread_mutex_unlock(&sessions_delegator.work_queue_mutex);

  WorkersConfigDescriptor *jobworkers = sessions_delegator.jobworkers_config_descriptor;
  if (IS_PRESENT(jobworkers)) {
    pthread_mutex_lock(&jobworkers->work_queue_mutex);
    jobworkers->up_status = POOL_STATE_DOWN;
    pthread_cond_broadcast(&jobworkers->queue_not_empty_cond);
    pthread_mutex_unlock(&jobworkers->work_queue_mutex);
  }
}

size_t
UfsrvGetSessionWorkersSize(void)
{
  return sessions_delegator.setsize;
}

unsigned
UfsrvSessionWorkersPoolState(void)
{
  return atomic_load(&sessions_delegator.up_status);
}

unsigned
SessionDelegatorSessionIoHold(void)
{
  unsigned hold;

  pthread_mutex_lock(&sessions_delegator.work_queue_mutex);
  hold = sessions_delegator.session_io_hold;
  pthread_mutex_unlock(&sessions_delegator.work_queue_mutex);
  return hold;
}

void
SessionDelegatorSetSessionIoHold(unsigned hold)
{
  pthread_mutex_lock(&sessions_delegator.work_queue_mutex);
  sessions_delegator.session_io_hold = hold ? 1u : 0u;
  if (!sessions_delegator.session_io_hold) {
    pthread_cond_broadcast(&sessions_delegator.queue_not_empty_cond);
  }
  pthread_mutex_unlock(&sessions_delegator.work_queue_mutex);
}

unsigned
SessionDelegatorListenerHold(void)
{
  if (!pthread_equal(pthread_self(), sessions_delegator.pid)) {
    syslog(LOG_ERR, "%s: refused off the delegator thread", __func__);
    return 0;
  }

  return sessions_delegator.listener_hold;
}

size_t
SessionDelegatorIncomingConnectionsQueueCapacity(void)
{
  size_t slots = (size_t)sessions_delegator.new_connections.queue.queue_sz;

  return slots ? slots - 1 : 0;
}

unsigned long
SessionDelegatorListenerRefused(void)
{
  return atomic_load_explicit(&sessions_delegator.listener_refused, memory_order_relaxed);
}

unsigned long
SessionDelegatorListenerRefusedDuringHold(void)
{
  if (!pthread_equal(pthread_self(), sessions_delegator.pid)) {
    syslog(LOG_ERR, "%s: refused off the delegator thread", __func__);
    return 0;
  }

  if (sessions_delegator.listener_hold) {
    return SessionDelegatorListenerRefused() - sessions_delegator.listener_refused_at_pause;
  }

  return sessions_delegator.listener_refused_hold_delta;
}

void
SessionDelegatorSetListenerHold(unsigned hold)
{
  UfsrvSession *pipe;

  if (!pthread_equal(pthread_self(), sessions_delegator.pid)) {
    syslog(LOG_ERR, "%s: refused off the delegator thread", __func__);
    return;
  }

  if (hold) {
    if (!sessions_delegator.listener_hold) {
      sessions_delegator.listener_hold = 1;
      sessions_delegator.listener_refused_at_pause = SessionDelegatorListenerRefused();
      sessions_delegator.listener_refused_hold_delta = 0;
    }
    return;
  }

  if (sessions_delegator.listener_hold) {
    sessions_delegator.listener_refused_hold_delta =
        SessionDelegatorListenerRefused() - sessions_delegator.listener_refused_at_pause;
  }
  sessions_delegator.listener_hold = 0;

  sDequeueNewConnections(&sessions_delegator);
  pipe = sessions_delegator.listener_delegator_ipc ? ((UfsrvSession *)GetInstance(sessions_delegator.listener_delegator_ipc)) : NULL;
  sReadListenerPipe(pipe);
  sDequeueNewConnections(&sessions_delegator);
}

/**
 *  Edge triggered: notification is on state of readiness event, not state of buffer ie whether it has has unread data in it or not. The latter is how level triggered works.
 *
 *  another way to put it:  transition from empty to non-empty for reads and from full to not-full for writes.
 *  Thus, to enable the next trigger, the buffers first have to be driven to empty on read/full on write (i.e. EAGAIN).
 *
 *  for listening sockets see http://stackoverflow.com/questions/14221339/epoll-wait-on-a-listener-socket-and-spurious-failures
 *
 */
static int
sAddWorkEvent(SessionsDelegator *sd_ptr, InstanceHolderForSession *instance_sesn_ptr)
{
  UfsrvSession *session = ((UfsrvSession *)GetInstance(instance_sesn_ptr));
  struct epoll_event event = {.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP};
  SessionIoWork identity = {.session = instance_sesn_ptr, .session_id = (session)->session_id,
                             .fd = (session)->ssptr->sock};
  void *retired = NULL;
  if (WorkQueueLock(sd_ptr, 0) != 0) return 0;
  event.data.u64 = SessionIoRegister(&session_io_queue, &session->io_work, &identity, &retired);
  int added = 0;
  if (event.data.u64) {
    added = epoll_ctl(sd_ptr->epoll_handle, EPOLL_CTL_ADD, identity.fd, &event) == 0;
    if (!added) {
      int error = errno;
      SessionIoUnregister(&session->io_work);
      syslog(LOG_ERR, "%s: could not register session fd=%d generation=%llu: %s", __func__, identity.fd,
             (unsigned long long)event.data.u64, sEpollControlError(error));
    }
  } else syslog(LOG_ERR, "%s: could not allocate socket generation for fd=%d", __func__, identity.fd);
  WorkQueueUnLock(sd_ptr);
	if (retired)	UfsrvSessionUnreference(retired, 1);
#ifdef __UF_FULLDEBUG
  if (added) syslog(LOG_DEBUG, "%s (pid:'%lu' o:'%p' cid:'%lu' fd:'%d' generation:'%llu'): Added monitored event",
    __func__, pthread_self(), session, identity.session_id, identity.fd, (unsigned long long)event.data.u64);
#endif
  return added;
}

static const char *
sEpollControlError(int error)
	{
		switch (error) {
			case EEXIST:
				return ("'EEXIST' the supplied file descriptor fd is already registered with this epoll instance");
			case EBADF:
        return("'EBADF' error: invalid fd'");
			case EINVAL:
        return("'EINVAL' fd is the same a epfd, or the requested operation op is not supported by this interface'");
			case ENOENT:
        return("'ENOENT' 'fd is not registered with this epoll instance'");
			case ENOMEM:
        return("'ENOMEM' 'NOT ENOUGH MEMORY'");
			case ENOSPC:
        return("'ENOSPC' 'error: imit imposed by /proc/sys/fs/epoll/max_user_watches was encountered while trying to register'");
			default:
				return("ERROR NOT REGISTERED");
		}
	}

__unused static int
RearmSessionInMonitoredWorkEvents(SessionsDelegator *sd_ptr, InstanceHolderForSession *instance_sesn_ptr)
{
  UfsrvSession *session = ((UfsrvSession *)GetInstance(instance_sesn_ptr));
  struct epoll_event event = {.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT};
  if (WorkQueueLock(sd_ptr, 0) != 0) return 0;
  event.data.u64 = SessionIoGeneration(&session->io_work);
  int rearmed = event.data.u64 &&
    epoll_ctl(sd_ptr->epoll_handle, EPOLL_CTL_MOD, session->io_work.registration.identity.fd, &event) == 0;
  if (!rearmed) syslog(LOG_ERR, "%s: could not rearm fd=%d generation=%llu", __func__,
    session->io_work.registration.identity.fd, (unsigned long long)event.data.u64);
  WorkQueueUnLock(sd_ptr);
  return rearmed;
}

int
AddSessionToMonitoredWorkEvents(InstanceHolderForSession *instance_sesn_ptr)
{
  return sAddWorkEvent(SessionsDelegatorPtr(), instance_sesn_ptr);

}

int
RemoveSessionToMonitoredWorkEvents(InstanceHolderForSession *instance_sesn_ptr)
{
		return sRemoveWorkEvent(SessionsDelegatorPtr(), instance_sesn_ptr);
}

static int
sRemoveWorkEvent(SessionsDelegator *sd_ptr, InstanceHolderForSession *instance_sesn_ptr)
{
  UfsrvSession *session = ((UfsrvSession *)GetInstance(instance_sesn_ptr));
  if (WorkQueueLock(sd_ptr, 0) != 0) return 0;
  int removed = 1;
  if (SessionIoGeneration(&session->io_work)) {
    int fd = session->io_work.registration.identity.fd;
    uint64_t generation = SessionIoGeneration(&session->io_work);
    SessionIoUnregister(&session->io_work);
    if (epoll_ctl(sd_ptr->epoll_handle, EPOLL_CTL_DEL, fd, NULL) != 0 && errno != ENOENT && errno != EBADF) {
      removed = 0;
      syslog(LOG_ERR, "%s: could not remove fd=%d generation=%llu: %s", __func__, fd,
        (unsigned long long)generation, sEpollControlError(errno));
    }
#ifdef __UF_FULLDEBUG
    if (removed) syslog(LOG_DEBUG, "%s (pid:'%lu' fd:'%d' generation:'%llu'): Removed monitored event",
      __func__, pthread_self(), fd, (unsigned long long)generation);
#endif
  }
  WorkQueueUnLock(sd_ptr);
  return removed;
}

inline int
WorkQueueLock(SessionsDelegator *sd_ptr, int try_flag)
{
	int lock_state;

	if (try_flag) {
		lock_state = pthread_mutex_trylock(&(sd_ptr->work_queue_mutex));
		if (lock_state == 0) {
#ifdef __UF_FULLDEBUG
			syslog(LOG_DEBUG, "%s (pid='%lu' lock:10:1 ): SUCCESS: ACQUIRED TRY-LOCK", __func__, pthread_self());
#endif
		}

		else {
			char *err_str = thread_error(errno);
			syslog(LOG_DEBUG, "%s (pid:'%lu'): ERROR: COULD NOT ACQUIRE TRY-LOCK (errno='%d'): '%s'", __func__, pthread_self(), errno, err_str);
		}
	} else {
		lock_state = pthread_mutex_lock(&(sd_ptr->work_queue_mutex));
		if (lock_state == 0) {
#ifdef __UF_FULLDEBUG
			syslog(LOG_DEBUG, "%s (pid='%lu' lock:30:1 ): SUCCESS: ACQUIRED LOCK", __func__, pthread_self());
#endif
		} else {
			char *err_str = thread_error(errno);
			syslog(LOG_DEBUG, "%s (pid:'%lu'): ERROR: COULD NOT ACQUIRE LOCK (errno='%d'): '%s'", __func__, pthread_self(), errno, err_str);
		}
	}

	return lock_state;

}

inline int
WorkQueueUnLock(SessionsDelegator *sd_ptr)
{
	int lock_state = pthread_mutex_unlock(&(sd_ptr->work_queue_mutex));

	if (lock_state == 0) {
#ifdef __UF_FULLDEBUG
		syslog(LOG_DEBUG, "%s: (pid='%lu' lock:30:-1 ): SUCCESS: RELEASED LOCK", __func__, pthread_self());
#endif
	} else {
		char *err_str = thread_error(errno);
		syslog(LOG_WARNING, "%s: (pid='%lu' lock:30:0 ): ERROR: COULD NOT RELEASE LOCK (errno='%d'): '%s'", __func__, pthread_self(),  errno, err_str);
	}

	return lock_state;

}

static void
sAddListerToDelegatorPipeConnectionToMonitoredEvents(SessionsDelegator *sd_ptr, InstanceHolderForSession *work_delegator_pipe)
{
	if (work_delegator_pipe) {
		struct epoll_event epoll_event = {0};
		epoll_event.events = EPOLLIN|EPOLLET;
		epoll_event.data.u64 = 0;

    UfsrvSession *sesn_ptr_pipe = ((UfsrvSession *)GetInstance(work_delegator_pipe));//WORK_DELEGATOR_PIPE_SESSION;
		if (epoll_ctl(sd_ptr->epoll_handle, EPOLL_CTL_ADD, sesn_ptr_pipe->dsptr->sock, &epoll_event) == -1) {//PIPE READER END
			syslog(LOG_ERR, "%s: COULD NOT add pipe connection to events loop: WILL NOT be able to process new connections: errno='%d'", __func__, errno);
		} else {
			syslog(LOG_ERR, "%s: SUCCESSFULLY added pipe connection to monitored events...", __func__);
		}
	} else {
		syslog(LOG_ERR, "%s: FATAL: masterptr->work_delegator_pipe is NOT SET: WILL NOT be able to process new connections", __func__);
	}

}

static void
sExecuteConsoleDelegatorCommand(const char *method, const char *path, BufferDescriptor *reply)
{
	SessionDelegatorCommandHandlerExecute(method, path, reply);
}

static void
sAddConsoleToDelegatorPipe(SessionsDelegator *sd_ptr)
{
	int fd = ConsoleDelegatorChannelReadFd(&sd_ptr->console_channel);
	struct epoll_event epoll_event = {0};

	if (fd < 0) {
		syslog(LOG_ERR, "%s: command console delegator pipe is not open", __func__);
		return;
	}
	epoll_event.events = EPOLLIN | EPOLLET;
	epoll_event.data.u64 = CONSOLE_DELEGATOR_EPOLL_COOKIE;
	if (epoll_ctl(sd_ptr->epoll_handle, EPOLL_CTL_ADD, fd, &epoll_event) == -1) {
		syslog(LOG_ERR, "%s: COULD NOT add command console pipe to events loop errno='%d'", __func__, errno);
	}
}

static void
sReadListenerPipe(UfsrvSession *sesn_ptr_ipc)
{
	char buf[256];
	int fd;

	if (!sesn_ptr_ipc || !sesn_ptr_ipc->dsptr) return;
	fd = sesn_ptr_ipc->dsptr->sock;
	if (fd < 0) return;
	for (;;) {
		ssize_t n = read(fd, buf, sizeof buf);
		if (n > 0) continue;
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
		syslog(LOG_ERR, "%s {pid:'%lu', cid_pipe:'%lu', errno:'%d', rc:'%ld'}: ERROR NewConnectionsDelegatorPipe: COULD NOT READ PIPE",
		       __func__, pthread_self(), (sesn_ptr_ipc)->session_id, errno, (long)n);
		return;
	}
}

static void
sDequeueNewConnections(SessionsDelegator *sd_ptr)
{
	LocklessSpscQueue *queue = &sd_ptr->new_connections.queue;
	QueueClientData *client_data_ptr = NULL;
	long long timer_start = GetTimeNowInMicros();

	while (LamportQueuePop(queue, &client_data_ptr)) {
		InstanceHolderForSession *instance_sesn_ptr_new = client_data_ptr;
		UfsrvSession *sesn_ptr_new = ((UfsrvSession *)GetInstance(instance_sesn_ptr_new));

		sesn_ptr_new->when_serviced_end = time(NULL);
		if (sAddWorkEvent(sd_ptr, instance_sesn_ptr_new)) {
#ifdef __UF_TESTING
			syslog(LOG_DEBUG, LOGSTR_WDELEG_NEWCONNECTION_ADDED, __func__, pthread_self(), client_data_ptr, (sesn_ptr_new)->session_id, LOGCODE_WDELEG_NEWCONNECTION_ADDED);
#endif
		} else {
			syslog(LOG_ERR, "%s (pid:'%lu', o:'%p', pid:'%lu'): ERROR: COULD NOT ADD new event... DROPPING PACKET and SUSPENDING into RECYCLER...", __func__, pthread_self(), sesn_ptr_new, (sesn_ptr_new)->session_id);
			close(sesn_ptr_new->ssptr->sock);
			UfsrvSessionReturnToRecycler(instance_sesn_ptr_new, NULL, CALL_FLAG_HASH_SESSION_LOCALLY);
		}
	}

	statsd_timing(sd_ptr->instrumentation_backend_ptr, "delegator.new_connections.dequeue.elapsed_time", GetTimeNowInMicros() - timer_start, UfsrvConfigInstrumentationBackendServerAsPtr());
}

PUBLIC_API void
InitNewConnectionsQueue(QueueClientData *listener_connection_queue, size_t queue_sz)
{
	syslog(LOG_INFO, ">> %s: New Connections Queue: Initialising lockless queue...", __func__);
	LamportQueueInit(&(sessions_delegator.new_connections.queue), listener_connection_queue, queue_sz);

	// statsd_gauge(UfsrvConfigInstrumentationBackend(), "listener.connection.queue_capacity", queue_sz, UfsrvConfigInstrumentationBackendServerAsPtr());

}

PUBLIC_API HashTable *
SessionDelegatorHashedSessionsTable()
{
	return &(sessions_delegator.hashed_sessions.hashtable);
}

PUBLIC_API MessageQueueBackend *
SessionsDelegatorMessageQueueBackend(void)
{
  return SessionsDelegatorPtr()->msgqueue_pub_ptr;
}

PUBLIC_API void
SessionsDelegatorInitialiseHashtable(HashTable *hash_table, HashtableSpecs *spec)
{
	if (hash_table == NULL || spec == NULL) {
		syslog(LOG_ERR, "%s: ERROR: missing %s", __func__, hash_table == NULL ? "table storage" : "table spec");
		return;
	}

	if (HashTableLockingInstantiateWithSpecs(hash_table, spec) == NULL) {
		syslog(LOG_ERR, "%s: ERROR: COULD NOT INITIALISE HashTable '%s'", __func__, spec->table_name ? spec->table_name : "AnonymousTable");
	}
}

#if 1

PUBLIC_API bool
SessionDelegatorPushIncomingConnection(QueueClientData *instance_sesn_ptr)
{
	if (LamportQueuePush(&(sessions_delegator.new_connections.queue), instance_sesn_ptr)) {
		atomic_store_explicit(&sessions_delegator.new_connections_pending, 1, memory_order_release);
		return true;
	}

	atomic_fetch_add_explicit(&sessions_delegator.listener_refused, 1, memory_order_relaxed);
	return false;
}

PUBLIC_API size_t
SessionDelegatorIncomingConnectionsQueueSize()
{
	return LamportQueueLeasedSize(&(sessions_delegator.new_connections.queue));
}

static void
sRecycleQueuedConnections(SessionsDelegator *sd_ptr)
{
  QueueClientData *client_data_ptr = NULL;

  while (LamportQueuePop(&sd_ptr->new_connections.queue, &client_data_ptr)) {
    InstanceHolderForSession *instance_sesn_ptr = client_data_ptr;
    UfsrvSession *sesn_ptr = ((UfsrvSession *)GetInstance(instance_sesn_ptr));

    close(sesn_ptr->ssptr->sock);
    UfsrvSessionReturnToRecycler(instance_sesn_ptr, NULL, CALL_FLAG_HASH_SESSION_LOCALLY);
  }
}

static void * __attribute__ (( no_sanitize_thread, noreturn ))
sThreadWorkerDelegator(void *session_delegator)
{
	SessionsDelegator *sd_ptr = session_delegator;

  SetThreadName("ufWorkDelegatr");
  UfsrvCpuAffinityPin(UFSRV_THREAD_DELEGATOR, 0);
  sessions_delegator.pid = pthread_self();
  ufsrv_thread_context.persistence_backend = sd_ptr->ufsrv_delegator_userdata.persistence_backend;

  unsigned long 		stat_atomic = 0L;


  sAddListerToDelegatorPipeConnectionToMonitoredEvents(sd_ptr, sd_ptr->listener_delegator_ipc);
  if (ConsoleDelegatorChannelOpen(&sd_ptr->console_channel, sExecuteConsoleDelegatorCommand) != 0) {
    syslog(LOG_ERR, "%s: command console delegator channel failed", __func__);
  }
  if (SessionDelegatorCommandHandlerInit() != 0) {
    syslog(LOG_ERR, "%s: command console delegator route table unavailable", __func__);
  }
  sAddConsoleToDelegatorPipe(sd_ptr);

  sessions_delegator.up_status = 1;
  syslog(LOG_INFO, "%s (pid:'%lu'): SUCCESS Reached delegator initialisation barrier: waiting for other worker threads...", __func__, pthread_self());
  pthread_barrier_wait(&sessions_delegator.init_barrier);

  //new connections queue processing block todo: use elsewhere defined _PollConnectionsListeners()
  //main event listener
  while (1 != 2) {
    size_t 	broadcast_work = 0;
    InstanceHolderForSession *instance_sesn_ptr;
    UfsrvSession *sesn_ptr;

#ifdef __UF_FULLDEBUG
    syslog(LOG_DEBUG, "sThreadWorkerDelegator: Blocking on I/O events: epoll_wait...");
#endif

    broadcast_work = 0;

    //>>>>>>>>>>>>>>>>>>>>>>>>
    int ready_events_count = epoll_wait(sd_ptr->epoll_handle, sd_ptr->events_container, sd_ptr->setsize, 500); //timeout value is necessary (regular loop cycling on sd_ptr->up_status) to facilitate catching shutdown signal by main thread
    //>>>>>>>>>>>>>>>>>>>>>>>>

    statsd_gauge(sd_ptr->instrumentation_backend_ptr, "delegator.ready_events.queue_size", ready_events_count, UfsrvConfigInstrumentationBackendServerAsPtr());

    if (WorkQueueLock(sd_ptr, 0) != 0) abort();
    bool stopping = sd_ptr->up_status == 0;
    WorkQueueUnLock(sd_ptr);
    if (stopping) break;

    if (!sd_ptr->listener_hold &&
        atomic_exchange_explicit(&sd_ptr->new_connections_pending, 0, memory_order_acquire)) {
      sDequeueNewConnections(sd_ptr);
    }

    if (ready_events_count > 0) {
      unsigned j;
      long long event_loop_start = GetTimeNowInMicros();

#ifdef __UF_FULLDEBUG
      syslog (LOG_DEBUG, "%s (pid='%lu' ready_events:'%d): --->>> epoll_wait returned: attempting to acquire Session Work Queue lock: I might block if other workers are fetching jobs...", __func__, pthread_self(), ready_events_count);
#endif

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
      if ((WorkQueueLock(sd_ptr, 0)) != 0) {
        syslog(LOG_CRIT, "%s: Session work queue mutex failed", __func__);
        abort();
      }
      if (sd_ptr->up_status == 0) {
        WorkQueueUnLock(sd_ptr);
        break;
      }
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

#ifdef __UF_FULLDEBUG
      syslog(LOG_DEBUG, "sThreadWorkerDelegator: SESSION WORK QUEUE MUTEX LOCK ACQUIRED (1)... looping through ready events... ");
#endif
      //read_ready_events
      int console_commands = 0;
      for (j=0; j<ready_events_count; j++) {
        const struct epoll_event *ee_ptr = &((struct epoll_event *)sd_ptr->events_container)[j];
        SessionIoWork work = {0};

        if (ee_ptr->data.u64 == 0) {
          if (sd_ptr->listener_delegator_ipc) {
            sReadListenerPipe(((UfsrvSession *)GetInstance(sd_ptr->listener_delegator_ipc)));
          }
          continue;
        }
        if (ee_ptr->data.u64 == CONSOLE_DELEGATOR_EPOLL_COOKIE) {
          console_commands = 1;
          continue;
        }
        if (!SessionIoResolve(ee_ptr->data.u64, &work)) continue;
        instance_sesn_ptr = work.session;
        sesn_ptr = ((UfsrvSession *)GetInstance(instance_sesn_ptr));
        work.events = ee_ptr->events;
        if (SessionIoSubmit(&session_io_queue, &sesn_ptr->io_work, &work)) {
          UfsrvSessionReference(instance_sesn_ptr, 1);
          if (!sd_ptr->session_io_hold) broadcast_work++;
        }

      }
      //end read_ready_events

      if (broadcast_work > 0) {
#ifdef __UF_FULLDEBUG
        syslog(LOG_DEBUG, "%s (pid='%lu' broadcast_ready_sz:'%lu'): FINISHED Queueing jobs: SIGNALLING  on 'queue_not_empty_cond' and releasing mutex lock (-1)...", __func__, pthread_self(), broadcast_work);
#endif
        while (broadcast_work--) pthread_cond_signal(&sd_ptr->queue_not_empty_cond);
      } else {
        syslog(LOG_DEBUG, "%s (pid='%lu'): NO JOBS WERE QUEUED... Checking if received events contained New Connections requests", __func__, pthread_self());
      }

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
      //this will set off the other listeners
      if ((WorkQueueUnLock(sd_ptr)) != 0) {
        //todo error recovery
      }
      if (console_commands) ConsoleDelegatorDrain(&sd_ptr->console_channel);
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

      long long event_loop_end = GetTimeNowInMicros();
      statsd_timing(sd_ptr->instrumentation_backend_ptr, "delegator.ready_events.elapsed_time", event_loop_end - event_loop_start, UfsrvConfigInstrumentationBackendServerAsPtr());

    } else if (ready_events_count == -1) {
      if (errno == EINTR)	continue;

      {//TODO: eplo_wait recovery
        char *er;
        char erbuf[MBUF];
        er = strerror_r(errno, erbuf, MBUF);

        syslog(LOG_ERR, "%s {pid:'%lu' errno'%d' error'%s' e:'%d'}: !ERROR: EPOLL_WAIT: COULD NOT POLL I/O EVENTS", __func__, pthread_self(), errno, er, 108);
      }
    }
  }

  sRecycleQueuedConnections(sd_ptr);

  syslog(LOG_NOTICE, "%s (pid:'%lu'): Session delegator shutting down...", __func__, pthread_self());
  pthread_exit(NULL);
}

#endif
