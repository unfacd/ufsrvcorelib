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

#include <netdb.h>
#include <sys/eventfd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <uflib/adt/adt_hashtable.h>
#include <ufsrv_core/udp_server/udp_dtls_context.h>

#include "udp_server_priv.h"

static int
sBindShardSocket(const struct addrinfo *address, bool flow_mode)
{
  // Flow identity includes the local endpoint. Wildcard listeners would
  // need packet-info routing to identify and preserve that endpoint.
  if (flow_mode &&
      ((address->ai_family == AF_INET &&
        ((const struct sockaddr_in *)address->ai_addr)->sin_addr.s_addr == INADDR_ANY) ||
       (address->ai_family == AF_INET6 &&
        IN6_IS_ADDR_UNSPECIFIED(&((const struct sockaddr_in6 *)address->ai_addr)->sin6_addr)))) return -1;

  int fd = socket(address->ai_family, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, address->ai_protocol);
  if (fd < 0) return -1;

  int reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == 0 && bind(
    fd, address->ai_addr, address->ai_addrlen) == 0) return fd;

  close(fd);
  return -1;
}

UdpServer *
UdpServerStart(const UdpServerSpec *spec)
{
  if (!spec || !spec->shard_count || !spec->workers_per_shard || !spec->bind_host || !spec->port || spec->port > 65535
    || ((spec->on_datagram == NULL) == (spec->on_flow_datagram == NULL)) || spec->shard_count > SIZE_MAX / spec->workers_per_shard) return NULL;

  const bool flow_mode = spec->on_flow_datagram != NULL;
  if (!flow_mode && spec->on_flow_session_release) return NULL;
  if (flow_mode && (!spec->flows_per_shard || !spec->flow_idle_timeout_seconds || !spec->sends_per_shard))
    return NULL;

  const bool dtls_mode = spec->dtls_chain_file || spec->dtls_key_file;
  if (dtls_mode && !flow_mode) return NULL;

  size_t workers = spec->shard_count * spec->workers_per_shard;
  if (!spec->worker_slots || !spec->slot_size || spec->slot_count != workers || workers > SIZE_MAX / spec->slot_size)
    return NULL;

  char service[6];
  snprintf(service, sizeof(service), "%u", spec->port);
  struct addrinfo  hints     = {.ai_socktype = SOCK_DGRAM, .ai_flags = AI_NUMERICSERV};
  struct addrinfo *addresses = NULL;
  if (getaddrinfo(spec->bind_host, service, &hints, &addresses) != 0) return NULL;

  UdpServer *server = calloc(1, sizeof(*server));
  if (!server) {
    freeaddrinfo(addresses);
    return NULL;
  }
  if (dtls_mode) {
    server->dtls_context = UdpDtlsServerContextCreate(spec->dtls_chain_file, spec->dtls_key_file);
    if (!server->dtls_context) goto fail;
  }
  server->shards         = calloc(spec->shard_count, sizeof(*server->shards));
  server->shard_threads  = calloc(spec->shard_count, sizeof(*server->shard_threads));
  server->worker_threads = calloc(workers, sizeof(*server->worker_threads));
  server->shard_starts   = calloc(spec->shard_count, sizeof(*server->shard_starts));
  server->worker_starts  = calloc(workers, sizeof(*server->worker_starts));
  if (!server->shards || !server->shard_threads || !server->worker_threads || !server->shard_starts || !server->
    worker_starts) goto fail;

  server->on_worker_start = spec->on_worker_start;
  server->admit_datagram  = spec->admit_datagram;
  server->protocol_context_data = spec->protocol_context_data;
  server->on_datagram     = spec->on_datagram;
  server->on_flow_datagram = spec->on_flow_datagram;
  server->on_flow_session_release = spec->on_flow_session_release;
  server->flows_per_shard = spec->flows_per_shard;
  server->flow_idle_timeout_seconds = spec->flow_idle_timeout_seconds;
  server->sends_per_shard = spec->sends_per_shard;

  const struct addrinfo *address = addresses;
  for (size_t i = 0; i < spec->shard_count; i++) {
    UdpShard *shard = &server->shards[i];
    if (pthread_mutex_init(&shard->mutex, NULL) != 0) goto fail;
    if (pthread_cond_init(&shard->wake, NULL) != 0) {
      pthread_mutex_destroy(&shard->mutex);
      goto fail;
    }
    shard->fd = -1;
    shard->send_wake = -1;
    server->shards_initialized++;
    shard->index = i;
    atomic_init(&shard->up, 1);
    shard->fd = sBindShardSocket(address, flow_mode);
    while (i == 0 && shard->fd < 0 && address->ai_next) {
      address   = address->ai_next;
      shard->fd = sBindShardSocket(address, flow_mode);
    }
    if (shard->fd < 0) goto fail;
    if (flow_mode) {
      const HashtableSpecs index = {
        .table_name = "UdpShardPeers",
        .fTableSize = spec->shard_hash_table_startup_size,
        .fKeyOffset = offsetof(UdpFlow, key),
        .fKeySize = sizeof(UdpPeerKey),
        .fKeyIsPtr = HASHTABLE_KEY_IS_INLINE,
      };
      shard->peer_index = HashTableSetUniqueKeys(HashTableInstantiateWithSpecs(NULL, &index), true);
      shard->flows = calloc(spec->flows_per_shard, sizeof(*shard->flows));
      shard->send_wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
      if (!shard->peer_index || !shard->flows || shard->send_wake < 0) goto fail;
    }
    server->shard_starts[i] = (UdpThreadStart){.shard = shard, .server = server};
  }

  for (size_t i = 0; i < workers; i++) {
    UdpThreadStart *start     = &server->worker_starts[i];
    start->creation.idx       = i;
    start->creation.user_data = (unsigned char*)spec->worker_slots + i * spec->slot_size;
    start->shard              = &server->shards[i / spec->workers_per_shard];
    start->server             = server;
    if (pthread_create(&server->worker_threads[i], NULL, UdpWorkerThread, start) != 0) goto fail;
    server->workers_started++;
    pthread_mutex_lock(&start->shard->mutex);
    while (!start->ready) pthread_cond_wait(&start->shard->wake, &start->shard->mutex);
    pthread_mutex_unlock(&start->shard->mutex);
  }
  for (size_t i = 0; i < spec->shard_count; i++) {
    UdpThreadStart *start = &server->shard_starts[i];
    if (pthread_create(&server->shard_threads[i], NULL, UdpShardThread, start) != 0) goto fail;
    server->shards_started++;
    pthread_mutex_lock(&start->shard->mutex);
    while (!start->ready) pthread_cond_wait(&start->shard->wake, &start->shard->mutex);
    pthread_mutex_unlock(&start->shard->mutex);
  }

  freeaddrinfo(addresses);
  return server;

fail:
  freeaddrinfo(addresses);
  UdpServerStop(server);
  return NULL;
}

