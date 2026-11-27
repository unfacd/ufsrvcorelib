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

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <uflib/standard_defs.h>
#include <ufsrv_core/delegator_sessions/session_io_work.h>
#include <sys/epoll.h>

#include "ufsrv_core/delegator_sessions/workers/worker_session_io_thread.h"
#include "ufsrv_core/delegator_sessions/thread_context_type.h"
#include "ufsrv_core/session/session_io.h"
#include <ufsrv_core/session/sessions_utils.h>
#include <ufsrv_core/system/thread_utils.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
#include "uflib/recycler/recycler.h"
#include "uflib/utils.h"
#include <sys/prctl.h>//for naming thread
#include "ufsrv_core/protocol/protocol.h"
#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend.h>
#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>
#include "ufsrv_core/http/http_request.h"
#include "ufsrv_core/delegator_sessions/delegator_session_worker_thread.h"
#include "ufsrv_core/ufsrvresult_type.h"
#include <ufsrv_core/ratelimit_v2/ratelimit.h>
#include "uflib/adt/adt_hopscotch_hashtable.h"
#include <uflib/net/socket_message_type.h>
#include "ufsrv_core/system/log_message_literals.h"
#include "ufsrv_core/session/sesstion_stat_flags.h"

// #include "utils_cpu.h" //todo fixme this is cannot be pulled in here, because it breaks dependency inversion rules: long term fix: this module needs to be in ufsrcorelib.

static UFSRVResult *_p_ProcessSessionSocketMessage(InstanceHolderForSession *, SocketMessage *, int, uint32_t);
static UFSRVResult *_HandleSessionWorkRequest(InstanceContextForSession *instance_ctx_ptr, SessionsDelegator *sd_ptr, const SessionIoWork *work);
static UFSRVResult *_HandleSuccessfulWorkRequest(SessionsDelegator *sd_ptr, unsigned long session_id_invoked, UFSRVResult *res_ptr);
static UFSRVResult *_HandleMessageForConnectedSession(InstanceHolderForSession *instance_sesn_ptr, SocketMessage *sock_msg_ptr, int flag);
static UFSRVResult *_InvokeLifecycleCallbackMsgOut(InstanceHolderForSession *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned long);
static bool sFollowHeldOnce(SessionsDelegator *, InstanceContextForSession *, InstanceHolderForSession **,
                           InstanceHolderForSession **, const SessionIoWork *);
static ssize_t _s_DrainQueuedPayload(InstanceHolderForSession *);

extern __thread ThreadContext ufsrv_thread_context; //thread local defined in delegator_session_io_worker_thread.c
//extern __thread ThreadContext      *ufsrvworker_thread_context;//to replace above when ready


static uint64_t
sIoNow(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

static void
sReleaseIoWork(const SessionIoWork *work, void *context)
{
  UfsrvSessionUnreference(work->session, 1);
}

static bool
sIoWorkReadable(const SessionIoWork *work)
{
  return (work->events & (EPOLLIN | EPOLLERR)) != 0 ||
         (!work->retry_read && (work->events & EPOLLRDHUP) != 0);
}

static void
sRestoreReadableIoWork(UfsrvSession *session, const SessionIoWork *saved)
{
  SessionsDelegator *sd_ptr = SessionsDelegatorPtr();
  if (WorkQueueLock(sd_ptr, 0) != 0) return;
  if (sd_ptr->up_status != 0 && SessionIoSubmit(&session_io_queue, &session->io_work, saved)) {
    UfsrvSessionReference(saved->session, 1);
    if (!sd_ptr->session_io_hold) pthread_cond_signal(&sd_ptr->queue_not_empty_cond);
  }
  WorkQueueUnLock(sd_ptr);
}

static void
sBleedBusyRead(const SessionIoWork *work)
{
  InstanceHolderForSession *holder = work->session;
  UfsrvSession *session = ((UfsrvSession *)GetInstance(holder));
  if (pthread_mutex_trylock(&(session->message_queue_in.mutex)) != 0) {
    sRestoreReadableIoWork(session, work);
    return;
  }
  bool same = SessionIoGeneration(&session->io_work) == work->generation &&
              (session)->session_id == work->session_id && session->ssptr &&
              (session)->ssptr->sock == work->fd && (session)->ssptr->sock >= 0 &&
              !SESNSTATUS_IS_SET(session->stat, SESNSTATUS_RECYCLED | SESNSTATUS_SUSPENDED);
  bool secure = SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SECURE);
  if (same && !secure) ReadIncomingQueueLocked(session);
  pthread_mutex_unlock(&(session->message_queue_in.mutex));
  if (same && secure) sRestoreReadableIoWork(session, work);
}

static SessionIoAcquire
sAcquireIoWork(const SessionIoWork *work, void *context)
{
  UfsrvSession *session = ((UfsrvSession *)GetInstance(work->session));
  SessionLockRWCtx(THREAD_CONTEXT_PTR, session, _LOCK_TRY_FLAG_TRUE, __func__);
  if (_RESULT_TYPE_EQUAL(THREAD_CONTEXT_UFSRV_RESULT(THREAD_CONTEXT), RESULT_TYPE_ERR)) {
    if (!SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SECURE) && sIoWorkReadable(work))
      return SESSION_IO_BLEED;
    return SESSION_IO_DEFER;
  }
  if (SessionIoGeneration(&session->io_work) != work->generation ||
      (session)->session_id != work->session_id || !session->ssptr ||
      (session)->ssptr->sock < 0 || (session)->ssptr->sock != work->fd ||
      SESNSTATUS_IS_SET(session->stat, SESNSTATUS_RECYCLED)) {
    SessionUnLockCtx(THREAD_CONTEXT_PTR, session, __func__);
    return SESSION_IO_RETIRED;
  }
  if (SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SUSPENDED | SESNSTATUS_INSERVICE)) {
    SessionUnLockCtx(THREAD_CONTEXT_PTR, session, __func__);
    return SESSION_IO_DEFER;
  }
  return SESSION_IO_ACQUIRED;
}

