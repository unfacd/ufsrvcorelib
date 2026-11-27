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
 * @file udp_server_priv_type.h
 * @brief Private UDP shard, peer flow, and worker handoff types.
 */

#ifndef UFSRVCORELIB_UDP_SERVER_PRIV_TYPE_H
#define UFSRVCORELIB_UDP_SERVER_PRIV_TYPE_H

#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <time.h>

#include <openssl/ssl.h>
#include <uflib/adt/adt_hashtable_type.h>

#include <ufsrv_core/udp_server/udp_server.h>

enum { UDP_BUFFER_SIZE = 65535 }; ///< Capacity of each shard's input and reply buffers.

/*! @brief State of a shard's single worker handoff, protected by the shard mutex. */
typedef enum UdpWorkState
{
  UDP_WORK_IDLE,    ///< Buffers are available to the shard.
  UDP_WORK_PENDING, ///< Input is ready for one worker to claim.
  UDP_WORK_RUNNING, ///< The claiming worker is executing the protocol callback.
  UDP_WORK_DONE     ///< The worker has published the reply length.
} UdpWorkState;

/*!
 * @brief Fixed-width peer key within one shard's local endpoint and transport.
 *
 * Every byte is initialized before lookup. The key stays fixed while indexed.
 */
typedef struct UdpPeerKey
{
  unsigned char family;      ///< Address family discriminator, encoded as 4 or 6.
  unsigned char port[2];     ///< Peer UDP port in network byte order.
  unsigned char scope[4];    ///< IPv6 scope ID bytes, zero for IPv4.
  unsigned char address[16]; ///< Peer address bytes, zero-padded for IPv4.
} UdpPeerKey;

/*!
 * @brief A peer slot owned by a shard, with optional DTLS and recycler state.
 *
 * The shard owns index changes and OpenSSL I/O. Its mutex protects slot
 * identity and lifetime checks shared with workers and send producers.
 */
typedef struct UdpFlow
{
  UdpPeerKey key;               ///< Lookup key stored inline for the shard index.
  bool indexed;                 ///< Whether the peer index holds this slot.
  struct sockaddr_storage peer; ///< Socket address used for outbound datagrams.
  socklen_t peer_len;           ///< Length of the stored peer socket address.
  uint64_t generation;           ///< Slot incarnation used to reject stale handles, zero before first use.
  struct timespec last_seen;     ///< Monotonic timestamp used to compute idle expiry.
  struct timespec lease_deadline; ///< Absolute monotonic deadline, zero when unleased.
  UdpFlowLeaseExpiryCallback on_lease_expiry;
  InstanceHolder *session;     ///< Optional recycler reference owned by the flow.
  SSL *dtls;                    ///< Optional DTLS association driven only by the shard.
  bool closed;                  ///< Whether this incarnation has been retired.
} UdpFlow;

/*! @brief Outbound datagram whose payload is copied into a shard-owned queue. */
typedef struct UdpSend
{
  struct UdpSend *next;     ///< Next queued send, or NULL at the tail.
  UdpFlowHandle flow;       ///< Destination flow and generation to check before sending.
  size_t length;            ///< Number of bytes stored in the payload.
  unsigned char payload[]; ///< Inline plaintext bytes released with the queue node.
} UdpSend;

/*!
 * @brief Owns a UDP socket, its peer index, and the work assigned to its worker group.
 *
 * The shard thread drives the socket and mutates the peer index. The mutex
 * protects the work handoff, shared flow state, and outbound queue.
 */
typedef struct UdpShard
{
  size_t                  index;                  ///< Shard position in the server's shard array.
  int                     fd;                     ///< Bound UDP socket descriptor, or -1 before binding.
  _Atomic unsigned        up;                     ///< Run flag cleared by server shutdown.
  pthread_mutex_t         mutex;                  ///< Guards the handoff, shared flow state, and send queue.
  pthread_cond_t          wake;                   ///< Signals thread readiness, work, completion, or shutdown.
  UdpWorkState            state;                  ///< State of the current datagram handoff.
  struct sockaddr_storage peer;                   ///< Source address of the current input datagram.
  socklen_t               peer_len;               ///< Length of the current source address.
  size_t                  input_len;              ///< Number of input bytes published to the worker.
  size_t                  reply_len;              ///< Number of reply bytes published by the worker.
  UdpFlowHandle           flow;                   ///< Flow handle for the current handoff in flow mode.
  pthread_t               active_worker;          ///< Thread that owns the running callback, valid in UDP_WORK_RUNNING.
  UdpFlow *               flows;                  ///< Fixed array of peer slots, allocated in flow mode.
  HashTable *             peer_index;              ///< Shard-local map from peer keys to flow slots.
  int                     send_wake;              ///< Event descriptor waking poll for queued sends, or -1 outside flow mode.
  UdpSend *               send_head;              ///< First queued outbound datagram.
  UdpSend *               send_tail;              ///< Last queued outbound datagram.
  size_t                  send_count;             ///< Queued sends plus any send currently being drained.
  unsigned char           input[UDP_BUFFER_SIZE]; ///< Datagram receive buffer, reused for decrypted input in DTLS mode.
  unsigned char           reply[UDP_BUFFER_SIZE]; ///< Reply buffer filled by the claiming worker.
} UdpShard;

/*! @brief Stable thread startup data retained until the thread is joined. */
typedef struct UdpThreadStart
{
  WorkerThreadCreationContext creation; ///< Worker index and host-owned TLS slot.
  UdpShard *                  shard;    ///< Shard assigned to this thread.
  UdpServer *                 server;   ///< Server that owns the shard and thread.
  bool                        ready;    ///< Startup acknowledgement guarded by the shard mutex.
} UdpThreadStart;

/*! @brief Owns the UDP shards, their threads, and the configured dispatch hooks. */
struct UdpServer
{
  UdpShard *                    shards;                       ///< Array of shard state owned by the server.
  void (*                       on_worker_start)(ThreadContext *); ///< Optional hook called after the worker installs TLS.
  pthread_t *                   shard_threads;                ///< Thread IDs for successfully started shards.
  pthread_t *                   worker_threads;               ///< Thread IDs for successfully started workers.
  UdpThreadStart *              shard_starts;                 ///< Startup arguments retained for shard threads.
  UdpThreadStart *              worker_starts;                ///< Startup arguments retained for worker threads.
  UdpDatagramAdmissionCallback  admit_datagram;                ///< Optional plaintext admission hook run on the shard.
  void *                        protocol_context_data;         ///< Opaque instance state handed back to the host.
  UdpDatagramCallback           on_datagram;                   ///< Worker callback for stateless datagrams.
  SSL_CTX *                     dtls_context;                  ///< Shared server TLS context, owned until all shards join.
  UdpFlowDatagramCallback       on_flow_datagram;              ///< Worker callback for datagrams with a flow handle.
  UdpFlowSessionReleaseCallback on_flow_session_release;       ///< Host hook that releases a recycler reference on the shard.
  size_t                        flows_per_shard;              ///< Number of allocated peer slots per shard in flow mode.
  unsigned                      flow_idle_timeout_seconds;    ///< Idle lifetime applied to peer flows.
  size_t                        sends_per_shard;              ///< Maximum outstanding asynchronous sends per shard.
  size_t                        shards_initialized;            ///< Shards whose synchronization and resources can be cleaned up.
  size_t                        shards_started;                ///< Number of shard threads to join, including after partial startup.
  size_t                        workers_started;               ///< Number of worker threads to join, including after partial startup.
};

#endif
