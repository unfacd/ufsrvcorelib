/**
 * @file sfu_shard_onboarding.c
 * @brief Seam between the main thread and shard context creation.
 *
 * The main thread owns every shared allocation and publishes one immutable
 * SfuShardSpec per shard; after this returns, each shard thread only ever reads
 * its own spec. Onboarding does not start threads: it makes the contexts
 * complete enough that a thread can be pointed at one.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#include <stddef.h>

#include <uflib/cdt/cdt_lamport_queue.h>
#include <uflib/cdt/cdt_mpsc_queue.h>
#include <uflib/cdt/cdt_treiber_stack.h>
#include <uflib/adt/flow_table/flow_table.h>
#include <uflib/slab/inlined_slab.h>

#include "sfu_server_priv.h"
#include "sfu_shard_subscription.h"
#include "sfu_shard_ice.h"
#include "dtls/sfu_shard_dtls.h"

#if defined(HAVE_VALGRIND_MEMCHECK)
# include <valgrind/memcheck.h>
#endif

/*
 * Defaults until the configuration surface lands. Each one is a documented
 * engine default, not a hard-coded policy: they become config keys in a later
 * step, at which point the host fills them into the spec.
 */
#define SFU_ONBOARD_RECV_BATCH     64u
#define SFU_ONBOARD_SEND_BATCH     64u
#define SFU_ONBOARD_PEER_CAPACITY  256u
#define SFU_ONBOARD_SLAB_PACKETS   8192u
#define SFU_ONBOARD_QUEUE_DEPTH    1024u  /* one slot reserved: 1023 usable */
#define SFU_ONBOARD_COMMAND_POOL   256u
#define SFU_ONBOARD_SOCKET_BUFFER  (8u * 1024u * 1024u)
#define SFU_ONBOARD_MAX_SHARDS     64u
#define SFU_ONBOARD_POLL_MS        100u
#define SFU_ONBOARD_PEER_IDLE_TIMEOUT_SECONDS 60u
#define SFU_ONBOARD_CONSENT_TIMEOUT_SECONDS 30u
#define SFU_ONBOARD_UFRAG_MIN_CAPACITY 64u
#define SFU_ONBOARD_ROOMS_MAX 64u  /* rooms per shard; the map bound */
#define SFU_ONBOARD_RTCP_INTERVAL_SECONDS 5u    /* RR cadence toward publishers */
#define SFU_ONBOARD_RTCP_PLI_MIN_INTERVAL_MS 1000u /* per-publisher PLI throttle */

static int
sBindShardSocket(const SfuServerSpec *server, SfuShardSpec *spec){
  const unsigned port = spec->port;
  const char *host = server->bind_host;
  char service[6];
  snprintf(service, sizeof(service), "%u", port);

  struct addrinfo hints = {.ai_socktype = SOCK_DGRAM, .ai_flags = AI_NUMERICSERV};
  struct addrinfo *addresses = NULL;
  if (getaddrinfo(host, service, &hints, &addresses) != 0) return -1;

  int fd = -1;
  for (const struct addrinfo *address = addresses; address; address = address->ai_next) {
    fd = socket(address->ai_family, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, address->ai_protocol);
    if (fd < 0) continue;

    if (address->ai_family == AF_INET6) {
      /* Dual stack: one socket serves IPv6 and IPv4-mapped peers, and the shard
       * keys each family in its own table. Refusing this option would silently
       * turn the socket into v6-only. */
      int v6only = 0;
      if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) != 0) {
        syslog(LOG_WARNING, "%s: IPV6_V6ONLY=0 refused: %s", __func__, strerror(errno));
      }
    }

    int reuse = server->reuseport ? 1 : 0;
    int rcvbuf = (int)(server->rcvbuf_bytes ? server->rcvbuf_bytes : SFU_ONBOARD_SOCKET_BUFFER);
    int sndbuf = (int)(server->sndbuf_bytes ? server->sndbuf_bytes : SFU_ONBOARD_SOCKET_BUFFER);
    /* Tuning failures are reported but never fatal: a kernel that refuses one
     * option (busy-poll is privileged on some releases) must not stop the bind. */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) != 0) {
      syslog(LOG_WARNING, "%s: SO_REUSEPORT=%d refused: %s", __func__, reuse, strerror(errno));
    }
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) != 0) {
      syslog(LOG_WARNING, "%s: SO_RCVBUF=%d refused: %s", __func__, rcvbuf, strerror(errno));
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) != 0) {
      syslog(LOG_WARNING, "%s: SO_SNDBUF=%d refused: %s", __func__, sndbuf, strerror(errno));
    }
    if (server->busy_poll_us) {
      int busy_poll = (int)server->busy_poll_us;
      if (setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll)) != 0) {
        syslog(LOG_WARNING, "%s: SO_BUSY_POLL=%d refused: %s", __func__, busy_poll, strerror(errno));
      }
    }
    if (server->path_mtu_discover && address->ai_family == AF_INET) {
      int discover = IP_PMTUDISC_DO;
      if (setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &discover, sizeof(discover)) != 0) {
        syslog(LOG_WARNING, "%s: IP_MTU_DISCOVER refused: %s", __func__, strerror(errno));
      }
    }
    else if (server->path_mtu_discover && address->ai_family == AF_INET6) {
      int discover = IPV6_PMTUDISC_DO;
      if (setsockopt(fd, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &discover, sizeof(discover)) != 0) {
        syslog(LOG_WARNING, "%s: IPV6_MTU_DISCOVER refused: %s", __func__, strerror(errno));
      }
    }

    if (bind(fd, address->ai_addr, address->ai_addrlen) == 0) {
      spec->family = address->ai_family;
      if (address->ai_family == AF_INET) {
        spec->bound_address = ((const struct sockaddr_in *)address->ai_addr)->sin_addr.s_addr;
      } else {
        memcpy(spec->bound_address6, &((const struct sockaddr_in6 *)address->ai_addr)->sin6_addr, 16);
      }
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(addresses);
  return fd;
}