static void
sAbandonHeld(InstanceHolderForSession *holder, const SessionIoWork *identity)
{
  SessionsDelegator *sd_ptr = SessionsDelegatorPtr();
  UfsrvSession *session;
  if (WorkQueueLock(sd_ptr, 0) != 0) {
    UfsrvSessionUnreference(holder, 1);
    return;
  }
  session = ((UfsrvSession *)GetInstance(holder));
  SessionIoFinishHeld(&session_io_queue, &session->io_work, identity, false);
  WorkQueueUnLock(sd_ptr);
  UfsrvSessionUnreference(holder, 1);
}

static void
sMergeOwnerRetry(InstanceHolderForSession *holder, UfsrvSession *session)
{
  if (!session->io_retry_events && !SESNSTATUS_IS_SET(session->stat, SESNSTATUS_RECYCLEREQUEST)) return;
  SessionIoWork extra = {
    .session = holder,
    .session_id = (session)->session_id,
    .fd = (session)->ssptr->sock,
    .generation = SessionIoGeneration(&session->io_work),
    .not_before_ns = session->io_retry_after_ns
  };
  SessionIoContinuation(&extra, session->io_retry_events,
                        SESNSTATUS_IS_SET(session->stat, SESNSTATUS_RECYCLEREQUEST));
  SessionIoSubmit(&session_io_queue, &session->io_work, &extra);
  session->io_retry_events = 0;
  session->io_retry_after_ns = 0;
  SESNSTATUS_UNSET(session->stat, SESNSTATUS_RECYCLEREQUEST);
}

static void
sFinishHeld(InstanceHolderForSession *holder, const SessionIoWork *snapshot)
{
  SessionsDelegator *sd_ptr = SessionsDelegatorPtr();
  UfsrvSession *session;
  SessionIoSlot *slot;
  bool queued;
  if (WorkQueueLock(sd_ptr, 0) != 0) {
    UfsrvSessionUnreference(holder, 1);
    return;
  }
  session = ((UfsrvSession *)GetInstance(holder));
  slot = &session->io_work;
  queued = SessionIoFinishHeld(&session_io_queue, slot, snapshot, sd_ptr->up_status != 0);
  if (queued && !sd_ptr->session_io_hold) pthread_cond_signal(&sd_ptr->queue_not_empty_cond);
  WorkQueueUnLock(sd_ptr);
  if (!queued) UfsrvSessionUnreference(holder, 1);
}

static void
sMoveClaim(SessionsDelegator *sd_ptr, InstanceHolderForSession **claim, InstanceHolderForSession *dest,
           const SessionIoWork *acquired)
{
  UfsrvSession *to;
  UfsrvSession *from;
  ThreadContext *ctx;
  SessionIoWork dest_identity;
  SessionIoHandoff handoff;
  if (!dest || *claim == dest) return;
  to = ((UfsrvSession *)GetInstance(dest));
  ctx = THREAD_CONTEXT_PTR;
  if (!IS_PRESENT(ctx) || !IS_PRESENT(ctx->ht_ptr) || !IsObjectInLockedObjectsStore(ctx->ht_ptr, to)) return;
  if (WorkQueueLock(sd_ptr, 0) != 0) return;
  dest_identity = (SessionIoWork){.session = dest, .session_id = (to)->session_id,
    .fd = (to)->ssptr->sock, .generation = SessionIoGeneration(&to->io_work)};
  from = ((UfsrvSession *)GetInstance(*claim));
  handoff = SessionIoHandoffHeld(&session_io_queue, &from->io_work, &to->io_work,
                                 acquired, &dest_identity);
  WorkQueueUnLock(sd_ptr);
  if (handoff == SESSION_IO_HANDOFF_MOVED) {
    UfsrvSessionReference(dest, 1);
    if (ctx->io_claim_work) *ctx->io_claim_work = dest_identity;
    UfsrvSessionUnreference(*claim, 1);
    *claim = dest;
  } else if (handoff == SESSION_IO_HANDOFF_DEST_PIN) {
    if (ctx->io_claim_work) *ctx->io_claim_work = dest_identity;
    UfsrvSessionUnreference(*claim, 1);
    *claim = dest;
  }
}

static bool
sFollowHeldOnce(SessionsDelegator *sd_ptr, InstanceContextForSession *ctx, InstanceHolderForSession **holder_io,
                InstanceHolderForSession **claim, const SessionIoWork *acquired)
{
  InstanceHolderForSession *holder = *holder_io;
  UfsrvSession *session = ((UfsrvSession *)GetInstance(holder));
  if (SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SUSPENDED | SESNSTATUS_RECYCLED)) return false;
  if (WorkQueueLock(sd_ptr, 0) != 0) return false;
  sMergeOwnerRetry(holder, session);
  SessionIoWork follow;
  bool bits = SessionIoCollectHeld(&session->io_work, &follow);
  size_t waiting = __sync_add_and_fetch(&(UFSRV_SESSION_INSOCKMSG_QUEUE_SIZE(session)), 0);
  bool later = bits && follow.not_before_ns > sIoNow();
  if (later) SessionIoSubmit(&session_io_queue, &session->io_work, &follow);
  WorkQueueUnLock(sd_ptr);
  if (later || (!bits && waiting == 0)) return true;
  if (waiting > 0) {
    InstanceHolderForSession *before = *claim;
    ssize_t drained = _s_DrainQueuedPayload(holder);
    if (*claim != before) {
      holder = *claim;
      session = ((UfsrvSession *)GetInstance(holder));
      *holder_io = holder;
      follow.session = holder;
      follow.session_id = (session)->session_id;
      follow.fd = (session)->ssptr->sock;
      follow.generation = SessionIoGeneration(&session->io_work);
    }
    if (drained < 0 && SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SUSPENDED)) return false;
  }
  if (!bits) return true;
  ctx->instance_sesn_ptr = holder;
  ctx->sesn_ptr = session;
  UFSRVResult *result = _HandleSessionWorkRequest(ctx, sd_ptr, &follow);
  InstanceHolderForSession *next = result ? result->result_user_data : NULL;
  if (!next) return false;
  if (next != holder) {
    sMoveClaim(sd_ptr, claim, next, acquired);
    holder = next;
    session = ((UfsrvSession *)GetInstance(holder));
    *holder_io = holder;
  }
  if (SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SUSPENDED | SESNSTATUS_RECYCLED)) return false;
  return true;
}


