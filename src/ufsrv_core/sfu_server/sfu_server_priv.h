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

#ifndef UFSRVCORELIB_SFU_SERVER_PRIV_H
#define UFSRVCORELIB_SFU_SERVER_PRIV_H

#include <uflib/resource_router/resource_router_type.h>

#include <pthread.h>
#include <stdatomic.h>

#include <ufsrv_core/sfu_server/sfu_server.h>
#include <uflib/buffer_descriptor/buffer_descriptor_type.h>

#include "model/counters_type.h"
#include "model/shard_type.h"

/**
 * Everything the main thread allocated for a shard set, in one handle so that
 * teardown is exact: the specs and shard contexts plus the blocks they borrow
 * (queue matrix, slab, counters). Owned by the host that called
 * SfuShardOnboard(); freed by SfuShardOffboard() after every shard thread has
 * been joined.
 */
struct SfuShardSet {
  SfuEventEgressSpec   events;         /* immutable, with owned transport strings */
  size_t               count;
  unsigned char *      arena;           /* one block; every array below points into it */
  size_t               arena_bytes;
  SfuShardSpec *       specs;
  SfuShard *           shards;
  LocklessSpscQueue *  queues;          /* count * (count - 1) */
  QueueClientData **   queue_payload;   /* slots for every queue above */
  LocklessSpscQueue ** inbound_links;   /* count * count, indexed by shard */
  LocklessSpscQueue ** outbound_links;  /* count * count, indexed by shard */
  SfuPacket *          slabs;           /* count * slab_packets */
  SfuCounters *        counters;        /* count */
};

struct SfuDtlsContext;

struct SfuServer {
  SfuShardSet *     set;             /* owned: released by SfuServerStop() */
  size_t            threads_started;
  struct SfuEventEgress *event_egress; /* owned by the server */
  struct SfuDtlsContext *dtls;       /* created before threads, destroyed after join */
  ResourceRouteRegistry *console_routes; /* this instance's /server registry, NULL when not started */
};

/** Entry point of a shard thread: one SfuShard created by onboarding. */
void *SfuShardThread(void *ptr);

/** Render the shard's live state as JSON. Runs on the shard's own thread. */
BufferDescriptor *SfuServerDescribeRoom(SfuServer *server, size_t shard_index, uint64_t room_id, BufferDescriptor *reply);
BufferDescriptor *DescribeShard(SfuShard *shard, BufferDescriptor *reply);

#endif