/* Queues are indexed by ordered pair: one queue for traffic from -> to. */
static size_t
sQueueIndex(size_t count, size_t from, size_t to)
{
  return from * (count - 1) + (to < from ? to : to - 1);
}

/** Geometry resolved once from the server contract and shared by every shard. */
typedef struct SfuGeometry {
  unsigned recv_batch;
  unsigned send_batch;
  unsigned peer_capacity;
  unsigned slab_packets;
  unsigned queue_depth;
  unsigned command_pool;
  unsigned poll_ms;
  unsigned peers_idle_timeout_seconds;
  unsigned ice_consent_timeout_seconds;
  unsigned rtcp_interval_seconds;
  unsigned rtcp_pli_min_interval_ms;
  unsigned nack_retransmit_per_second;
  size_t   ufrag_capacity;
  size_t   ssrc_capacity;
  size_t   subscription_capacity;
  size_t   rooms_max;
} SfuGeometry;

/**
 * One block of storage for the whole set. The first pass runs with base NULL to
 * measure, the second carves the same layout out of the allocated region, so
 * there is a single description of the footprint and one free at teardown.
 */
typedef struct SfuArena {
  unsigned char *base;
  size_t         used;
} SfuArena;

static size_t
sAlignUp(size_t value, size_t alignment)
{
  return (value + (alignment - 1)) & ~(alignment - 1);
}

static void *
sArenaBlock(SfuArena *arena, size_t bytes, size_t alignment)
{
  const size_t offset = sAlignUp(arena->used, alignment);
  void *       block = arena->base ? arena->base + offset : NULL;
  arena->used = offset + bytes;
#if defined(HAVE_VALGRIND_MEMCHECK)
  if (block) VALGRIND_MEMPOOL_ALLOC(arena->base, block, bytes);
#endif
  return block;
}

