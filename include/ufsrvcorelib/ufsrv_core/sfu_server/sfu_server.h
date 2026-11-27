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
 * @file sfu_server.h
 * @brief One thread per router, each bound to its own UDP port.
 */

#ifndef UFSRVCORELIB_SFU_SERVER_H
#define UFSRVCORELIB_SFU_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <ufsrvcorelib/ufsrv_core/sfu_server/model/command_type.h>

typedef struct SfuServer SfuServer;
typedef struct SfuShardSet SfuShardSet;

typedef enum {
  SFU_EVENT_EGRESS_DISABLED = 0,
  SFU_EVENT_EGRESS_UNIX_SOCKET,
  SFU_EVENT_EGRESS_TCP_STREAM,
  SFU_EVENT_EGRESS_HTTP_WEBHOOK,
} SfuEventEgressMode;

/** Server-wide startup settings. Onboarding copies any transport strings. */
typedef struct SfuEventEgressSpec {
  SfuEventEgressMode mode;
  size_t queue_capacity;
  size_t connect_timeout_ms;
  size_t write_timeout_ms;
  size_t reconnect_backoff_ms;
  const char *unix_socket_path;
  const char *webhook_endpoint;
  const char *webhook_auth_token;
  size_t webhook_batch_max_events;
  size_t webhook_batch_linger_ms;
  size_t webhook_timeout_ms;
  size_t webhook_max_retries;
  bool emit_peer_connected;
  bool emit_peer_disconnected;
  bool emit_dominant_speaker;
  bool emit_publication_added;
  bool emit_publication_removed;
  bool emit_ice_restart_succeeded;
  bool emit_ice_restart_failed;
  bool emit_room_idle;
  bool emit_event_delivery_gap;
  size_t ice_consent_timeout_ms;
  size_t dominant_speaker_hold_ms;
  size_t dominant_speaker_min_interval_ms;
  int dominant_speaker_silence_dbov;
} SfuEventEgressSpec;

/**
 * Host-supplied contract. Capacity fields are the resolved configuration; a
 * zero lets onboarding fall back to its documented default so a caller that
 * only cares about binding can leave them out.
 */
typedef struct SfuServerSpec {
  size_t router_count;    ///< One thread, one UDP port.
  const char *bind_host;
  unsigned base_port;     ///< Shard i binds base_port + i.

  unsigned recv_batch;      ///< datagrams per recvmmsg
  unsigned send_batch;      ///< messages per sendmmsg
  unsigned rcvbuf_bytes;    ///< SO_RCVBUF request
  unsigned sndbuf_bytes;    ///< SO_SNDBUF request
  bool     reuseport;       ///< SO_REUSEPORT
  bool     path_mtu_discover; ///< IP(V6)_MTU_DISCOVER = DO
  unsigned busy_poll_us;    ///< SO_BUSY_POLL, 0 disables
  size_t   max_shards;      ///< hard ceiling on router_count
  unsigned poll_ms;         ///< shard idle wake granularity
  unsigned command_pool;    ///< signaller commands in flight per shard
  unsigned slab_packets;    ///< packets per shard slab
  unsigned peer_capacity;   ///< peer table entries per shard
  unsigned rooms_max;       ///< concurrent rooms per shard; 0 uses the default
  unsigned queue_depth;     ///< cross-shard SPSC slots
  unsigned peers_idle_timeout_seconds; ///< un-nominated peer expiry; 0 uses the default
  unsigned ice_consent_timeout_seconds; ///< consent freshness window; 0 uses the default
  unsigned rtcp_interval_seconds;       ///< RR cadence toward publishers; 0 uses the default
  unsigned rtcp_pli_min_interval_ms;    ///< per-publisher PLI throttle; 0 uses the default
  unsigned nack_retransmit_per_second;  ///< per-subscription NACK retransmits; 0 uses the default
  SfuEventEgressSpec events; ///< Zero initialization disables event egress.
} SfuServerSpec;

/**
 * Seam between the process main thread and shard context creation: validate the
 * server contract, build the spec array indexed by shard count, bind each
 * shard's socket and wake descriptor, allocate its slab, batch vectors, command
 * pool and counters, wire the cross-shard queues and put the packets on the
 * free list. No thread is created here.
 *
 * Returns NULL with nothing left allocated when the contract is unusable or any
 * allocation, bind or descriptor creation fails.
 *
 * @code{.c}
 * SfuServerSpec spec = {.router_count = 2, .bind_host = "127.0.0.1", .base_port = 40000};
 * SfuShardSet *shards = SfuShardOnboard(&spec);
 * SfuServer *server = SfuServerStart(shards);
 * @endcode
 */
PUBLIC_API SfuShardSet *SfuShardOnboard(const SfuServerSpec *spec);

/**
 * Release a set produced by SfuShardOnboard() that never reached SfuServerStart(),
 * or that start rejected. A set handed to a started server is released by
 * SfuServerStop(). NULL is safe.
 */
PUBLIC_API void SfuShardOffboard(SfuShardSet *set);

/**
 * Start one thread per shard context. The server takes the set: SfuServerStop()
 * joins the threads and releases both the set and the server handle.
 *
 * Returns NULL when the set is unusable or a thread cannot be created; threads
 * already started are stopped and joined, and the set is released, before
 * returning.
 */
