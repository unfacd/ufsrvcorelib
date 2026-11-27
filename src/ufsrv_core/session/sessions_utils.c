/**
 * @file sessions_utils.c
 * @brief Session lock and message-queue lock helpers.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <uflib/standard_defs.h>
#include <uflib/utils_crypto.h>
#include <uflib/utils_threads.h>

#include <ufsrvcorelib/ufsrv_core/session/session_layout.h>
#include "ufsrv_core/delegator_sessions/delegator_session_worker_thread.h"
#include <ufsrv_core/delegator_sessions/thread_context_type.h>
#include "ufsrv_core/protocol/protocol.h"
#include "ufsrv_core/system/log_message_literals.h"
#include "ufsrv_core/ufsrv_config/ufsrv_config_shim.h"
#include <ufsrv_core/session/sessions_utils.h>
#include <ufsrv_core/session/ufsrv_session_type.h>
#include <ufsrv_core/system/thread_utils.h>
#include <ufsrv_core/ufsrvresult_type.h>
#include "ufsrv_core/session/sesstion_stat_flags.h"

UFSRVResult *
SessionLockRDCtx(ThreadContext *thread_ctx_ptr, UfsrvSession *sesn_ptr, int try_flag)
{
  int          lock_state;
  UFSRVResult *res_ptr;

  if (likely(IS_PRESENT(thread_ctx_ptr))) {
    res_ptr = thread_ctx_ptr->res_ptr;

#if UFNET_DEBUG_BUILD
    syslog(LOG_DEBUG, "LOCKLEDGER A cid:'%lu' o:'%p' tid:'%lu' ht:'%p' held:%d try:%d func:'%s'", sesn_ptr->session_id,
           sesn_ptr, pthread_self(), thread_ctx_ptr->ht_ptr,
           IsObjectInLockedObjectsStore(thread_ctx_ptr->ht_ptr, (void*)sesn_ptr) ? 1 : 0, try_flag, __func__);
#endif

    if (IsObjectInLockedObjectsStore(thread_ctx_ptr->ht_ptr, sesn_ptr)) {
      lock_state = 0;
      syslog(LOG_DEBUG, "%s {pid:'%lu', cid:'%lu', o:'%p, ctx:'%p', try:'%i'}: NOTICE: NOT LOCKING: ALREADY IN STORE",
             __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0,
             try_flag);
      goto return_already_locked_by_this_thread;
    }
  }
  else res_ptr = &(sesn_ptr->result);

  if (try_flag) {
    lock_state = pthread_rwlock_tryrdlock(&(sesn_ptr->session_events.rwlock));
    if (lock_state == 0) {
      syslog(
        LOG_DEBUG,
        "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p', lock:50:1 ): SUCCESS: TRY-READ lock for Session events acquired...",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0);
      if (IS_PRESENT(thread_ctx_ptr) && (PutIntoLockedObjectsStore(thread_ctx_ptr->ht_ptr, (void*)sesn_ptr) != 0)) {
        syslog(
          LOG_ERR,
          "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p'): ERROR: COULD NOT RECORD LOCK OWNERSHIP: RELEASING AND NOT TREATING AS LOCKED...",
          __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, thread_ctx_ptr);
        pthread_rwlock_unlock(&(sesn_ptr->session_events.rwlock));
        goto return_wont_lock;
      }
      goto return_locked;
    }
    else {
      char *err_str = thread_error(errno);
      syslog(
        LOG_DEBUG,
        "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p'): ERROR: COULD NOT acquire TRY-READ lock for Session events (errno='%d'):  '%s'",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0,
        errno, err_str);
    }
  }
  else {
    lock_state = pthread_rwlock_rdlock(&(sesn_ptr->session_events.rwlock));
    if (lock_state == 0) {
      syslog(
        LOG_DEBUG,
        "%s (pid:'%lu', cid:'%lu', o:'%p, ctx:'%p', lock:50:1 ): SUCCESS: READ lock for Session events acquired...",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0);
      if (IS_PRESENT(thread_ctx_ptr) && (PutIntoLockedObjectsStore(thread_ctx_ptr->ht_ptr, (void*)sesn_ptr) != 0)) {
        syslog(
          LOG_ERR,
          "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p'): ERROR: COULD NOT RECORD LOCK OWNERSHIP: RELEASING AND NOT TREATING AS LOCKED...",
          __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, thread_ctx_ptr);
        pthread_rwlock_unlock(&(sesn_ptr->session_events.rwlock));
        goto return_wont_lock;
      }
      goto return_locked;
    }
    else {
      char *err_str = thread_error(errno);
      syslog(
        LOG_DEBUG,
        "%s (pid='%lu' cid='%lu' o='%p): ERROR: COULD NOT acquire READ lock for Session events (errno='%d'):  '%s'",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, errno, err_str);
    }
  }

return_wont_lock: _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, RESCODE_PROG_WONTLOCK)

return_locked: _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_SUCCESS, RESCODE_PROG_LOCKED)

return_already_locked_by_this_thread: _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_SUCCESS,
                                                         RESCODE_PROG_LOCKED_BY_THIS_THREAD)
}

UFSRVResult *
SessionLockRWCtx(ThreadContext *thread_ctx_ptr, UfsrvSession *sesn_ptr, int try_flag, const char *func)
{
  int          lock_state;
  UFSRVResult *res_ptr;

  if (likely(IS_PRESENT(thread_ctx_ptr))) {
    res_ptr = thread_ctx_ptr->res_ptr;

#if UFNET_DEBUG_BUILD
    syslog(LOG_DEBUG, "LOCKLEDGER A cid:'%lu' o:'%p' tid:'%lu' ht:'%p' held:%d try:%d func:'%s'", sesn_ptr->session_id,
           sesn_ptr, pthread_self(), thread_ctx_ptr->ht_ptr,
           IsObjectInLockedObjectsStore(thread_ctx_ptr->ht_ptr, (void*)sesn_ptr) ? 1 : 0, try_flag, func);
#endif

    if (IsObjectInLockedObjectsStore(thread_ctx_ptr->ht_ptr, sesn_ptr)) {
      lock_state = 0;
      syslog(
        LOG_DEBUG,
        "%s (pid:'%lu', cid:'%lu', o:'%p, ctx:'%p', try:'%i', func:'%s'): NOTICE: NOT LOCKING: ALREADY IN STORE",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, thread_ctx_ptr, try_flag, func);
      goto return_already_locked_by_this_thread;
    }
  }
  else res_ptr = &(sesn_ptr->result);

  if (try_flag) {
    lock_state = pthread_rwlock_trywrlock(&(sesn_ptr->session_events.rwlock));
    if (lock_state == 0) {
#if UFNET_DEBUG_BUILD
      syslog(
        LOG_DEBUG,
        "%s (pid:'%lu' cid:'%lu' o:'%p', ctx:'%p', try:1, func:'%s'): SUCCESS: TRY-WRITE/READ lock for Session events acquired...",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0L,
        func);
#endif
      if (IS_PRESENT(thread_ctx_ptr) && (PutIntoLockedObjectsStore(thread_ctx_ptr->ht_ptr, (void*)sesn_ptr) != 0)) {
        syslog(
          LOG_ERR,
          "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p'): ERROR: COULD NOT RECORD LOCK OWNERSHIP: RELEASING AND NOT TREATING AS LOCKED...",
          __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, thread_ctx_ptr);
        pthread_rwlock_unlock(&(sesn_ptr->session_events.rwlock));
        goto return_wont_lock;
      }

      goto return_locked;
    }
    else if (lock_state != EBUSY) {
      char *err_str = thread_error_wrlock(lock_state);
      syslog(
        LOG_DEBUG,
        "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p', try:1, func:'%s'): ERROR: COULD NOT acquire TRY-WRITE/READ lock for Session events (lock='%d'): '%s'",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0, func,
        lock_state, err_str);
    }
  }
  else {
    lock_state = pthread_rwlock_wrlock(&(sesn_ptr->session_events.rwlock));
    if (lock_state == 0) {
#if UFNET_DEBUG_BUILD
      syslog(
        LOG_DEBUG,
        "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p', func:'%s'): SUCCESS: WRITE/READ lock for Session events acquired...",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0L,
        func);
#endif
      if (IS_PRESENT(thread_ctx_ptr) && (PutIntoLockedObjectsStore(thread_ctx_ptr->ht_ptr, (void*)sesn_ptr) != 0)) {
        syslog(
          LOG_ERR,
          "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p'): ERROR: COULD NOT RECORD LOCK OWNERSHIP: RELEASING AND NOT TREATING AS LOCKED...",
          __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, thread_ctx_ptr);
        pthread_rwlock_unlock(&(sesn_ptr->session_events.rwlock));
        goto return_wont_lock;
      }

      goto return_locked;
    }
    else {
      char *err_str = thread_error_wrlock(errno);
      syslog(
        LOG_DEBUG,
        "%s (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p', func:'%s'): ERROR: COULD NOT acquire WRITE/READ lock for Session events (errno='%d'): '%s'",
        __func__, pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0, func,
        errno, err_str);
    }
  }

return_wont_lock: _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, RESCODE_PROG_WONTLOCK)

return_locked: _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_SUCCESS, RESCODE_PROG_LOCKED)

return_already_locked_by_this_thread: _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_SUCCESS,
                                                         RESCODE_PROG_LOCKED_BY_THIS_THREAD)
}

UFSRVResult *
SessionUnLockCtx(ThreadContext *thread_ctx_ptr, UfsrvSession *sesn_ptr, const char *func)
{
  UFSRVResult *res_ptr;

  if (IS_PRESENT(thread_ctx_ptr)) res_ptr = thread_ctx_ptr->res_ptr;
  else res_ptr                            = &(sesn_ptr->result);

#if UFNET_DEBUG_BUILD
  syslog(LOG_DEBUG, "LOCKLEDGER R cid:'%lu' o:'%p' tid:'%lu' ht:'%p' held:%d func:'%s'", sesn_ptr->session_id, sesn_ptr,
         pthread_self(), IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr->ht_ptr : 0L,
         (IS_PRESENT(thread_ctx_ptr) && IsObjectInLockedObjectsStore(thread_ctx_ptr->ht_ptr, (void*)sesn_ptr)) ? 1 : 0,
         func);
#endif

  if (IS_PRESENT(thread_ctx_ptr) && (RemoveFromLockedObjectsStore(thread_ctx_ptr->ht_ptr, (void*)sesn_ptr) != 0)) {
    syslog(
      LOG_ERR,
      "SessionUnLock: (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p', func:'%s'): ERROR: LOCK NOT OWNED BY THIS THREAD: NOT RELEASING...",
      pthread_self(), sesn_ptr->session_id, sesn_ptr, thread_ctx_ptr, func);

    _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, RESCODE_PROG_WONTLOCK)
  }

  int lock_state = pthread_rwlock_unlock(&(sesn_ptr->session_events.rwlock));
  if (lock_state == 0) {
#if UFNET_DEBUG_BUILD
    syslog(
      LOG_DEBUG,
      "SessionUnLock: (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p', func:'%s): SUCCESS: RELEASED WRITE/READ lock for Session events...",
      pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0, func);
#endif
    _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_SUCCESS, RESCODE_PROG_LOCKED)
  }
  else {
    char *err_str = thread_error(errno);
    syslog(
      LOG_DEBUG,
      "SessionUnLock: (pid:'%lu', cid:'%lu', o:'%p', ctx:'%p', func:'%s'): ERROR: COULD NOT RELEASE WRITE/READ lock for Session events (errno='%d'): '%s'",
      pthread_self(), sesn_ptr->session_id, sesn_ptr, IS_PRESENT(thread_ctx_ptr) ? thread_ctx_ptr : 0, func, errno,
      err_str);
  }

  _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, RESCODE_PROG_WONTLOCK)
}

int
MessageQueueLock(UfsrvSession *sesn_ptr, MessageQueue *msg_que_ptr, int try_flag)
{
  int lock_state;

  if (try_flag) {
    lock_state = pthread_mutex_trylock(&(msg_que_ptr->mutex));

    if (lock_state == 0) {
#if UFNET_DEBUG_BUILD
      syslog(
        LOG_DEBUG,
        "%s: {pid'%lu', th_ctx:'%lu', cid:'%lu'  o:'%p'}: SUCCESS: MUTEX TRY-LOCK for MessageQueue events acquired...",
        __func__, pthread_self(), 0L, sesn_ptr->session_id, sesn_ptr);
#endif
    }
    else {
      char *err_str = thread_error(errno);
      syslog(
        LOG_DEBUG,
        "%s: {pid'%lu', th_ctx:'%lu', cid:'%lu'  o:'%p'}: ERROR: COULD NOT acquire MUTEX lock for MessageQueue (errno='%d'):  '%s'",
        __func__, pthread_self(), 0UL, sesn_ptr->session_id, sesn_ptr, errno, err_str);
    }
  }
  else {
    lock_state = pthread_mutex_lock(&(msg_que_ptr->mutex));
    if (lock_state == 0) {
#if UFNET_DEBUG_BUILD
      syslog(
        LOG_DEBUG, "%s: {pid'%lu', th_ctx:'%lu', cid:'%lu'  o:'%p'}: SUCCESS:  MUTEX lock for MessageQueue acquired...",
        __func__, pthread_self(), 0L, sesn_ptr->session_id, sesn_ptr);
#endif
    }
    else {
      char *err_str = thread_error(errno);
      syslog(
        LOG_DEBUG,
        "%s: {pid'%lu', th_ctx:'%lu', cid:'%lu'  o:'%p'}: ERROR: COULD NOT acquire MUTEX lock for MessageQueue (errno='%d'): '%s'",
        __func__, pthread_self(), 0UL, sesn_ptr->session_id, sesn_ptr, errno, err_str);
    }
  }

  return lock_state;
}

int
MessageQueueUnLock(UfsrvSession *sesn_ptr, MessageQueue *msg_que_ptr)
{
  int lock_state = pthread_mutex_unlock(&(msg_que_ptr->mutex));
  if (lock_state == 0) {
#if UFNET_DEBUG_BUILD
    syslog(
      LOG_DEBUG, "%s: {pid'%lu', th_ctx:'%lu', cid:'%lu'  o:'%p'}: SUCCESS: RELEASED MUTEX lock for MessageQueue...",
      __func__, pthread_self(), 0L, sesn_ptr->session_id, sesn_ptr);
#endif
  }
  else {
    char *err_str = thread_error(errno);
    syslog(
      LOG_DEBUG,
      "%s: {pid'%lu', th_ctx:'%lu', cid:'%lu', o:'%p'}: ERROR: COULD NOT RELEASE MUTEX lock for MessageQueue (errno='%d'):  '%s'",
      __func__, pthread_self(), 0UL, sesn_ptr->session_id, sesn_ptr, errno, err_str);
  }

  return lock_state;
}

static _Atomic(UfsrvSessionSuspensionPublisher) suspension_publisher;

void
UfsrvSessionSetSuspensionPublisher(UfsrvSessionSuspensionPublisher publisher)
{
  atomic_store(&suspension_publisher, publisher);
}

void
DestructSocketMessage(SocketMessage *message)
{
  if (message->processed_msg_size > 0) free(message->_processed_msg);
  if (message->raw_msg_size > 0) free(message->_raw_msg);
  if (message->holding_buffer_msg_size > 0) free(message->holding_buffer);
  if (message->frame_index_multiples > 0) free(message->frame_index);

  memset(message, 0, sizeof(*message));
}

void
DestructSocketMessageQueue(MessageQueue *queue)
{
  while (queue->queue.nEntries != 0) {
    QueueEntry *entry = deQueue(&queue->queue);
    DestructSocketMessage(entry->whatever);
    free(entry->whatever);
    free(entry);
  }
}

int
UfsrvSessionSuspendSoft(InstanceHolderForSession *holder)
{
  UfsrvSession *session = AS_UFSRV_SESSION(GetInstance(holder));
  if (SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SUSPENDED)) {
    syslog(LOG_DEBUG, LOGSTR_INCONSISTENT_STATE, __func__, pthread_self(), session, session->session_id,
           RESCODE_PROG_INCONSISTENT_STATE, "Session already suspended (proceeding anyway).");
  }

  SESNSTATUS_SET(session->stat, SESNSTATUS_SUSPENDED);
  SESNSTATUS_UNSET(session->stat, SESNSTATUS_CONNECTED);
  session->when_suspended = time(NULL);

  const Protocol *protocol = UfsrvSessionProtocol(session);
  if (protocol && protocol->protocol_callbacks.session_reset_callback) {
    protocol->protocol_callbacks.session_reset_callback(holder, SOFT_SUSPENSE);
  }

  int incoming_locked = MessageQueueLock(session, &session->message_queue_in, 0) == 0;
  const bool worker_owns_socket = SESNSTATUS_IS_SET(session->stat, SESNSTATUS_WORKER_OWNS_SOCKET);
  if (!worker_owns_socket && session->ssptr->sock > 0) {
    shutdown(session->ssptr->sock, SHUT_RDWR);
    RemoveSessionToMonitoredWorkEvents(holder);
    close(session->ssptr->sock);
    session->ssptr->sock = -1;
  }
  if (incoming_locked) {
    if (!worker_owns_socket) session->ssptr->sock = -1;
    DestructSocketMessageQueue(&session->message_queue_in);
    MessageQueueUnLock(session, &session->message_queue_in);
  }

  if (MessageQueueLock(session, &session->message_queue_out, 1) == 0) {
    DestructSocketMessageQueue(&session->message_queue_out);
    MessageQueueUnLock(session, &session->message_queue_out);
  }

  UfsrvSessionSuspensionPublisher publish = atomic_load(&suspension_publisher);
  if (publish && protocol && protocol->protocol_controls.pub_session_transitions) publish(holder);

  return 1;
}

/**
 * @brief A generic Session instantiator that is unrelated to the underlying recycler subsystem lifecycle or protocol
 * lifecycle.
 * param protocol_id: >=0 protocol id which can be used as index. If -1 no protocol is associated with this session
 */