static SfuGeometry
sResolveGeometry(const SfuServerSpec *server)
{
  SfuGeometry geometry = {
    .recv_batch = server->recv_batch ? server->recv_batch : SFU_ONBOARD_RECV_BATCH,
    .send_batch = server->send_batch ? server->send_batch : SFU_ONBOARD_SEND_BATCH,
    .peer_capacity = server->peer_capacity ? server->peer_capacity : SFU_ONBOARD_PEER_CAPACITY,
    .slab_packets = server->slab_packets ? server->slab_packets : SFU_ONBOARD_SLAB_PACKETS,
    .queue_depth = server->queue_depth ? server->queue_depth : SFU_ONBOARD_QUEUE_DEPTH,
    .command_pool = server->command_pool ? server->command_pool : SFU_ONBOARD_COMMAND_POOL,
    .poll_ms = server->poll_ms ? server->poll_ms : SFU_ONBOARD_POLL_MS,
    .peers_idle_timeout_seconds = server->peers_idle_timeout_seconds ? server->peers_idle_timeout_seconds
                                                                   : SFU_ONBOARD_PEER_IDLE_TIMEOUT_SECONDS,
    .ice_consent_timeout_seconds = server->ice_consent_timeout_seconds ? server->ice_consent_timeout_seconds
                                                                      : SFU_ONBOARD_CONSENT_TIMEOUT_SECONDS,
    .rtcp_interval_seconds = server->rtcp_interval_seconds ? server->rtcp_interval_seconds
                                                           : SFU_ONBOARD_RTCP_INTERVAL_SECONDS,
    .rtcp_pli_min_interval_ms = server->rtcp_pli_min_interval_ms ? server->rtcp_pli_min_interval_ms
                                                                 : SFU_ONBOARD_RTCP_PLI_MIN_INTERVAL_MS,
    .nack_retransmit_per_second = server->nack_retransmit_per_second,
    .ufrag_capacity = SFU_ONBOARD_UFRAG_MIN_CAPACITY,
    .ssrc_capacity = SFU_ONBOARD_UFRAG_MIN_CAPACITY,
    .subscription_capacity = SFU_ONBOARD_UFRAG_MIN_CAPACITY,
    .rooms_max = server->rooms_max ? server->rooms_max : SFU_ONBOARD_ROOMS_MAX,
  };
  while (geometry.ufrag_capacity < (size_t)geometry.peer_capacity * 2u) geometry.ufrag_capacity <<= 1;
  while (geometry.ssrc_capacity < (size_t)geometry.peer_capacity * 2u) geometry.ssrc_capacity <<= 1;
  while (geometry.subscription_capacity < (size_t)geometry.peer_capacity * SFU_PEER_SUBS_MAX)
    geometry.subscription_capacity <<= 1;
  return geometry;
}

/**
 * Reserve every array and slab the set needs. Writes to @p set only when the
 * arena has a base, which is what lets the measuring pass reuse this layout.
 */
static void
sPlanArena(SfuShardSet *set, SfuArena *arena, const SfuGeometry *geometry)
{
  const size_t count = set->count;
  const size_t pairs = count * (count - 1);
  const bool   wire = arena->base != NULL;

  set->specs = sArenaBlock(arena, count * sizeof(*set->specs), _Alignof(SfuShardSpec));
  set->shards = sArenaBlock(arena, count * sizeof(*set->shards), _Alignof(SfuShard));
  set->counters = sArenaBlock(arena, count * sizeof(*set->counters), _Alignof(SfuCounters));
  set->slabs = sArenaBlock(arena, count * (size_t)geometry->slab_packets * sizeof(SfuPacket), _Alignof(SfuPacket));
  set->inbound_links = sArenaBlock(arena, count * count * sizeof(*set->inbound_links), _Alignof(void *));
  set->outbound_links = sArenaBlock(arena, count * count * sizeof(*set->outbound_links), _Alignof(void *));
  set->queues = pairs ? sArenaBlock(arena, pairs * sizeof(*set->queues), _Alignof(LocklessSpscQueue)) : NULL;
  set->queue_payload =
      pairs ? sArenaBlock(arena, pairs * (size_t)geometry->queue_depth * sizeof(*set->queue_payload), _Alignof(void *))
            : NULL;

  for (size_t index = 0; index < count; index++) {
    void *peers = sArenaBlock(arena, (size_t)geometry->peer_capacity * sizeof(SfuPeer), _Alignof(SfuPeer));
    void *recv_packets = sArenaBlock(arena, (size_t)geometry->recv_batch * sizeof(SfuPacket *), _Alignof(SfuPacket *));
    void *recv_msgs = sArenaBlock(arena, (size_t)geometry->recv_batch * sizeof(struct mmsghdr), _Alignof(struct mmsghdr));
    void *recv_iov = sArenaBlock(arena, (size_t)geometry->recv_batch * sizeof(struct iovec), _Alignof(struct iovec));
    void *recv_peers = sArenaBlock(arena, (size_t)geometry->recv_batch * sizeof(struct sockaddr_storage),
                                   _Alignof(struct sockaddr_storage));
    void *send_msgs = sArenaBlock(arena, (size_t)geometry->send_batch * sizeof(struct mmsghdr), _Alignof(struct mmsghdr));
    void *send_iov = sArenaBlock(arena, (size_t)geometry->send_batch * sizeof(struct iovec), _Alignof(struct iovec));
    void *command_pool =
        sArenaBlock(arena, ((size_t)geometry->command_pool + 1u) * sizeof(SfuCommandNode), _Alignof(SfuCommandNode));
    void *ufrag_index =
        sArenaBlock(arena, geometry->ufrag_capacity * sizeof(SfuUfragEntry), _Alignof(SfuUfragEntry));
    void *ssrc_index =
        sArenaBlock(arena, geometry->ssrc_capacity * sizeof(SfuSsrcEntry), _Alignof(SfuSsrcEntry));
    void *subscription_index = sArenaBlock(arena, geometry->subscription_capacity * sizeof(SfuOutboundEntry),
                                          _Alignof(SfuOutboundEntry));
    void *nack_rings =
        sArenaBlock(arena, (size_t)geometry->peer_capacity * sizeof(SfuNackRing), _Alignof(SfuNackRing));
    void *nack_slots =
        sArenaBlock(arena, (size_t)geometry->peer_capacity * SFU_NACK_VIDEO_SLOTS * sizeof(SfuNackSlot),
                    _Alignof(SfuNackSlot));
    if (!wire) continue;
    SfuShard *shard = &set->shards[index];
    /* Nothing is bound yet: publish the empty descriptors now so teardown of a
     * set that fails part-way never closes a descriptor that was never opened. */
    shard->spec = &set->specs[index];
    shard->fd = -1;
    shard->epoll_fd = -1;
    set->specs[index].fd = -1;
    set->specs[index].wake_fd = -1;
    shard->peers.slots = peers;
    shard->packets.recv_packets = recv_packets;
    shard->packets.recv_msgs = recv_msgs;
    shard->packets.recv_iov = recv_iov;
    shard->packets.recv_peers = recv_peers;
    shard->packets.send_msgs = send_msgs;
    shard->packets.send_iov = send_iov;
    shard->commands.pool = command_pool;
    shard->ice.index = ufrag_index;
    shard->ice.mask = geometry->ufrag_capacity - 1u;
    shard->ice.count = 0;
    shard->ssrc_index = ssrc_index;
    shard->ssrc_mask = geometry->ssrc_capacity - 1u;
    shard->ssrc_count = 0;
    shard->subscriptions.index = subscription_index;
    shard->subscriptions.mask = geometry->subscription_capacity - 1u;
    shard->nack.rings = nack_rings;
    shard->nack.slots = nack_slots;
  }
}