void *
UdpServerProtocolContext(UdpServer *server)
{
  return server ? server->protocol_context_data : NULL;
}

void
UdpServerStop(UdpServer *server)
{
  if (!server) return;

  for (size_t i = 0; i < server->shards_initialized; i++) {
    UdpShard *shard = &server->shards[i];
    pthread_mutex_lock(&shard->mutex);
    atomic_store(&shard->up, 0);
    pthread_cond_broadcast(&shard->wake);
    if (shard->send_wake >= 0) eventfd_write(shard->send_wake, 1);
    pthread_mutex_unlock(&shard->mutex);
  }
  for (size_t i = 0; i < server->shards_started; i++) pthread_join(server->shard_threads[i], NULL);
  for (size_t i = 0; i < server->workers_started; i++) pthread_join(server->worker_threads[i], NULL);

  for (size_t i = 0; i < server->shards_initialized; i++) {
    UdpShard *shard = &server->shards[i];
    if (shard->fd >= 0) close(shard->fd);
    if (shard->send_wake >= 0) close(shard->send_wake);
    while (shard->send_head) {
      UdpSend *send = shard->send_head;
      shard->send_head = send->next;
      free(send);
    }
    HashTableDestruct(shard->peer_index, true);
    free(shard->flows);
    pthread_cond_destroy(&shard->wake);
    pthread_mutex_destroy(&shard->mutex);
  }
  free(server->shards);
  free(server->shard_threads);
  free(server->worker_threads);
  free(server->shard_starts);
  free(server->worker_starts);
  SSL_CTX_free(server->dtls_context);
  free(server);
}