UfsrvSession *
InstantiateUfsrvSession(Socket *ssptr, Socket *dsptr, unsigned call_flags, int protocol_id)
{
  UfsrvSession *sesn_ptr = calloc(1, sizeof(UfsrvSession));

	if (protocol_id > 0) {
	  if ((sesn_ptr->session_id = GenerateSessionId(protocol_id, &sesn_ptr->result, NULL)) == 0)	goto final_clean_up;
	}

	pthread_rwlockattr_init(&(sesn_ptr->session_events.rwattr));
	int rc = pthread_rwlock_init(&(sesn_ptr->session_events.rwlock), &(sesn_ptr->session_events.rwattr));//==0 on success
	if (rc == 0) {
		if (ssptr) {
			//TODO: consider using local variable
			pthread_mutexattr_t attr;

			//Note: using adaptive mutex changes the error reporting behaviour in lock/unlock it would appears multipe locks acquired at the same time
			pthread_mutexattr_init(&(sesn_ptr->message_queue_in.mutex_attr));
			pthread_mutexattr_settype(&(sesn_ptr->message_queue_in.mutex_attr), PTHREAD_MUTEX_ADAPTIVE_NP);

			if ((pthread_mutex_init (&(sesn_ptr->message_queue_in.mutex), 	&(sesn_ptr->message_queue_in.mutex_attr))) != 0) {
				syslog(LOG_ERR, "%s (pid:'%lu', errno:'%d'): ERROR: COULD NOT INITIALISE MUTEX FOR INCOMING SOCKETMESSAGE QUEUE...", __func__, pthread_self(), errno);
				pthread_mutexattr_destroy(&(sesn_ptr->message_queue_in.mutex_attr));

				goto final_clean_up;
			}

			pthread_mutexattr_destroy(&(sesn_ptr->message_queue_in.mutex_attr));

			///////////////

			pthread_mutexattr_init(&(sesn_ptr->message_queue_out.mutex_attr));
			pthread_mutexattr_settype(&(sesn_ptr->message_queue_out.mutex_attr), PTHREAD_MUTEX_ADAPTIVE_NP);

			if ((pthread_mutex_init (&(sesn_ptr->message_queue_out.mutex), 	&(sesn_ptr->message_queue_out.mutex_attr))) != 0) {
				syslog(LOG_ERR, "%s (pid:'%lu' errno:'%d'): ERROR: COULD NOT INITIALISE MUTEX FOR OUTGOING SOCKETMESSAGE QUEUE...", __func__, pthread_self(), errno);
				pthread_mutexattr_destroy(&(sesn_ptr->message_queue_out.mutex_attr));

				goto final_clean_up;
			}

			pthread_mutexattr_destroy(&(sesn_ptr->message_queue_out.mutex_attr));

			sesn_ptr->ssptr = ssptr;
		}

		if (dsptr)	sesn_ptr->dsptr = dsptr;


		//invoke protocol session specific initialisation for this session
    if (protocol_id >= 0) {
      //1) assign static protocol type data
      UFSRV_SESSION_PROTOCOLTYPE(sesn_ptr) = (ProtocolTypeData *)ProtocolGet(protocol_id);
    }

		return sesn_ptr;
	} else {
		char error_str[250];
		strerror_r(errno, error_str, 250);

		syslog(LOG_ERR, "%s (pid:'%lu'): ERROR (errno='%d' str='%s'): COULD NOT INITIALISE MUTEX", __func__, pthread_self(), errno, error_str);

		goto final_clean_up;
	}

	mutex_clean_up:
	pthread_rwlockattr_destroy(&(sesn_ptr->session_events.rwattr));
	pthread_mutexattr_destroy(&(sesn_ptr->message_queue_in.mutex_attr));
	pthread_mutexattr_destroy(&(sesn_ptr->message_queue_out.mutex_attr));

	pthread_rwlock_destroy(&(sesn_ptr->session_events.rwlock));
	pthread_mutex_destroy(&(sesn_ptr->message_queue_in.mutex));
	pthread_mutex_destroy(&(sesn_ptr->message_queue_out.mutex));

	goto final_clean_up;

	//
	final_clean_up:
	free(sesn_ptr);

	return NULL;

}  /**/

  unsigned long
  GenerateSessionIdLocally(void)
	{
	  return GenerateRandomNumber();

	}


  /**
   * @brief A jump function to invoke protocol sensitve session id generation
   * @param protocol_id
   * @param res_ptr Result carrier
   * @param context_data User context data
   * @return
   */
