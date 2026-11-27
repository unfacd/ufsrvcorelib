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

#ifndef UFSRVCORELIB_UDP_SERVER_H
#define UFSRVCORELIB_UDP_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <ufsrv_core/delegator_sessions/thread_context_type.h>
#include <ufsrv_core/protocol/protocol_type.h>

typedef struct UdpServer UdpServer;

/**
 * Process one datagram on a shard worker. Return the reply length, or zero
 * to drop it. Input, peer and reply storage are borrowed for this call.
 */
typedef size_t (*UdpDatagramCallback)(const struct sockaddr *peer, socklen_t peer_len, const unsigned char *input,
                                      size_t                 input_len, unsigned char *reply, size_t reply_capacity);

/** Return true to dispatch a datagram. Called concurrently by shard threads. */
typedef bool (*UdpDatagramAdmissionCallback)(UdpServer *server, const struct sockaddr *peer, socklen_t peer_len,
                                             const unsigned char *input, size_t input_len);

/** Copyable identifier valid only with the server that issued it. */
typedef struct UdpFlowHandle
{
  size_t   shard;
  size_t   slot;
  uint64_t generation;
} UdpFlowHandle;

/** Deliver a datagram with a flow handle that can be used for later sends. */
typedef size_t (*UdpFlowDatagramCallback)(UdpServer *    server, UdpFlowHandle flow, const struct sockaddr *peer,
                                          socklen_t      peer_len, const unsigned char *input, size_t input_len,
                                          unsigned char *reply, size_t reply_capacity);

/**
 * Release one attached session reference on the owning shard, outside its mutex.
 * The hook must return promptly and must not stop the server.
 */
typedef void (*UdpFlowSessionReleaseCallback)(UdpFlowHandle flow, InstanceHolder *session);

typedef struct UdpServerSpec
{
  size_t shard_count; ///< Shard threads to start; fixed for the life of the process.
  size_t workers_per_shard; ///< Worker threads that may run each shard's datagrams.
  const char *bind_host; ///< Numeric host or name to bind; resolved during start.
  unsigned port; ///< UDP port to bind, 1..65535.
  protocol_id_t protocol_id; ///< Protocol this endpoint serves, as registered by the host.
  UfsrvUserData *worker_slots; ///< Caller-owned contiguous worker slots, stride slot_size.
  void *protocol_context_data; ///< Host instance state, returned by UdpServerProtocolContext().
  size_t slot_count; ///< Slots in worker_slots; must equal shard_count * workers_per_shard.
  size_t slot_size; ///< Stride in bytes between worker slots.
  UdpDatagramAdmissionCallback admit_datagram; ///< Optional plaintext admission check before dispatch.
  UdpDatagramCallback on_datagram; ///< Stateless datagram handler.
  void (*on_worker_start)(ThreadContext *); ///< Hook run after a worker installs its TLS.
  UdpFlowDatagramCallback on_flow_datagram; ///< Alternative to on_datagram.
  size_t flows_per_shard; ///< Bounded peer table for flow mode.
  unsigned flow_idle_timeout_seconds; ///< Idle time before a handle expires.
  size_t sends_per_shard; ///< Maximum queued asynchronous sends.
  UdpFlowSessionReleaseCallback on_flow_session_release; ///< Required for session attachment.
  const char *dtls_chain_file; ///< Enable DTLS in flow mode with a PEM chain.
  const char *dtls_key_file; ///< PEM private key for the chain.
  size_t shard_hash_table_startup_size; ///< First peer index allocation size, zero uses the hash table default.
} UdpServerSpec;

PUBLIC_API UdpServer *UdpServerStart(const UdpServerSpec *spec);
/**
 * Copy a payload into the owning shard's queue. False means the flow has
 * expired, the queue is full, or the payload could not be queued. Success
 * means queued, and delivery is best effort. Stop external producers before
 * calling UdpServerStop.
 */
PUBLIC_API bool       UdpServerQueueSend(UdpServer *server, UdpFlowHandle flow, const void *payload, size_t length);
/**
 * Set a flow's lease to a future CLOCK_MONOTONIC deadline, or clear it with
 * NULL. A live lease keeps the flow usable beyond its idle timeout.
 * Closed, expired, stale, and stopping flows reject updates. Callers must
 * serialize updates for the same allocation and stop external producers
 * before destroying the server.
 *
 * @code{.c}
 * struct timespec deadline;
 * if (clock_gettime(CLOCK_MONOTONIC, &deadline) == 0) {
 *   deadline.tv_sec += allocation_lifetime;
 *   if (UdpServerSetFlowLease(server, flow, &deadline)) {
 *     // Publish the allocation with the same deadline.
 *   }
 * }
 * @endcode
 */
PUBLIC_API bool       UdpServerSetFlowLease(UdpServer *server, UdpFlowHandle flow, const struct timespec *deadline);

typedef void (*UdpFlowLeaseExpiryCallback)(UdpFlowHandle flow, InstanceHolder *session);

/**
 * Register one notification for a leased flow with an attached session.
 * The owning shard invokes it outside its mutex, between datagram callbacks,
 * when the lease expires and the flow remains active. If the flow closes
 * instead, the session release callback handles cleanup. Replacing or
 * clearing the lease replaces or clears the notification.
 */
PUBLIC_API bool UdpServerSetFlowLeaseWithExpiry(UdpServer *server, UdpFlowHandle flow, const struct timespec *deadline,
                                                UdpFlowLeaseExpiryCallback on_expiry);

/**
 * Attach one host-owned recycler reference during this flow's worker callback.
 * Success transfers the reference to the flow. Failure leaves it with the caller.
 * The flow releases it on expiry or shutdown through on_flow_session_release.
 */
PUBLIC_API bool UdpServerAttachSession(UdpServer *server, UdpFlowHandle flow, InstanceHolder *session);

/** Borrow the attached session for the duration of this flow's worker callback. */
PUBLIC_API InstanceHolder *UdpServerFlowSession(UdpServer *server, UdpFlowHandle flow);

PUBLIC_API void UdpServerStop(UdpServer *server);

/** The opaque instance state the host attached through UdpServerSpec.protocol_context_data. */
PUBLIC_API void *UdpServerProtocolContext(UdpServer *server);

#endif