/** Fill one shard's context: its contract, its socket and its pool. */
static bool
sStartShard(SfuShardSet *set, size_t index, const SfuServerSpec *server, const SfuGeometry *geometry)
{
  SfuShardSpec *spec = &set->specs[index];
  SfuShard *    shard = &set->shards[index];

  spec->index = index;
  spec->shard_count = set->count;
  spec->port = (unsigned)(server->base_port + (unsigned)index);
  spec->recv_batch = geometry->recv_batch;
  spec->send_batch = geometry->send_batch;
  spec->peer_capacity = geometry->peer_capacity;
  spec->rooms_max = (unsigned)geometry->rooms_max;
  spec->slab_packets = geometry->slab_packets;
  spec->queue_depth = geometry->queue_depth;
  spec->poll_ms = geometry->poll_ms;
  spec->peers_idle_timeout_seconds = geometry->peers_idle_timeout_seconds;
  spec->ice_consent_timeout_seconds = geometry->ice_consent_timeout_seconds;
  spec->rtcp_interval_seconds = geometry->rtcp_interval_seconds;
  spec->rtcp_pli_min_interval_ms = geometry->rtcp_pli_min_interval_ms;
  spec->nack_retransmit_per_second = geometry->nack_retransmit_per_second;
  spec->slab = &set->slabs[index * (size_t)geometry->slab_packets];
  spec->counters = &set->counters[index];

  /* Full-width arrays, indexed by shard id: spec->outbound[target] and
   * spec->inbound[source] are directly usable by the shard, with the self slot
   * left NULL so a mistake reads as a null dereference, not a wrong queue. */
  {
    LocklessSpscQueue **inbound = &set->inbound_links[index * set->count];
    LocklessSpscQueue **outbound = &set->outbound_links[index * set->count];
    for (size_t other = 0; other < set->count; other++) {
      if (other == index) {
        inbound[other] = NULL;
        outbound[other] = NULL;
        continue;
      }
      inbound[other] = &set->queues[sQueueIndex(set->count, other, index)];
      outbound[other] = &set->queues[sQueueIndex(set->count, index, other)];
    }
    spec->inbound = inbound;
    spec->outbound = outbound;
  }

  spec->fd = sBindShardSocket(server, spec);
  if (spec->fd < 0) {
    syslog(LOG_ERR, "%s: COULD NOT bind shard %zu on %s:%u", __func__, index, server->bind_host, spec->port);
    return false;
  }
  /* Publish the bound socket before anything else can fail, so teardown of a
   * half-onboarded shard still closes it. */
  shard->spec = spec;
  shard->fd = spec->fd;

  if (!SfuIceGenerateCredentials(spec->ice_ufrag, spec->ice_pwd)) {
    syslog(LOG_ERR, "%s: COULD NOT generate ICE credentials for shard %zu", __func__, index);
    return false;
  }

  spec->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (spec->wake_fd < 0) {
    syslog(LOG_ERR, "%s: COULD NOT create the wake descriptor for shard %zu", __func__, index);
    return false;
  }

  shard->epoll_fd = -1; /* the shard thread creates and owns it */
  shard->peers.capacity = spec->peer_capacity;

  InlinedSlabDescriptor packet_pool = {
    .region = spec->slab,
    .region_bytes = (size_t)spec->slab_packets * sizeof(SfuPacket),
    .capacity = spec->slab_packets,
    .slot_size = sizeof(SfuPacket),
    .alignment = _Alignof(SfuPacket),
    .link_offset = offsetof(SfuPacket, link),
    .low_water = spec->slab_packets / 10u,
    .flags = INLINED_SLAB_FLAG_NONE,
  };
#if UFSRV_DEBUG_BUILD
  packet_pool.flags |= INLINED_SLAB_FLAG_GUARD;
#endif
  if (InlinedSlabInit(&shard->packets.slab, &packet_pool) != 0) {
    syslog(LOG_ERR, "%s: COULD NOT initialise the packet pool of shard %zu", __func__, index);
    return false;
  }
  for (unsigned slot = 0; slot < spec->slab_packets; slot++) spec->slab[slot].home_shard = (uint16_t)index;

  shard->nack.capacity = spec->peer_capacity;
  shard->nack.retransmit_per_second = spec->nack_retransmit_per_second;
  shard->nack.free_head = 0;
  for (size_t ring = spec->peer_capacity; ring > 0; ring--) {
    SfuNackRing *descriptor = &shard->nack.rings[ring - 1];
    descriptor->slots = &shard->nack.slots[(ring - 1) * SFU_NACK_VIDEO_SLOTS];
    descriptor->capacity = 0;
    descriptor->stored = 0;
    descriptor->next_free = shard->nack.free_head;
    shard->nack.free_head = (uint32_t)ring;
  }

  /* The flow table is the one structure that grows on its own: it owns its
   * buckets, so it is created beside the arena rather than inside it. */
  const uint64_t seed = ((uint64_t)getpid() << 32) ^ (uint64_t)time(NULL) ^ (uint64_t)index;
  /* Peer slots start on a free list so expiry can recycle them. */
  shard->peers.free_head = 0;
  for (size_t slot = spec->peer_capacity; slot > 0; slot--) {
    shard->peers.slots[slot - 1].state = SFU_PEER_STATE_FREE;
    shard->peers.slots[slot - 1].slot_next = shard->peers.free_head;
    shard->peers.free_head = (uint32_t)slot;
  }
  shard->peers.count = 0;
  shard->peers.v4 = FlowTableCreate(FLOW_TABLE_KIND_V4, spec->peer_capacity, seed);
  shard->peers.v6 = FlowTableCreate(FLOW_TABLE_KIND_V6, spec->peer_capacity, seed ^ 0x9E3779B97F4A7C15ull);
  if (!shard->peers.v4 || !shard->peers.v6) {
    syslog(LOG_ERR, "%s: COULD NOT create the flow tables of shard %zu", __func__, index);
    return false;
  }

  SfuShardSubscriptionsInit(shard, shard->subscriptions.index, shard->subscriptions.mask + 1u, seed);
  shard->rtcp.interval_ms = spec->rtcp_interval_seconds * 1000u;
  shard->rtcp.pli_min_ms = spec->rtcp_pli_min_interval_ms;
  shard->rtcp.next_rr_ms = (uint64_t)geometry->poll_ms; /* first sweep after the loop starts */
  shard->rtcp.local_ssrc = (uint32_t)(seed ^ (seed >> 32)) | 1u;
  shard->rtcp.cname_length = (uint8_t)snprintf(shard->rtcp.cname, sizeof(shard->rtcp.cname), "ufsrvsfu-%08x",
                                               shard->rtcp.local_ssrc);
  shard->rtcp.jitter_state = (uint32_t)(seed >> 17) | 1u;

  /* The room index is bounded by rooms.max and allocated here rather than in the
   * arena: a shard pays for its room bound, never for a room_pool x peer table. */
  shard->rooms.index = Id64MapCreate(spec->rooms_max, seed ^ 0xD1B54A32D192ED03ull);
  shard->rooms.limit = spec->rooms_max;
  shard->rooms.count = 0;
  shard->rooms.members = 0;
  if (!shard->rooms.index) {
    syslog(LOG_ERR, "%s: COULD NOT create the room map of shard %zu", __func__, index);
    return false;
  }

  mpsc_queue_init(&shard->commands.queue);
  shard->commands.capacity = geometry->command_pool;
  atomic_init(&shard->commands.free, 0);
  for (uint32_t slot = shard->commands.capacity; slot > 0; slot--) {
    CDT_TreiberStackPush(&shard->commands.free, shard->commands.pool, sizeof(SfuCommandNode),
                         offsetof(SfuCommandNode, next_free), slot);
  }

  syslog(LOG_INFO, "%s: shard:'%zu', port:'%u', slab:'%u', peers:'%u'", __func__, index, spec->port, spec->slab_packets,
         spec->peer_capacity);
  return true;
}