unsigned long
GenerateSessionId(int protocol_id, UFSRVResult *result, ClientContextData *context)
{
  return protocol_id < 0 ? 0 : UfsrvGenerateSessionIdForProtocol(ProtocolGet(protocol_id), result, context);
}

unsigned long
UfsrvGenerateSessionIdForProtocol(const Protocol *protocol, UFSRVResult *result, ClientContextData *context)
{
  if (!protocol || !protocol->protocol_callbacks.generate_session_id_callback) return 0;
  result = protocol->protocol_callbacks.generate_session_id_callback(result, context);
  return result && _RESULT_TYPE_SUCCESS(result) ? (unsigned long)(uintptr_t)_RESULT_USERDATA(result) : 0;
}

InstanceHolderForSession *
UfsrvSessionCheckout(const Protocol *protocol, unsigned long flags)
{
  if (!protocol || !protocol->session_pool) return NULL;
  UfsrvSessionCheckoutContext context = {.protocol = protocol};
  return RecyclerGet(protocol->session_pool->type, &context, flags);
}

int
UfsrvSessionReturnToRecycler(InstanceHolderForSession *holder, ContextData *context, unsigned long flags)
{
  unsigned pool_type = UfsrvSessionOwnerPoolType(holder);
  if (!pool_type) return -1;

  int rc = RecyclerPut(pool_type, holder, context, flags);
  if (rc == -3) {
    const Protocol *protocol = UfsrvSessionProtocol(GetInstance(holder));
    if (protocol && protocol->protocol_callbacks.recycler_error_callback) {
      protocol->protocol_callbacks.recycler_error_callback(holder, CALLFLAGS_EMPTY);
    }
  }
  return rc;
}

SessionStorageLayout
UfsrvSessionGetLayout(void)
{
  return (SessionStorageLayout){.size = sizeof(UfsrvSession), .alignment = _Alignof(UfsrvSession)};
}