sessionworker_thread_callback
GetSessionWorkerThreadHandler(void)
{
  return  &ThreadWebSockets;
}

void *
ThreadWebSockets(void *ptr)
{
	long long service_start = 0,
						service_end   = 0;
	UFSRVResult 			 ufsrv_result					= {0};  //scoped until thread exit
	HttpRequestContext http_request_context = {0};  //scoped until thread exit
	HopscotchHashtableConfigurable locked_objects_store = {0}; //scoped until thread exit
	WorkerThreadCreationContext *th_ctx_ptr = ptr;

  ufsrv_thread_context.random_state = time(NULL) ^ getpid() ^ pthread_self(); //set to a seed value before rand_r is called for the first time
  SessionsDelegator *const sd_ptr = SessionsDelegatorPtr();

  ufsrv_thread_context.user_data = th_ctx_ptr->user_data;
  if (th_ctx_ptr->user_data && sd_ptr->ufsrv_delegator_userdata.on_worker_start)
    sd_ptr->ufsrv_delegator_userdata.on_worker_start(&ufsrv_thread_context);

	{
		#define MAX_NAME_LEN 15
		char proc_name [MAX_NAME_LEN + 1];	/* Name must be <= 15 characters + a null */

		strncpy(proc_name, "ufSessnWorker", MAX_NAME_LEN);
		proc_name [MAX_NAME_LEN] = 0;
		prctl (PR_SET_NAME, (unsigned long)&proc_name);
    UfsrvCpuAffinityPin(UFSRV_THREAD_WORKER, (int)th_ctx_ptr->idx);
		#undef MAX_NAME_LEN
	}

	__init_block:
	#if 1
  if (InitLockedObjectsStore(&locked_objects_store) != 0) {
    syslog(LOG_ERR, "%s: ERROR: COULD NOT INITIALISE the locked-objects store for Session Worker thread: '%lu'...", __func__, pthread_self());
    _exit(-1);
  }

	ufsrv_thread_context.ht_ptr = &locked_objects_store;

	ufsrv_thread_context.res_ptr = &ufsrv_result;


	//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

		if (IS_PRESENT(InitialiseHttpRequestContext(&http_request_context, 0))) {
      ufsrv_thread_context.http_request_context = &http_request_context;
		} else {
			syslog(LOG_ERR, "%s: ERROR: COULD NOT INITIALISE HttpRequestContext for Session Worker thread: '%lu'...", __func__, pthread_self());
			_exit(-1);
		}

		syslog(LOG_DEBUG, "%s: SUCCESS (http_ptr:'%p'): Initialised HttpRequestContext for Session Worker thread: '%lu'...", __func__, &http_request_context, pthread_self());
	//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

	InstrumentationBackend *instr_ptr = NULL;
	instr_ptr = InstrumentationBackendInit(NULL, NULL, NULL, 0);//no namespace
	if (instr_ptr) {
    ufsrv_thread_context.instrumentation_backend = instr_ptr;
	} else {
		syslog(LOG_ERR, "%s: ERROR: COULD NOT INITIALISE INSTRUMENTATION for Session Worker thread: '%lu'...", __func__, pthread_self());
	}

	syslog(LOG_DEBUG, "%s: SUCCESS (instr_ptr:'%p'): Initialised Instrumentation Backend for Session Worker thread: '%lu'...", __func__, instr_ptr, pthread_self());

  syslog(LOG_INFO, "%s (pid:'%lu', idx:'%lu'): SUCCESS Reached initialisation barrier: waiting for other sibling threads...", __func__, pthread_self(), th_ctx_ptr->idx);
  pthread_barrier_wait(&sd_ptr->init_barrier);

	syslog(LOG_INFO, "%s: --> Launching into main loop: pid:'%lu', ufsrv_th_ctx:'%p', th_ctx:'%p', idx:'%lu', queue:'%p', random_state: '%u'...", __func__, pthread_self(), &ufsrv_thread_context, th_ctx_ptr, th_ctx_ptr->idx, &session_io_queue, ufsrv_thread_context.random_state);

	#endif
	//end init_block

	while(1) {
    InstanceContextForSession instance_context = {0};
    SessionIoWork work;
    if (WorkQueueLock(sd_ptr, 0) != 0) continue;
    int taken = SESSION_IO_TAKE_NONE;
    while (sd_ptr->up_status == 1 &&
           (sd_ptr->session_io_hold ||
            (taken = SessionIoTake(&session_io_queue, sIoNow(), sAcquireIoWork, sReleaseIoWork, NULL, &work)) ==
                SESSION_IO_TAKE_NONE)) {
      if (!sd_ptr->session_io_hold && session_io_queue.count) {
        struct timespec deadline;
        uint64_t now = sIoNow();
        uint64_t wait_ns = session_io_queue.retry_at_ns > now ? session_io_queue.retry_at_ns - now : 1;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += wait_ns / 1000000000;
        deadline.tv_nsec += wait_ns % 1000000000;
        if (deadline.tv_nsec >= 1000000000) {
          deadline.tv_nsec -= 1000000000;
          deadline.tv_sec++;
        }
        pthread_cond_timedwait(&sd_ptr->queue_not_empty_cond, &sd_ptr->work_queue_mutex, &deadline);
      } else pthread_cond_wait(&sd_ptr->queue_not_empty_cond, &sd_ptr->work_queue_mutex);
    }
    if (sd_ptr->up_status == 0) {
      SessionIoCancel(&session_io_queue, sReleaseIoWork, NULL);
      WorkQueueUnLock(sd_ptr);
      pthread_exit(NULL);
    }
    if (taken == SESSION_IO_TAKE_BLEED) UfsrvSessionReference(work.session, 1);
    WorkQueueUnLock(sd_ptr);
    if (taken == SESSION_IO_TAKE_BLEED) {
      sBleedBusyRead(&work);
      UfsrvSessionUnreference(work.session, 1);
      continue;
    }
    instance_context.instance_sesn_ptr = work.session;
    instance_context.sesn_ptr = ((UfsrvSession *)GetInstance(work.session));
    InstanceHolderForSession *claim = work.session;
    SessionIoWork claim_identity = work;
    THREAD_CONTEXT.io_claim = &claim;
    THREAD_CONTEXT.io_claim_work = &claim_identity;
    instance_context.sesn_ptr->io_retry_events = 0;
    instance_context.sesn_ptr->io_retry_after_ns = 0;
    // LoadSessionWorkerAccessContext(instance_context.sesn_ptr);

		ssize_t drained = _s_DrainQueuedPayload(instance_context.instance_sesn_ptr);
		instance_context.instance_sesn_ptr = claim;
		instance_context.sesn_ptr = ((UfsrvSession *)GetInstance(claim));
    if (claim != work.session) {
      work.session = claim;
      work.session_id = claim_identity.session_id;
      work.fd = claim_identity.fd;
      work.generation = claim_identity.generation;
    }
		if ((drained < 0)
				&& SESNSTATUS_IS_SET(instance_context.sesn_ptr->stat, SESNSTATUS_SUSPENDED)) {
			SessionUnLockCtx(THREAD_CONTEXT_PTR, instance_context.sesn_ptr, __func__);
			THREAD_CONTEXT.io_claim = NULL;
			THREAD_CONTEXT.io_claim_work = NULL;
			sAbandonHeld(claim, &claim_identity);
			continue;
		}

		__session_in_service:
		SESNSTATUS_SET(instance_context.sesn_ptr->stat, SESNSTATUS_INSERVICE);
		service_start = GetTimeNowInMicros();

#if __UF_FULLDEBUG
		syslog(LOG_DEBUG, "%s (pid:%lu cid:%lu proto:'%d'): END COND_WAIT EVENT: Session retrieved: performing Session I/O work...",
				__func__, pthread_self(), (sesnptr)->session_id, PROTO_PROTOCOL_ID(((Protocol *)(sesnptr)->protocol_type_data)));
#endif

		//do work
		{
			unsigned long session_id_invoked;
			UFSRVResult *res_ptr;
        instance_context.sesn_ptr->when_serviced_start = time(NULL);//service_start/1000000UL;

				session_id_invoked = (instance_context.sesn_ptr)->session_id;

				statsd_inc(THREAD_CONTEXT_INSTRUMENTATION_BACKEND, "session_worker.request", 1.0, UfsrvConfigInstrumentationBackendServerAsPtr());

				//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
				//this may return a suspended session
				res_ptr = _HandleSessionWorkRequest(&instance_context, sd_ptr, &work);//we always return a session back regardless
        instance_context.instance_sesn_ptr = claim;
        instance_context.sesn_ptr = ((UfsrvSession *)GetInstance(claim));
        InstanceHolderForSession *instance_sesn_ptr_aux = (InstanceHolderForSession *)res_ptr->result_user_data;

        if (unlikely(IS_EMPTY(instance_sesn_ptr_aux))) {
          syslog(LOG_ERR, "%s (pid:%lu, o:'%p', cid_invoked:'%lu'): SEVERE ERROR: WE LOST REFERENCE TO SESSION (handshake may have failed)...", __func__, pthread_self(), instance_context.sesn_ptr, session_id_invoked);
          SessionUnLockCtx(THREAD_CONTEXT_PTR, instance_context.sesn_ptr, __func__);
          THREAD_CONTEXT.io_claim = NULL;
          THREAD_CONTEXT.io_claim_work = NULL;
          sAbandonHeld(claim, &claim_identity);
          continue;
        }

        UfsrvSession *sesn_ptr_aux = ((UfsrvSession *)GetInstance(instance_sesn_ptr_aux));
				//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

				SESNSTATUS_UNSET(sesn_ptr_aux->stat, SESNSTATUS_INSERVICE);

				//All error handling happens at lower level. Here we are only interested if the Session is still live or suspended
				if (SESNSTATUS_IS_SET(sesn_ptr_aux->stat, SESNSTATUS_SUSPENDED)) {
					//two rules apply:
					//1) if _RESULT_TYPE_SUCCESS means user initiated quit, so we hard suspend
					//2)if _RESULT_TYPE_ERROR we check if protocol allow for grace soft period

					if (_RESULT_TYPE_SUCCESS(res_ptr)) {
						//all statements with '///' intentionally disabled
						///if (SuspendSession (sesn_ptr_aux, 1))	recycle_flag=true;
					} else if ((_RESULT_TYPE_ERROR(res_ptr)) &&
						            (!UfsrvSessionProtocol(sesn_ptr_aux)->protocol_controls.retain_session_on_error)) {
						///if (SuspendSession (sesn_ptr_aux, 1))	recycle_flag=true;
					}
				}

				if (instance_sesn_ptr_aux != claim) sMoveClaim(sd_ptr, &claim, instance_sesn_ptr_aux, &claim_identity);

				bool publish = claim == instance_sesn_ptr_aux &&
				               !SESNSTATUS_IS_SET(sesn_ptr_aux->stat, SESNSTATUS_SUSPENDED | SESNSTATUS_RECYCLED) &&
				               sFollowHeldOnce(sd_ptr, &instance_context, &instance_sesn_ptr_aux, &claim, &claim_identity);
				sesn_ptr_aux = ((UfsrvSession *)GetInstance(instance_sesn_ptr_aux));

				//>>>>>>>>>>>>>>>>>>>>>>>
				service_end = GetTimeNowInMicros();
				sesn_ptr_aux->when_serviced_end = time(NULL);//service_end/1000000UL;
				statsd_timing(THREAD_CONTEXT_INSTRUMENTATION_BACKEND, "worker.session.service.elapsed_time", (service_end - service_start), UfsrvConfigInstrumentationBackendServerAsPtr());

				SessionIoWork snapshot = {0};
				bool same = claim == instance_sesn_ptr_aux;
				if (same) {
					snapshot.session = claim;
					snapshot.session_id = (sesn_ptr_aux)->session_id;
					snapshot.fd = (sesn_ptr_aux)->ssptr->sock;
					snapshot.generation = claim_identity.generation;
					snapshot.not_before_ns = sesn_ptr_aux->io_retry_after_ns;
					SessionIoContinuation(&snapshot, sesn_ptr_aux->io_retry_events,
					                      SESNSTATUS_IS_SET(sesn_ptr_aux->stat, SESNSTATUS_RECYCLEREQUEST));
					sesn_ptr_aux->io_retry_events = 0;
					sesn_ptr_aux->io_retry_after_ns = 0;
					SESNSTATUS_UNSET(sesn_ptr_aux->stat, SESNSTATUS_RECYCLEREQUEST);
				}
				SessionUnLockCtx(THREAD_CONTEXT_PTR, sesn_ptr_aux, __func__);
				THREAD_CONTEXT.io_claim = NULL;
				THREAD_CONTEXT.io_claim_work = NULL;
				if (same) {
					if (publish) sFinishHeld(claim, &snapshot);
					else sAbandonHeld(claim, &claim_identity);
				} else sAbandonHeld(claim, &claim_identity);

#if 0
				//at the moment this semantic is disabled. Kicking off session in this loop has proved problematic
				//instead, we rely on timer thread to catch up with it
				if (recycle_flag) {
					//IMPORTANT to only add to recycler after session has been unlocked and completely reset
					SESNSTATUS_SET(sesn_ptr_aux->stat, SESNSTATUS_RECYCLED);

					RecyclerPut(1, (RecyclerClientData *)sesn_ptr_aux, (ContextData *)NULL, 0);
					recycle_flag = false;
				}
#endif
				//>>>>>>>>>>>>>>>>>>>>>>>

				//else the Session either destructed or suspended so we don't care
		}
	}

}

