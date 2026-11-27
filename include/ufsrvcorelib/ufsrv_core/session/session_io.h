/**
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file session_io.h
 * @brief Session socket transport interfaces.
 */
#ifndef UFSRVCORELIB_SESSION_IO_H
#define UFSRVCORELIB_SESSION_IO_H

#include <sys/types.h>

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <ufsrvcorelib/ufsrv_core/session/transmission_message_type.h>
#include <ufsrvcorelib/ufsrv_core/session/ufsrv_session_type.h>

#define SOCKMSG_SMERRNO_PEERCLOSED (-2)

/**
 * @brief Read or decode incoming session data according to the socket-message flags.
 * @code{.c}
 * ssize_t bytes = ReadFromSocket(holder, &message, SOCKMSG_READSOCKET);
 * @endcode
 */
PUBLIC_API ssize_t ReadFromSocket(InstanceHolderForSession *holder, SocketMessage *message, unsigned flags);

/**
 * Read from a socket exclusively owned by the caller, using its SocketMessage
 * for raw bytes, optional frame reassembly, and decoded output. No queue
 * operations are performed. The caller consumes and clears any previous output
 * before reading again, and owns connection teardown on failure.
 * Supports SOCKMSG_READSOCKET and optional SOCKMSG_DONTDECODE.
 * Returns positive bytes on success, zero on retry or incomplete frame,
 * -1 on EOF or I/O failure, or a negative allocation/decode status.
 */
PUBLIC_API ssize_t ReadFromSocketNonQueued(InstanceHolderForSession *holder, SocketMessage *message, unsigned flags);

/**
 * Write the entire buffer to a worker-owned socket, using TCP or TLS as configured.
 * Borrows data for the duration of the call. An incomplete write returns -1 and
 * marks the connection failed so the owning worker can close it.
 */
PUBLIC_API ssize_t WriteToSocketNonQueued(InstanceHolderForSession *holder, const void *data, size_t length);

/**
 * @brief Queue a raw socket read while the caller holds the incoming queue lock.
 * @code{.c}
 * ssize_t bytes = ReadIncomingQueueLocked(session);
 * @endcode
 */
PUBLIC_API ssize_t ReadIncomingQueueLocked(UfsrvSession *session);

/**
 * @brief Coalesce incoming queue entries into one socket message.
 * @details The caller holds the session lock and either holds the incoming queue
 * lock or passes SOCKMSG_LOCK_SOCKMSGQUEUE.
 * @code{.c}
 * UFSRVResult result;
 * ConsolidateSocketMessageQueue(session, SOCKMSG_CONSOLIDATE_INSESSION | SOCKMSG_LOCK_SOCKMSGQUEUE, &result);
 * @endcode
 */
PUBLIC_API UFSRVResult *ConsolidateSocketMessageQueue(UfsrvSession *session, unsigned flags, UFSRVResult *result);

/**
 * @brief Handle a socket error and soft-suspend the caller-locked session.
 * @code{.c}
 * ErrorFromSocket(holder, SOCKMSG_CONSOLIDATE_INSESSION | SOCKMSG_LOCK_SOCKMSGQUEUE);
 * @endcode
 */
PUBLIC_API void ErrorFromSocket(InstanceHolderForSession *holder, unsigned flags);

/**
 * @brief Resume queued writes in FIFO order under the session lock.
 * @param entries Maximum messages to drain, or zero to drain all available messages.
 * @return 1 after draining the requested entries, 0 if empty or blocked, -1 on I/O error.
 * @code{.c}
 * int result = DispatchSocketMessageQueue(holder, 0);
 * @endcode
 */
PUBLIC_API int DispatchSocketMessageQueue(InstanceHolderForSession *holder, size_t entries);

/**
 * @brief Send a prepared payload while the caller holds the session lock.
 * @return Bytes written immediately, 0 if queued, or a negative error code.
 * @code{.c}
 * TransmissionMessage message = {.type = TRANSMSG_TEXT, .len = length, .msg_packed = owned_buffer};
 * ssize_t result = SendToSocket(holder, &message, SOCKMSG_DONTWSFRAME);
 * @endcode
 */
PUBLIC_API ssize_t SendToSocket(InstanceHolderForSession *holder, TransmissionMessage *message, unsigned flags);

/**
 * @brief Send an owned copy of a text buffer without protocol framing.
 * @code{.c}
 * int result = SendTextMessage(holder, "hello", 5);
 * @endcode
 */
PUBLIC_API int SendTextMessage(InstanceHolderForSession *holder, const char *text, size_t length);

#endif
