/**
 * @file sessions_utils.h
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

#ifndef UFSRVCORELIB_SESSIONS_UTILS_H
#define UFSRVCORELIB_SESSIONS_UTILS_H

#include <ufsrvcorelib_defs.h>

#include <uflib/recycler/recycler.h>
#include <uflib/recycler/instance_type.h>

#include <ufsrv_core/delegator_sessions/sessions_delegator_type.h>

#include <ufsrv_core/session/sesstion_stat_flags.h>
#include <ufsrv_core/session/ufsrv_session_type.h>
#include <ufsrv_core/ufsrvresult_type.h>

PUBLIC_API UFSRVResult *SessionLockRDCtx(ThreadContext *, UfsrvSession *, int try_flag);
PUBLIC_API UFSRVResult *SessionLockRWCtx(ThreadContext *, UfsrvSession *, int try_flag, const char *func);
PUBLIC_API UFSRVResult *SessionUnLockCtx(ThreadContext *, UfsrvSession *, const char *func);
PUBLIC_API int         MessageQueueLock(UfsrvSession *, MessageQueue *msg_que_ptr, int try_flag);
PUBLIC_API int         MessageQueueUnLock(UfsrvSession *, MessageQueue *msg_que_ptr);

#define SOFT_SUSPENSE 0

typedef void (*UfsrvSessionSuspensionPublisher)(InstanceHolderForSession *);

/**
 * @brief Register the consumer's publisher for soft suspension notifications.
 * @param publisher Called under the session lock after transport cleanup, or NULL.
 * @code{.c}
 * UfsrvSessionSetSuspensionPublisher(PublishSuspendedSession);
 * @endcode
 */
PUBLIC_API void UfsrvSessionSetSuspensionPublisher(UfsrvSessionSuspensionPublisher publisher);

/**
 * @brief Soft-suspend a session while the caller holds its session lock.
 * @param holder Session instance to suspend.
 * @return 1 after completing the suspension.
 * @code{.c}
 * UfsrvSessionSuspendSoft(holder);
 * @endcode
 */
PUBLIC_API int UfsrvSessionSuspendSoft(InstanceHolderForSession *holder);

/**
 * @brief Release owned message buffers and clear the message.
 * @code{.c}
 * DestructSocketMessage(&message);
 * @endcode
 */
PUBLIC_API void DestructSocketMessage(SocketMessage *message);

/**
 * @brief Destroy the messages and entries of a caller-locked socket queue.
 * @code{.c}
 * DestructSocketMessageQueue(&session->message_queue_in);
 * @endcode
 */
PUBLIC_API void DestructSocketMessageQueue(MessageQueue *queue);

PUBLIC_API UfsrvSession *InstantiateUfsrvSession(Socket *ssptr, Socket *dsptr, unsigned call_flags, int protocol_id);

PUBLIC_API unsigned long GenerateSessionId(int protocol_id, UFSRVResult *res_ptr, ClientContextData *context_data);
PUBLIC_API unsigned long GenerateSessionIdLocally(void);

typedef struct Protocol Protocol;
typedef struct {
  const Protocol *protocol;
} UfsrvSessionCheckoutContext;

/**
 * @brief Check out a session using the pool and lifecycle of a resolved protocol.
 * @code{.c}
 * InstanceHolderForSession *holder = UfsrvSessionCheckout(listener->protocol, CALL_FLAG_HASH_SESSION_LOCALLY);
 * @endcode
 */
PUBLIC_API InstanceHolderForSession *UfsrvSessionCheckout(const Protocol *protocol, unsigned long flags);

/**
 * @brief Generate a session ID through the selected protocol's callback.
 * @code{.c}
 * unsigned long id = UfsrvGenerateSessionIdForProtocol(protocol, result, NULL);
 * @endcode
 */
PUBLIC_API unsigned long UfsrvGenerateSessionIdForProtocol(const Protocol *protocol, UFSRVResult *result,
                                                          ClientContextData *context);

/*! @brief Read the bound protocol of a session. */
static inline const Protocol *
UfsrvSessionProtocol(const UfsrvSession *session)
{
  return (const Protocol *)session->protocol_type_data;
}

/*! @brief Return the pool type recorded on a checked-out session. */
static inline unsigned
UfsrvSessionOwnerPoolType(InstanceHolderForSession *holder)
{
  const UfsrvSession *session = GetInstance(holder);
  return session->recycler_pool ? session->recycler_pool->type : 0;
}

/**
 * @brief Return a session to its recycler and notify its protocol on a recycler error.
 * @code{.c}
 * UfsrvSessionReturnToRecycler(holder, NULL, CALL_FLAG_HASH_SESSION_LOCALLY);
 * @endcode
 */
PUBLIC_API int UfsrvSessionReturnToRecycler(InstanceHolderForSession *holder, ContextData *context, unsigned long flags);

/*!
 * @brief Take a reference on a pooled session.
 *
 * The pool type number is the one recorded on the checked-out session.
 */
static inline void
UfsrvSessionReference(InstanceHolder *instance_holder_ptr, int multiples)
{
  RecyclerTypeReferenced(UfsrvSessionOwnerPoolType(instance_holder_ptr), instance_holder_ptr, multiples);
}

/*!
 * @brief Release a reference on a pooled session.
 *
 * The object returns to the pool when the count reaches zero, through the
 * poolop_initput_callback the consumer registered.
 */
static inline void
UfsrvSessionUnreference(InstanceHolder *instance_holder_ptr, int multiples)
{
  RecyclerTypeUnReferenced(UfsrvSessionOwnerPoolType(instance_holder_ptr), instance_holder_ptr, multiples);
}

#endif