static ssize_t
_s_DrainQueuedPayload(InstanceHolderForSession *instance_sesn_ptr)
{
	UfsrvSession *sesn_ptr = ((UfsrvSession *)GetInstance(instance_sesn_ptr));
	UFSRVResult    res;
	UFSRVResult   *res_ptr;
	SocketMessage *sm_ptr;

	if (__sync_add_and_fetch(&(sesn_ptr->message_queue_in.queue.nEntries), 0) == 0)	return 0;

	if ((pthread_mutex_trylock(&(sesn_ptr->message_queue_in.mutex))) != 0)	return -1;

	ConsolidateSocketMessageQueue(sesn_ptr, SOCKMSG_CONSOLIDATE_INSESSION, &res);

	pthread_mutex_unlock(&(sesn_ptr->message_queue_in.mutex));

	if (res.result_type != RESULT_TYPE_SUCCESS)	return -1;

	sm_ptr = (SocketMessage *)res.result_user_data;

	if (sm_ptr->sm_errno == SOCKMSG_SMERRNO_PEERCLOSED) {
		sm_ptr->sm_errno = 0;

		UfsrvSessionSuspendSoft(instance_sesn_ptr);

		return -1;
	}

	sm_ptr->sm_errno = 0;

	if (sm_ptr->raw_msg_size == 0)	return 0;

	ssize_t drained = sm_ptr->raw_msg_size;

	res_ptr = _p_ProcessSessionSocketMessage(instance_sesn_ptr, sm_ptr, SOCKMSG_READBUFFER, 0);

	return (_RESULT_TYPE_SUCCESS(res_ptr)) ? drained : -1;
}

