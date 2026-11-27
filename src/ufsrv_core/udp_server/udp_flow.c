/**
 * @file udp_flow.c
 * @brief Shard-owned UDP peer identities and asynchronous send queue.
 *
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

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>

#include <openssl/err.h>
#include <uflib/adt/adt_hashtable.h>

#include "udp_server_priv.h"

static bool
MakePeerKey(const UdpShard *shard, UdpPeerKey *key)
{
  *key = (UdpPeerKey){0};
  if (shard->peer.ss_family == AF_INET && shard->peer_len >= sizeof(struct sockaddr_in)) {
    const struct sockaddr_in *peer = (const struct sockaddr_in *)&shard->peer;
    key->family = 4;
    memcpy(key->port, &peer->sin_port, sizeof(key->port));
    memcpy(key->address, &peer->sin_addr, sizeof(peer->sin_addr));
    return true;
  }
  if (shard->peer.ss_family == AF_INET6 && shard->peer_len >= sizeof(struct sockaddr_in6)) {
    const struct sockaddr_in6 *peer = (const struct sockaddr_in6 *)&shard->peer;
    key->family = 6;
    memcpy(key->port, &peer->sin6_port, sizeof(key->port));
    memcpy(key->scope, &peer->sin6_scope_id, sizeof(key->scope));
    memcpy(key->address, &peer->sin6_addr, sizeof(key->address));
    return true;
  }
  return false;
}

static void
RemovePeerIndex(UdpShard *shard, UdpFlow *flow)
{
  if (!flow->indexed) return;
  RemoveFromHash(shard->peer_index, flow);
  flow->indexed = false;
}

static bool
TimeBefore(const struct timespec *left, const struct timespec *right)
{
  return left->tv_sec < right->tv_sec ||
         (left->tv_sec == right->tv_sec && left->tv_nsec < right->tv_nsec);
}

static bool
FlowActive(const UdpServer *server, const UdpFlow *flow, const struct timespec *now)
{
  if (!flow->generation || flow->closed) return false;
  if (TimeBefore(now, &flow->lease_deadline)) return true;
  time_t seconds = now->tv_sec - flow->last_seen.tv_sec;
  if (now->tv_nsec < flow->last_seen.tv_nsec) seconds--;
  return seconds < (time_t)server->flow_idle_timeout_seconds;
}

bool
UdpFlowResolve(UdpServer *server, UdpShard *shard, SSL *candidate, UdpFlowHandle *handle)
{
  UdpPeerKey key;
  struct timespec now;
  if (!MakePeerKey(shard, &key) || clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;

  UdpFlow *resident = HashLookup(shard->peer_index, &key, true);
  if (resident && FlowActive(server, resident, &now)) {
    if (candidate) return false;
    resident->last_seen = now;
    *handle = (UdpFlowHandle){shard->index, (size_t)(resident - shard->flows), resident->generation};
    return true;
  }

  // The shard alone owns the index. The mutex also protects slot state
  // consulted by workers and asynchronous send producers.
  for (size_t slot = 0; slot < server->flows_per_shard; slot++) {
    UdpFlow *flow = &shard->flows[slot];
    if (FlowActive(server, flow, &now) || flow->session || flow->dtls ||
        flow->generation == UINT64_MAX) continue;

    if (resident) RemovePeerIndex(shard, resident);
    RemovePeerIndex(shard, flow);
    flow->key = key;
    if (AddToHash(shard->peer_index, flow) != flow) return false;

    flow->indexed = true;
    flow->peer = shard->peer;
    flow->peer_len = shard->peer_len;
    flow->last_seen = now;
    flow->lease_deadline = (struct timespec){0};
    flow->on_lease_expiry = NULL;
    flow->dtls = candidate;
    flow->closed = false;
    *handle = (UdpFlowHandle){shard->index, slot, ++flow->generation};
    return true;
  }
  return false;
}

UdpFlow *
UdpFlowFindDtls(UdpServer *server, UdpShard *shard)
{
  UdpPeerKey key;
  struct timespec now;
  if (!MakePeerKey(shard, &key) || clock_gettime(CLOCK_MONOTONIC, &now) != 0) return NULL;

  UdpFlow *flow = HashLookup(shard->peer_index, &key, true);
  if (!flow || !flow->dtls || !FlowActive(server, flow, &now)) return NULL;
  shard->flow = (UdpFlowHandle){shard->index, (size_t)(flow - shard->flows), flow->generation};
  return flow;
}

void
UdpFlowClose(UdpShard *shard, UdpFlow *flow)
{
  pthread_mutex_lock(&shard->mutex);
  flow->closed = true;
  RemovePeerIndex(shard, flow);
  pthread_mutex_unlock(&shard->mutex);
}

bool
UdpFlowTouch(UdpServer *server, UdpShard *shard, UdpFlowHandle handle)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
  UdpFlow *flow = &shard->flows[handle.slot];
  if (flow->generation != handle.generation || !FlowActive(server, flow, &now)) return false;
  flow->last_seen = now;
  return true;
}

static UdpFlow *
FindActiveFlow(const UdpServer *server, const UdpShard *shard, UdpFlowHandle handle)
{
  struct timespec now;
  UdpFlow *flow = &shard->flows[handle.slot];
  if (flow->generation != handle.generation || clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
      !FlowActive(server, flow, &now)) return NULL;
  return flow;
}

bool
UdpFlowIsActive(const UdpServer *server, const UdpShard *shard, UdpFlowHandle handle)
{
  return FindActiveFlow(server, shard, handle) != NULL;
}

bool
UdpServerSetFlowLease(UdpServer *server, UdpFlowHandle handle, const struct timespec *deadline)
{
  return UdpServerSetFlowLeaseWithExpiry(server, handle, deadline, NULL);
}

bool
UdpServerSetFlowLeaseWithExpiry(UdpServer *server, UdpFlowHandle handle,
                               const struct timespec *deadline, UdpFlowLeaseExpiryCallback on_expiry)
{
  if (!server || !server->on_flow_datagram || !handle.generation ||
      handle.shard >= server->shards_initialized || handle.slot >= server->flows_per_shard ||
      (on_expiry && !deadline) ||
      (deadline && (deadline->tv_sec < 0 || deadline->tv_nsec < 0 ||
                    deadline->tv_nsec >= 1000000000L))) return false;

  UdpShard *shard = &server->shards[handle.shard];
  pthread_mutex_lock(&shard->mutex);
  UdpFlow *flow = &shard->flows[handle.slot];
  struct timespec now;
  const bool active = atomic_load(&shard->up) && flow->generation == handle.generation &&
                      (!on_expiry || flow->session) &&
                      clock_gettime(CLOCK_MONOTONIC, &now) == 0 && FlowActive(server, flow, &now) &&
                      (!deadline || TimeBefore(&now, deadline));
  if (active) {
    flow->lease_deadline = deadline ? *deadline : (struct timespec){0};
    flow->on_lease_expiry = on_expiry;
  }
  pthread_mutex_unlock(&shard->mutex);
  return active;
}

bool
UdpServerQueueSend(UdpServer *server, UdpFlowHandle handle, const void *payload, size_t length)
{
  if (!server || !server->on_flow_datagram || !handle.generation ||
      handle.shard >= server->shards_initialized || handle.slot >= server->flows_per_shard ||
      !payload || !length || length > 65507) return false;

  UdpShard *shard = &server->shards[handle.shard];
  pthread_mutex_lock(&shard->mutex);
  if (!atomic_load(&shard->up) || shard->send_count >= server->sends_per_shard ||
      !UdpFlowIsActive(server, shard, handle)) {
    pthread_mutex_unlock(&shard->mutex);
    return false;
  }

  UdpSend *send = malloc(sizeof(*send) + length);
  if (!send) {
    pthread_mutex_unlock(&shard->mutex);
    return false;
  }
  *send = (UdpSend){.flow = handle, .length = length};
  memcpy(send->payload, payload, length);

  int wake_status;
  do {
    wake_status = eventfd_write(shard->send_wake, 1);
  } while (wake_status < 0 && errno == EINTR);
  if (wake_status < 0 && errno != EAGAIN) {
    free(send);
    pthread_mutex_unlock(&shard->mutex);
    return false;
  }

  if (shard->send_tail) shard->send_tail->next = send;
  else shard->send_head = send;
  shard->send_tail = send;
  shard->send_count++;
  pthread_mutex_unlock(&shard->mutex);
  return true;
}

void
UdpShardDrainSends(UdpServer *server, UdpShard *shard)
{
  eventfd_t wake;
  eventfd_read(shard->send_wake, &wake);

  pthread_mutex_lock(&shard->mutex);
  const size_t pending = shard->send_count;
  for (size_t i = 0; i < pending && atomic_load(&shard->up); i++) {
    UdpSend *send = shard->send_head;
    shard->send_head = send->next;
    if (!shard->send_head) shard->send_tail = NULL;

    UdpFlow *flow = FindActiveFlow(server, shard, send->flow);
    pthread_mutex_unlock(&shard->mutex);
    // Only this shard changes peer addresses or consumes queued sends.
    if (flow) UdpShardSendFlow(shard, flow, send->payload, send->length);
    free(send);

    pthread_mutex_lock(&shard->mutex);
    shard->send_count--;
  }
  pthread_mutex_unlock(&shard->mutex);
}

static UdpShard *
SessionShard(UdpServer *server, UdpFlowHandle handle)
{
  if (!server || !server->on_flow_session_release || !handle.generation ||
      handle.shard >= server->shards_initialized || handle.slot >= server->flows_per_shard) return NULL;
  return &server->shards[handle.shard];
}

static bool
WorkerOwnsFlow(const UdpShard *shard, UdpFlowHandle handle)
{
  return shard->state == UDP_WORK_RUNNING &&
         shard->flow.slot == handle.slot && shard->flow.generation == handle.generation &&
         pthread_equal(shard->active_worker, pthread_self());
}

bool
UdpServerAttachSession(UdpServer *server, UdpFlowHandle handle, InstanceHolder *session)
{
  UdpShard *shard = SessionShard(server, handle);
  if (!shard || !session) return false;

  pthread_mutex_lock(&shard->mutex);
  UdpFlow *flow = &shard->flows[handle.slot];
  const bool attached = atomic_load(&shard->up) && WorkerOwnsFlow(shard, handle) &&
                        !flow->session && UdpFlowIsActive(server, shard, handle);
  if (attached) flow->session = session;
  pthread_mutex_unlock(&shard->mutex);
  return attached;
}

InstanceHolder *
UdpServerFlowSession(UdpServer *server, UdpFlowHandle handle)
{
  UdpShard *shard = SessionShard(server, handle);
  if (!shard) return NULL;

  pthread_mutex_lock(&shard->mutex);
  InstanceHolder *session = WorkerOwnsFlow(shard, handle) ? shard->flows[handle.slot].session : NULL;
  pthread_mutex_unlock(&shard->mutex);
  return session;
}

void
UdpShardMaintainFlows(UdpServer *server, UdpShard *shard, bool stopping)
{
  if (!server->on_flow_session_release && !server->dtls_context) return;
  struct timespec now;
  if (!stopping && clock_gettime(CLOCK_MONOTONIC, &now) != 0) return;

  pthread_mutex_lock(&shard->mutex);
  for (size_t i = 0; i < server->flows_per_shard; i++) {
    UdpFlow *flow = &shard->flows[i];
    if (!flow->session && !flow->dtls) continue;
    const UdpFlowHandle handle = {shard->index, i, flow->generation};
    if (!stopping && FlowActive(server, flow, &now)) {
      if (flow->on_lease_expiry && !TimeBefore(&now, &flow->lease_deadline)) {
        UdpFlowLeaseExpiryCallback on_expiry = flow->on_lease_expiry;
        InstanceHolder *session = flow->session;
        flow->on_lease_expiry = NULL;
        flow->lease_deadline = (struct timespec){0};
        pthread_mutex_unlock(&shard->mutex);
        on_expiry(handle, session);
        pthread_mutex_lock(&shard->mutex);
      }
      if (!flow->dtls) continue;
      pthread_mutex_unlock(&shard->mutex);
      ERR_clear_error();
      int result = DTLSv1_handle_timeout(flow->dtls);
      pthread_mutex_lock(&shard->mutex);
      if (result >= 0) continue;
    }

    InstanceHolder *session = flow->session;
    SSL *dtls = flow->dtls;
    RemovePeerIndex(shard, flow);
    flow->session = NULL;
    flow->dtls = NULL;
    flow->on_lease_expiry = NULL;
    flow->lease_deadline = (struct timespec){0};
    flow->closed = true;
    pthread_mutex_unlock(&shard->mutex);
    SSL_free(dtls);
    if (session) server->on_flow_session_release(handle, session);
    pthread_mutex_lock(&shard->mutex);
  }
  pthread_mutex_unlock(&shard->mutex);
}