PUBLIC_API SfuServer *SfuServerStart(SfuShardSet *shards);

/**
 * Deliver one signalling command from the control plane to the shard that owns
 * it. Room setup only: once a room exists, media never passes through here.
 *
 * False means the server is stopping, the shard index is outside the set, or
 * the shard's bounded command pool is exhausted.
 *
 * @code{.c}
 * SfuCommand command = {.type = SFU_COMMAND_CREATE_ROOM, .shard_index = 0};
 * SfuServerSignalCommand(server, &command);
 * @endcode
 */
PUBLIC_API bool SfuServerSignalCommand(SfuServer *server, const SfuCommand *command);

/**
 * Provision one ICE peer on a shard: the signalling layer owns ufrag/password
 * generation and passes them here before it hands the SDP answer out. The shard
 * returns true only after admission. Duplicate ufrag, capacity exhaustion,
 * and failure to complete the request return false.
 */
PUBLIC_API bool SfuServerRegisterPeer(SfuServer *server, size_t shard_index, uint64_t peer_id,
                                      const char *ufrag, const char *password,
                                      const uint8_t *remote_fingerprint);

/**
 * Create a room on @p shard_index. The room is transient media state owned by
 * that shard; duplicate ids and a full room pool are rejected asynchronously
 * (the shard counts the failure).
 */
PUBLIC_API bool SfuServerCreateRoom(SfuServer *server, size_t shard_index, uint64_t room_id);

/** Attach a registered peer (by ufrag) to a room. A peer belongs to one room. */
PUBLIC_API bool SfuServerJoinRoom(SfuServer *server, size_t shard_index, uint64_t room_id, const char *ufrag);

/** Publish one SSRC from a peer already in @p room_id. */
PUBLIC_API bool SfuServerAddTrack(SfuServer *server, size_t shard_index, uint64_t room_id, const char *ufrag,
                                  uint32_t ssrc, SfuTrackKind kind, uint32_t clock_rate);

/**
 * Map one publisher track onto an outbound SSRC for @p subscriber_ufrag. The
 * sequence offset is added to every forwarded sequence number, so the
 * subscriber sees a continuous stream across renegotiation.
 */
PUBLIC_API bool SfuServerSubscribe(SfuServer *server, size_t shard_index, uint64_t room_id,
                                   const char *subscriber_ufrag, uint32_t publisher_ssrc, uint32_t out_ssrc,
                                   uint16_t seq_offset);

/** Release a peer and everything it owns: room membership, SSRCs, ICE, DTLS, SRTP. */
PUBLIC_API bool SfuServerLeavePeer(SfuServer *server, size_t shard_index, const char *ufrag);

/** Read whether a stable peer key still exists, or synchronously release it.
 * Release is idempotent. False means the shard outcome is unavailable.
 * Pass a non-NULL present pointer to query, or NULL to release. */
PUBLIC_API bool SfuServerPeerLifecycle(SfuServer *server, size_t shard_index, const char *ufrag, bool *present);

/**
 * The SHA-256 of this server's DTLS certificate, the value every SDP answer must
 * advertise. False when @p out is NULL or no context exists.
 */
PUBLIC_API bool SfuServerCertificateFingerprint(const SfuServer *server, uint8_t out[32]);

/**
 * Render what signalling needs to write one SDP answer, as JSON:
 *
 *   {"shard":0,"address":"127.0.0.1","port":40000,"ice_ufrag":"...",
 *    "ice_pwd":"...","dtls_fingerprint":"aa:bb:..."}
 *
 * The address is the one the shard's socket is bound to (a wildcard bind is
 * reported as 0.0.0.0 or ::), the port is the shard's own, and the credentials
 * are this shard's, generated at onboarding. Everything here is immutable after
 * onboarding, so no shard thread is involved and the call never blocks.
 *
 * The caller must not use it after SfuServerStop(). Returns @p reply, with an
 * {"error":...} object when the shard does not exist.
 */
PUBLIC_API BufferDescriptor *SfuServerDescribeTransport(SfuServer *server, size_t shard_index,
                                                        BufferDescriptor *reply);

/** Prepare an ICE restart and return its local credentials as JSON.
 * The original registration ufrag remains the peer's control-plane identifier.
 */
PUBLIC_API BufferDescriptor *SfuServerRestartIce(SfuServer *server, size_t shard_index, const char *ufrag,
                                                const char *remote_ufrag, const char *remote_password,
                                                BufferDescriptor *reply);


/**
 * Ask one shard for its state document and wait for it, mirroring the console
 * to delegator command channel: the requestor supplies an initialised reply
 * buffer, the shard's own thread renders the JSON into it, and the call returns
 * once that thread has finished.
 *
 * Caller must not invoke this from a shard thread, and must not call it after
 * SfuServerStop(). Returns reply, with an {"error":...} object appended when the
 * command could not be delivered.
 */
PUBLIC_API BufferDescriptor *SfuServerDescribeShard(SfuServer *server, size_t shard_index, BufferDescriptor *reply);

/** Wake every shard thread, join them, and release the set and server handle. */
PUBLIC_API void SfuServerStop(SfuServer *server);

#endif