/**
//@brief	called from the websocket worker thread after it picked up a session work request. Its main function is a glue between the worker and the
//	low-level network i/o message processing stream, including websocket de-framing and command processing.
//	after returning from the i/o stream it checks the incoming queue for missed messages.
*	@locks: SocketMessage Queue indirectly via ConsolidateMessageQueue
*/
typedef struct SessionIoService {
  InstanceHolderForSession *holder;
  UFSRVResult *result;
  int fd;
} SessionIoService;

static SessionIoReadResult
sReadIoWork(void *context)
{
  SessionIoService *service = context;
  UfsrvSession *session = ((UfsrvSession *)GetInstance(service->holder));
  unsigned long before = UFSRV_SESSION_CUMMULATIVE_RC(session);
  service->result = _p_ProcessSessionSocketMessage(service->holder, UFSRV_SESSION_INSOCKMSG_TRANS_PTR(session),
                                                  SOCKMSG_READSOCKET, 0);
  if (service->result->result_user_data) service->holder = service->result->result_user_data;
  session = ((UfsrvSession *)GetInstance(service->holder));
  bool valid = session->ssptr && (session)->ssptr->sock >= 0 && (session)->ssptr->sock == service->fd &&
               !SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SUSPENDED | SESNSTATUS_RECYCLED);
  return SessionIoReadOutcome(service->result, valid,
                              session->ssptr && UFSRV_SESSION_CUMMULATIVE_RC(session) > before,
                              sIoNow(), session->io_retry_after_ns);
}

static void
sWriteIoWork(void *context)
{
  SessionIoService *service = context;
  UfsrvSession *session = ((UfsrvSession *)GetInstance(service->holder));
  service->result = _InvokeLifecycleCallbackMsgOut(service->holder, UFSRV_SESSION_INSOCKMSG_TRANS_PTR(session),
                                                  CALLFLAGS_EMPTY);
}

static void
sCloseIoWork(void *context)
{
  SessionIoService *service = context;
  UfsrvSession *session = ((UfsrvSession *)GetInstance(service->holder));
  ErrorFromSocket(service->holder, SOCKMSG_CONSOLIDATE_INSESSION | SOCKMSG_LOCK_SOCKMSGQUEUE);
  service->result = UFSRV_SESSION_RESULT_PTR(session);
  service->result->result_type = RESULT_TYPE_ERR;
  service->result->result_code = RESCODE_IO_CONNECTIONCLOSED;
  service->result->result_user_data = service->holder;
}