static void
sFreeEventEgressSpec(SfuEventEgressSpec *events)
{
  free((void *)events->unix_socket_path);
  free((void *)events->webhook_endpoint);
  free((void *)events->webhook_auth_token);
}

static bool
sCopyEventEgressSpec(SfuEventEgressSpec *owned, const SfuEventEgressSpec *source)
{
  if (source->mode == SFU_EVENT_EGRESS_DISABLED) return true;

  if (!source->queue_capacity || !source->connect_timeout_ms || !source->write_timeout_ms ||
      !source->ice_consent_timeout_ms || source->dominant_speaker_silence_dbov < -127 ||
      source->dominant_speaker_silence_dbov > 0) return false;

  *owned = *source;
  owned->unix_socket_path = NULL;
  owned->webhook_endpoint = NULL;
  owned->webhook_auth_token = NULL;

  switch (source->mode) {
    case SFU_EVENT_EGRESS_UNIX_SOCKET:
      if (!source->unix_socket_path || source->unix_socket_path[0] != '/' ||
          strlen(source->unix_socket_path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) return false;
      owned->unix_socket_path = strdup(source->unix_socket_path);
      return owned->unix_socket_path != NULL;
    case SFU_EVENT_EGRESS_HTTP_WEBHOOK:
      if (!source->webhook_endpoint || !*source->webhook_endpoint ||
          !source->webhook_batch_max_events || !source->webhook_timeout_ms) return false;
      owned->webhook_endpoint = strdup(source->webhook_endpoint);
      if (!owned->webhook_endpoint) return false;
      if (source->webhook_auth_token) {
        owned->webhook_auth_token = strdup(source->webhook_auth_token);
        if (!owned->webhook_auth_token) return false;
      }
      return true;
    case SFU_EVENT_EGRESS_TCP_STREAM:
      return true; /* Destination settings are reserved for the TCP adapter. */
    default:
      return false;
  }
}

SfuShardSet *
SfuShardOnboard(const SfuServerSpec *server)
{
  if (!server || !server->router_count || !server->bind_host || !*server->bind_host || !server->base_port ||
      server->base_port > 65535 || server->router_count > (size_t)(65535 - server->base_port) + 1) {
    return NULL;
  }
  const size_t max_shards = server->max_shards ? server->max_shards : SFU_ONBOARD_MAX_SHARDS;
  if (server->router_count > max_shards) {
    syslog(LOG_ERR, "%s: shard count '%zu' exceeds the supported maximum '%zu'", __func__, server->router_count,
           max_shards);
    return NULL;
  }

  const size_t count = server->router_count;
  const size_t pairs = count * (count - 1);

  SfuShardSet *set = calloc(1, sizeof(*set));
  if (!set) return NULL;
  if (!sCopyEventEgressSpec(&set->events, &server->events)) {
    syslog(LOG_ERR, "%s: invalid or unavailable event egress settings for mode %d", __func__, server->events.mode);
    sFreeEventEgressSpec(&set->events);
    free(set);
    return NULL;
  }
  set->count = count;

  const SfuGeometry geometry = sResolveGeometry(server);

  SfuArena measure = {0};
  sPlanArena(set, &measure, &geometry);
  set->arena_bytes = measure.used;
  set->arena = calloc(1, measure.used);
  if (!set->arena) {
    sFreeEventEgressSpec(&set->events);
    free(set);
    return NULL;
  }

  /* One block, with Memcheck's pool model over it: every carved block is reported against the pool, so a stray write or a missed block is visible in
   * a Valgrind run rather than inferred. */
#if defined(HAVE_VALGRIND_MEMCHECK)
  VALGRIND_CREATE_MEMPOOL(set->arena, 0, 1);
  VALGRIND_MAKE_MEM_NOACCESS(set->arena, measure.used);
#endif

  SfuArena arena = {.base = set->arena, .used = 0};
  sPlanArena(set, &arena, &geometry);

  for (size_t index = 0; index < count; index++) {
    if (!sStartShard(set, index, server, &geometry)) goto fail;
  }

  for (size_t queue = 0; queue < pairs; queue++) {
    LamportQueueInit(&set->queues[queue], &set->queue_payload[queue * (size_t)geometry.queue_depth], geometry.queue_depth);
  }
  return set;

fail:
  syslog(LOG_ERR, "%s: onboarding failed, releasing %zu shard context(s)", __func__, count);
  SfuShardOffboard(set);
  return NULL;
}

/**
 * Cleanup hooks: declaring a local with these attributes makes the release run
 * at the end of the enclosing block, including on an early return. They free a
 * copy of the pointer, so they are safe alongside a later free of the handle
 * that stored it.
 */
static void
sCloseOwned(int *descriptor)
{
  if (*descriptor >= 0) close(*descriptor);
  *descriptor = -1;
}

static void
sFreeOwned(void *pointer)
{
  free(*(void **)pointer);
}

#define SFU_CLEANUP_CLOSE __attribute__((cleanup(sCloseOwned)))
#define SFU_CLEANUP_FREE  __attribute__((cleanup(sFreeOwned)))

void
SfuShardOffboard(SfuShardSet *set)
{
  if (!set) return;
  sFreeEventEgressSpec(&set->events);

  for (size_t index = 0; index < set->count; index++) {
    SfuShard *shard = &set->shards[index];
    SFU_CLEANUP_CLOSE int owned_fd = shard->fd;
    SFU_CLEANUP_CLOSE int owned_wake_fd = shard->spec ? shard->spec->wake_fd : -1;
  }

  /* Threads are joined by now, so any peer that outlived the shutdown still
   * owns an SSL object and its BIOs: release them before the arena goes. */
  for (size_t index = 0; index < set->count; index++) {
    SfuShard *shard = &set->shards[index];
    for (size_t slot = 0; slot < shard->spec->peer_capacity; slot++) {
      if (shard->peers.slots[slot].state == SFU_PEER_STATE_FREE) continue;
      SfuShardDtlsReset(shard, &shard->peers.slots[slot]);
      shard->peers.slots[slot].state = SFU_PEER_STATE_FREE;
    }
  }

  if (set->queues) {
    for (size_t queue = 0; queue < set->count * (set->count - 1); queue++) LamportQueueDestroy(&set->queues[queue]);
  }
  for (size_t index = 0; index < set->count; index++) {
    FlowTableDestroy(set->shards[index].peers.v4);
    FlowTableDestroy(set->shards[index].peers.v6);
    /* A room outlives its members only until the last one leaves; anything left
     * here is a room the shard never released, so free it with its member array. */
    size_t cursor = 0;
    uint64_t room_id = 0;
    void *room = NULL;
    while (Id64MapNext(set->shards[index].rooms.index, &cursor, &room_id, &room)) free(room);
    Id64MapDestroy(set->shards[index].rooms.index);
  }

  /* Destroy the pool model before the block it describes goes away. */
#if defined(HAVE_VALGRIND_MEMCHECK)
  if (set->arena) VALGRIND_DESTROY_MEMPOOL(set->arena);
#endif

  SFU_CLEANUP_FREE void *owned_arena = set->arena;
  SFU_CLEANUP_FREE void *owned_set = set;
}