static void
sDeferIoWork(void *context, uint32_t events, uint64_t retry_at)
{
  SessionIoService *service = context;
  UfsrvSession *session = ((UfsrvSession *)GetInstance(service->holder));
  session->io_retry_events |= events;
  if (session->io_retry_after_ns < retry_at) session->io_retry_after_ns = retry_at;
}

static void
sRetryTerminalIoWork(void *context, uint32_t events)
{
  SessionIoService *service = context;
  UfsrvSession *session = ((UfsrvSession *)GetInstance(service->holder));
  session->io_retry_events |= EPOLLIN | events;
}

static UFSRVResult *
_HandleSessionWorkRequest(InstanceContextForSession *instance_ctx_ptr, SessionsDelegator *sd_ptr, const SessionIoWork *work)
{
  static const SessionIoCallbacks callbacks = {
    .read = sReadIoWork,
    .write = sWriteIoWork,
    .close = sCloseIoWork,
    .defer = sDeferIoWork,
    .retry_terminal = sRetryTerminalIoWork
  };
  SessionIoService service = {.holder = instance_ctx_ptr->instance_sesn_ptr,
                               .fd = (instance_ctx_ptr->sesn_ptr)->ssptr->sock};
  UfsrvSession *session = instance_ctx_ptr->sesn_ptr;
  if (work->session != service.holder || work->session_id != (session)->session_id ||
      work->fd != service.fd || work->generation != SessionIoGeneration(&session->io_work)) {
    UFSRVResult *result = UFSRV_SESSION_RESULT_PTR(session);
    result->result_type = RESULT_TYPE_ERR;
    result->result_code = RESCODE_IO_SESSIONSTATE;
    result->result_user_data = service.holder;
    return result;
  }
  SessionIoDispatch(work, &callbacks, &service);
  if (!service.result) {
    service.result = UFSRV_SESSION_RESULT_PTR(instance_ctx_ptr->sesn_ptr);
    service.result->result_type = RESULT_TYPE_SUCCESS;
    service.result->result_code = RESCODE_SERVICED;
    service.result->result_user_data = service.holder;
  }
  if (_RESULT_TYPE_SUCCESS(service.result) && service.result->result_code != RESCODE_IO_CONNECTIONCLOSED)
    return _HandleSuccessfulWorkRequest(sd_ptr, work->session_id, service.result);
  return service.result;
}

/**
 * 	@brief Session request was previously successfully completed. Before we return to the thread's main loop we check if the
 * 	the session has received any new incoming packets and process accordingly.
 *
 * 	@locked sesn_ptr_processed: by main loop
 */
static inline UFSRVResult *
_HandleSuccessfulWorkRequest(SessionsDelegator *sd_ptr, unsigned long session_id_invoked, UFSRVResult *res_ptr)
{
	InstanceHolderForSession *instance_sesn_ptr_processed = (InstanceHolderForSession *)_RESULT_USERDATA(res_ptr);//session object is always returned, even if suspended
  UfsrvSession *sesn_ptr_processed = ((UfsrvSession *)GetInstance(instance_sesn_ptr_processed));
	if ((sesn_ptr_processed)->session_id != session_id_invoked) {
#if __UF_FULLDEBUG
		syslog(LOG_DEBUG, "%s (pid:%lu o:'%p' cid:%lu): SESSION CHANGE OVER: invoked with: '%lu' -> returned: '%lu'", __func__, pthread_self(), sesn_ptr_processed, (sesn_ptr_processed)->session_id, session_id_invoked, (sesn_ptr_processed)->session_id);
#endif
	}

	//(2)check for/process stored message in incoming queue whilst in locked state
	//__consolidate_msgqueue_post:
	if (__sync_add_and_fetch(&UFSRV_SESSION_INSOCKMSG_QUEUE_SIZE(sesn_ptr_processed), 0) > 0) {
		//this locks the entire incoming queue. mid-process threads won't be able to add to it
		UFSRVResult		res;
		SocketMessage	*sm_ptr_consolidated;

		//consolidate all queue entries into the transient incoming SocketMessage buffer ie sesn_ptr->ssptr->incoming
		//LOCKS MESSAGE QUEUE
		ConsolidateSocketMessageQueue(sesn_ptr_processed, (SOCKMSG_CONSOLIDATE_INSESSION|SOCKMSG_LOCK_SOCKMSGQUEUE), &res);

		if (res.result_type == RESULT_TYPE_SUCCESS) {
			sm_ptr_consolidated = (SocketMessage *)res.result_user_data;

			if (!(sm_ptr_consolidated->sm_errno == 0)) {
				syslog(LOG_DEBUG, LOGSTR_QUEDIOERR, __func__, pthread_self(), sesn_ptr_processed, (sesn_ptr_processed)->session_id, sm_ptr_consolidated->sm_errno, LOGCODE_TSWORKER_QUEUEDIOERR, "POST REQUEST SERVICE");

				if (sm_ptr_consolidated->sm_errno == SOCKMSG_SMERRNO_PEERCLOSED) {
					sm_ptr_consolidated->sm_errno = 0;

					UfsrvSessionSuspendSoft(instance_sesn_ptr_processed);

					res_ptr->result_type = RESULT_TYPE_ERR;
					res_ptr->result_code = RESCODE_IO_CONNECTIONCLOSED;
					res_ptr->result_user_data = instance_sesn_ptr_processed;

					goto request_error_pre;
				}

				//requires Session lock, but since we performed consolidation above, no SocketMessage queue lock is required
				//don't consolidate we are not passing SOCKMSG_CONSOLIDATE_INSESSION
				//BUFFERS(if any as result of consolidation) DEALLOCATED in hard suspend

				ErrorFromSocket(instance_sesn_ptr_processed, 0);//Suspends session

				res_ptr->result_type = RESULT_TYPE_ERR;
				res_ptr->result_code = RESCODE_IO_SOCKETQUEUE_CONSOLIDTED;
				res_ptr->result_user_data = instance_sesn_ptr_processed;

				goto request_error_pre;
			}

			res_ptr = _p_ProcessSessionSocketMessage(instance_sesn_ptr_processed, sm_ptr_consolidated, SOCKMSG_READBUFFER, 0);
			if (_RESULT_TYPE_SUCCESS(res_ptr)) {
				goto request_successful;
			} else {
				//ON ERROR: _p_ProcessSessionSocketMessage() INVOKES SuspendsSession()
				goto request_error_pre; //this will return res_ptr along with its error settings
			}
		} else if ((res.result_code != RESCODE_LOGIC_EMPTY_RESOURCE) && (res.result_code != RESCODE_LOGIC_CANTLOCK)) {//ie error is not related to queue being empty
			//too bad we just bailout the session
			syslog(LOG_DEBUG, LOGSTR_IO_BUF_CONSOLIDATION_ERR, __func__, pthread_self(), sesn_ptr_processed, (sesn_ptr_processed)->session_id, LOGCODE_IO_BUF_CONSOLIDATION_ERR);

			res_ptr->result_type = res.result_type;
			res_ptr->result_code = res.result_code;
			res_ptr->result_user_data = instance_sesn_ptr_processed;

			UfsrvSessionSuspendSoft(instance_sesn_ptr_processed);

			goto request_error_pre;
		}
		else goto request_successful;//queue was empty, so we are good to proceed
	}
	//end __consolidate_msgqueue_post

	request_successful:
#ifdef __UF_FULLDEBUG
	syslog(LOG_DEBUG, "%s (pid:%lu, cid:%lu): SUCCESSFULLY PEROCESSED WORK REQUEST...", __func__, pthread_self(), (sesn_ptr_processed)->session_id);
#endif

	_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr_processed), instance_sesn_ptr_processed, RESULT_TYPE_SUCCESS, RESCODE_SERVICED)

	request_error_pre:
	//sesn_ptr_processed->when_serviced_end=time(NULL);//orig sesnptr
	goto request_error;

	request_error:
#ifdef __UF_TESTING
		syslog(LOG_DEBUG, "%s (pid:%lu, cid_invoked:'%lu'): UNSUCCESSFULLY PEROCESSED WORK REQUEST for Session...",__func__, pthread_self(), session_id_invoked);
#endif

	return res_ptr;

}

static bool
sIsSessionRequestRateLimitExceeded(UfsrvSession *sesn_ptr)
{
	char namespace_key[64];

	if (!UfsrvConfigRatelimitIsEnabled()) return false;
  bool denied = !RateLimitFormatSessionNamespace(namespace_key, sizeof(namespace_key), UFSRV_SESSION_ID(sesn_ptr),
                                                  (sesn_ptr)->session_id, RL_CAT_REQUESTS) ||
                RateLimitIsExceededForCategory(&ufsrv_thread_context.ratelimit_handle, namespace_key, RL_CAT_REQUESTS);
  if (denied) {
    const RateLimitPolicy *policy = RateLimitSpecsFor(RL_CAT_REQUESTS);
    uint64_t delay = policy ? ((uint64_t)policy->interval + 1) * 1000000 : 100000000;
    sesn_ptr->io_retry_after_ns = sIoNow() + delay;
  }
  return denied;
}

//https://firestuff.org/2016-02-24-down_the_epoll_rabbit_hole.html
/**
* @brief	process a single message contained in SocketMessage, which could be in any state other than new connection request (handled in the main loop).
 * Handshake and other regular comms are processed here.
* For brand-new connections:
* Session is either newly created (FLEDGLING|CONNECTED) or a previously connected one (RECYCLED|CONNECTED)
* FLEDGLING does not have a SessionService body initiated. Where it fails handshake we destruct it as opposed to recycle
* For recurring requests Session must be (CONNECTED|HANDSKAED|AUTHENTICATED) RECYCLED is irrelevant
* we should not allow a Session to be serviced by two simultaneous workers
* @locks
*/
static UFSRVResult *
_p_ProcessSessionSocketMessage(InstanceHolderForSession *instance_sesn_ptr, SocketMessage *sock_msg_ptr, int flag, uint32_t evbits)
{
  UfsrvSession *sesn_ptr = ((UfsrvSession *)GetInstance(instance_sesn_ptr));
  const Protocol *protocol = UfsrvSessionProtocol(sesn_ptr);

	if (flag & (SOCKMSG_READSOCKET | SOCKMSG_READBUFFER)) {
		__readable_input:
		if (!(sesn_ptr->stat&SESNSTATUS_CONNECTED)) {
			syslog(LOG_DEBUG, "%s (pid:'%lu' o:'%p' cid:'%lu' fd:'%d'): SESSION NOT CONNECTED: RETURNING...", __func__, pthread_self(), sesn_ptr, (sesn_ptr)->session_id, (sesn_ptr)->ssptr->sock);

			goto exit_non_successful;
		}

		if (protocol && protocol->protocol_callbacks.handshake_callback) {
			//protocol_feature_handshake:
			if (!(sesn_ptr->stat&SESNSTATUS_HANDSHAKED) && !(sesn_ptr->stat&SESNSTATUS_AUTHENTICATED)) {
#ifdef __UF_FULLDEBUG
				syslog(LOG_DEBUG, "%s (pid:'%lu' o:'%p', cid:'%lu'): INVOKING HANDSHAKE LIFECYCLE CALLBACK...", __func__, pthread_self(), sesn_ptr, (sesn_ptr)->session_id);
#endif
        return protocol->protocol_callbacks.handshake_callback(instance_sesn_ptr, sock_msg_ptr, CALLFLAGS_EMPTY, NULL);
			} else if ((sesn_ptr->stat&SESNSTATUS_HANDSHAKED) && (sesn_ptr->stat&SESNSTATUS_AUTHENTICATED)) {
#ifdef __UF_TESTING
				syslog(LOG_DEBUG, "%s (pid:'%lu' o:'%p' cid:'%lu'): CONNECTED USER MSG RECEIVED...", __func__, pthread_self(), sesn_ptr, (sesn_ptr)->session_id);
#endif

				if (sIsSessionRequestRateLimitExceeded(sesn_ptr))
					_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_ERR, RESCODE_USER_RATELIMIT_EXCEEDED);

				return(_HandleMessageForConnectedSession(instance_sesn_ptr, sock_msg_ptr, flag));
			} else {
				//in a level triggered polling, an event could be in the queue for a session that is recycled because the object is not
				//destroyed it is in the recycler.
				syslog(LOG_NOTICE, "%s (pid:'%lu' o:'%p' cid:'%lu'): INCONSISTENT SESSION STATE: ('%lu'): IGNORING", __func__, pthread_self(), sesn_ptr, (sesn_ptr)->session_id, sesn_ptr->stat);

				//TODO: this is causes inconsistencies, especially if ses is already suspended
				//SuspendSession (sesn_ptr, 0);

				_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_ERR, RESCODE_IO_SESSIONSTATE)
			}
			//end protocol_feature_handshake
		} else {
			//protocol_feature_no_handshake:
#ifdef __UF_FULLDEBUG
			syslog(LOG_DEBUG, "%s (pid:'%lu' cid:'%lu' fd:'%d'): NO-HANDSHAKE CONNECTED USER MSG RECEIVED...", __func__, pthread_self(), (sesn_ptr)->session_id, (sesn_ptr)->ssptr->sock);
#endif
			if (sIsSessionRequestRateLimitExceeded(sesn_ptr))
				_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_ERR, RESCODE_USER_RATELIMIT_EXCEEDED);

			return (_HandleMessageForConnectedSession(instance_sesn_ptr, sock_msg_ptr, flag));
		}
	}

	exit_non_successful:
	_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_ERR, RESCODE_IO_POLL)

}

/**
 * EPOLLET event response handler
 * @return
 */
static inline UFSRVResult *
_InvokeLifecycleCallbackMsgOut(InstanceHolderForSession *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned long call_flags)
{
  UfsrvSession *sesn_ptr = ((UfsrvSession *)GetInstance(instance_sesn_ptr));
  const Protocol *protocol = UfsrvSessionProtocol(sesn_ptr);

  if (DispatchSocketMessageQueue(instance_sesn_ptr, 0) < 0) {
    _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_ERR, RESCODE_IO_CONNECTIONCLOSED)
  }

	if (protocol && protocol->protocol_callbacks.msg_out_callback) {
		UFSRVResult *res_ptr = protocol->protocol_callbacks.msg_out_callback(instance_sesn_ptr, sock_msg_ptr, call_flags, 0);

		switch (_RESULT_TYPE(res_ptr))
		{
			case RESULT_TYPE_ERR:

        statsd_gauge_inc(THREAD_CONTEXT_INSTRUMENTATION_BACKEND, "worker.work.handshake_failed", 1, UfsrvConfigInstrumentationBackendServerAsPtr());

				UfsrvSessionSuspendSoft(instance_sesn_ptr);

				_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_ERR, RESCODE_IO_MSGDISPATCHED)

			default:
				//could have fallen through to _exit_sucess: below
				if (_RESULT_CODE_EQUAL(res_ptr, RESCODE_SESN_SOFTSPENDED))	UfsrvSessionSuspendSoft(instance_sesn_ptr);
				_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_SUCCESS, RESCODE_IO_MSGDISPATCHED)
		}
	} else {
#ifdef __UF_FULLDEBUG
		syslog(LOG_DEBUG, "%s (pid:'%lu' cid:'%lu'): NO MSG OUT CALLBACK DEFINED FOR PROTOCOL (id:'%d', name:'%s')", __func__, pthread_self(), (sesn_ptr)->session_id,
						PROTO_PROTOCOL_ID(((Protocol *)(sesn_ptr)->protocol_type_data)), PROTO_PROTOCOL_NAME(((Protocol *)(sesn_ptr)->protocol_type_data)));
#endif
	}

	exit_success:
	_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_SUCCESS, RESCODE_IO_MSGDISPATCHED)
}

/**
 * 	@brief: Key Session routing function for connections that support handshake.
 * 	Transition the newly connected session (aka in transient state) past initial incoming handshake.
 * 	Session is not authenticated in anyway. We have just processed basic comms/protocol semantics.
 *
 * 	@dynamic_memory redisReply: IMPORTED BY PROXY <- BackendCacheGetSessionRecordByCookie() AND EXPORTED -> _AuthenticateForBackendCookieHashedSession().
 * 	  Not feree'd here
 */
static inline UFSRVResult *
_HandleMessageForConnectedSession(InstanceHolderForSession *instance_sesn_ptr, SocketMessage *sock_msg_ptr, int flag)
{
  UfsrvSession *sesn_ptr = ((UfsrvSession *)GetInstance(instance_sesn_ptr));
  const Protocol *protocol = UfsrvSessionProtocol(sesn_ptr);

	if (protocol && protocol->protocol_callbacks.msg_callback) {
		UFSRVResult *res_ptr = protocol->protocol_callbacks.msg_callback(instance_sesn_ptr, sock_msg_ptr, flag, 0);

		switch (res_ptr->result_type)
		{
		  case RESULT_TYPE_ERR:
			case RESULT_TYPE_PROTOCOLERR:
			case RESULT_TYPE_IOERR:

				_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_ERR, res_ptr->result_code)

			default:
				//reset buffer
				if (sock_msg_ptr->processed_msg_size > 0) {
					free(sock_msg_ptr->_processed_msg);
					sock_msg_ptr->_processed_msg = 0;
					sock_msg_ptr->processed_msg_size = 0;
				}

				_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), instance_sesn_ptr, RESULT_TYPE_SUCCESS, res_ptr->result_code)
		}
	} else {
#ifdef __UF_FULLDEBUG
		syslog(LOG_DEBUG, "%s (pid:'%lu' cid:'%lu'): NO MSG CALLBACK DEFINED FOR PROTOCOL (id:'%d', name:'%s')", __func__, pthread_self(), (sesn_ptr)->session_id,
				PROTO_PROTOCOL_ID(((Protocol *)(sesn_ptr)->protocol_type_data)), PROTO_PROTOCOL_NAME(((Protocol *)(sesn_ptr)->protocol_type_data)));
#endif
	}

	_RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), NULL,  RESULT_TYPE_ERR, RESCODE_PROG_NULL_POINTER)

}
