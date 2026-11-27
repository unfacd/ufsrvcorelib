/**
 * @file shard_type.h
 * @brief The writable half of a shard: everything only its own thread touches.
 *
 * The immutable contract lives in SfuShardSpec, which the main thread builds
 * before this exists. Nothing here is shared with another shard; cross-shard
 * traffic moves as packets and commands through the queues.
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

#ifndef UFSRVCORELIB_SHARD_TYPE_H
#define UFSRVCORELIB_SHARD_TYPE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include <uflib/adt/flow_table/flow_table.h>
#include <uflib/adt/robinhood_hashtable/id_64bit.h>
#include <uflib/cdt/cdt_mpsc_queue_type.h>
#include <uflib/slab/inlined_slab_type.h>

#include "command_node_type.h"
#include "nack_type.h"
#include "packet_type.h"
#include "outbound_type.h"
#include "peer_type.h"
#include "room_type.h"
#include <ufsrvcorelib/ufsrv_core/sfu_server/model/shard_specs_type.h>

/**
 * One slot of the shard's ufrag index: an open-addressed map from the local
 * ICE ufrag to a peer slot. The hash is stored so a probe can reject most
 * entries without touching the peer record; slot_plus_one 0 is empty.
 */
typedef struct SfuSsrcEntry {
  uint32_t ssrc;
  uint32_t slot_plus_one;
} SfuSsrcEntry;

typedef struct SfuUfragEntry {
  uint32_t slot_plus_one;
  uint32_t hash;
} SfuUfragEntry;

struct SfuDtlsContext;
struct SfuEventEgress;
struct SfuEventEgressSpec;

typedef struct SfuShard {
  const struct SfuEventEgressSpec *event_settings; /* owned by the shard set */
  struct SfuEventEgress *  event_egress;  /* borrowed until shard join */
  struct SfuDtlsContext *  dtls_context;  /* process-wide, owned by SfuServer */
  const SfuShardSpec *     spec;          /* borrowed for the thread lifetime */
  int                      fd;            /* copied from the spec at start */
  int                      epoll_fd;      /* created by the shard thread */
  pthread_t                thread;
  /**
   * @brief Peers: the slot array, its occupancy, the free list and the flow tables.
   *
   * Every index here stores slot numbers, and all of it is created, grown and
   * reclaimed together: a slot cannot outlive its flows.
   */
  struct {
    SfuPeer *   slots;     ///< peer records, indexed by table value
    size_t      capacity;  ///< slots, fixed at onboarding
    size_t      count;     ///< live peer records
    uint32_t    free_head; ///< free slot index + 1; 0 when full
    FlowTable * v4;        ///< (src ip, src port, bound ip, port) -> peer index
    FlowTable * v6;        ///< the IPv6 twin of v4; a dual-stack shard uses both
  } peers;

  /**
   * @brief ICE: the local ufrag index, fixed at onboarding at a 75% load ceiling.
   */
  struct {
    SfuUfragEntry *index;   ///< local ICE ufrag -> peer slot
    size_t         mask;    ///< capacity - 1
    size_t         count;   ///< ufrags currently indexed
  } ice;
  SfuSsrcEntry *           ssrc_index;    /* publisher SSRC -> peer slot */
  size_t                   ssrc_mask;
  size_t                   ssrc_count;
  uint64_t                 next_speaker_ms;
  uint64_t                 next_expiry_ms;/* next idle sweep, monotonic */

  /**
   * @brief RTCP cadence: the receiver-report interval and the PLI throttle.
   *
   * Both come from config and are resolved at onboarding; `next_rr_ms` is the
   * next sweep that may emit reports, so the shard never needs a second timer.
   */
  struct {
    unsigned interval_ms; ///< RR cadence toward publishers
    unsigned pli_min_ms;  ///< per-publisher PLI throttle window
    uint64_t next_rr_ms;  ///< monotonic deadline for the next report sweep
    uint32_t local_ssrc;  ///< SSRC the shard uses as the sender of its own RTCP
    char     cname[32];   ///< SDES CNAME this shard advertises, NUL terminated
    uint8_t  cname_length;///< length of @ref cname
    uint32_t jitter_state;///< xorshift state for interval randomisation
  } rtcp;

  /**
   * @brief Subscriptions: outbound SSRC -> (peer slot, subscription index).
   *
   * Fixed open addressing allocated with the shard, so RTCP feedback resolves a
   * subscriber-visible SSRC without scanning peers. `next_ssrc` is the
   * allocator cursor; see sfu_shard_subscription.c.
   */
  struct {
    SfuOutboundEntry *index;     ///< entry array, allocated at onboarding
    size_t            mask;      ///< capacity - 1
    size_t            count;     ///< bound SSRCs
    uint32_t          next_ssrc; ///< allocator cursor, never zero
  } subscriptions;

  /**
   * @brief NACK retention: the ring descriptors and the budget that bounds them.
   *
   * One pool per shard; a ring is granted to a publisher track and returned when
   * the track goes. `free_head` is the descriptor free-list link, index + 1.
   */
  struct {
    uint64_t    serial;                ///< monotonic retained-packet identity
    SfuNackRing *rings;                 ///< capacity descriptors, linked by next_free
    SfuNackSlot *slots;                 ///< capacity * SFU_NACK_VIDEO_SLOTS entries, sliced per ring
    uint32_t     free_head;             ///< free-list head, index + 1; 0 when exhausted
    size_t       capacity;              ///< descriptors in the pool
    uint32_t     retransmit_per_second; ///< per-subscription budget; 0 disables
  } nack;
  /**
   * @brief Rooms: the id index and the counts that go with it.
   *
   * Control plane only, so nothing in the packet path reads it. A room exists
   * only while it has members; the map is bounded by rooms.max.
   */
  struct {
    Id64Map *index;   ///< room id -> SfuRoom *; bounded by limit
    size_t   limit;   ///< rooms.max: the map's entry bound
    size_t   count;   ///< live rooms
    size_t   members; ///< sum of member_count, maintained on join and leave
  } rooms;
  /**
   * @brief Packets: the slab that owns the bytes and the vectors that reference it.
   *
   * The receive and send batches are sized once from the spec; their iov bases
   * are bound per turn from the slab's free list.
   */
  struct {
    InlinedSlab              slab;         ///< pool over spec->slab, link inside the slot
    struct SfuPacket **      recv_packets; ///< spec->recv_batch slots of the current batch
    struct mmsghdr *         recv_msgs;    ///< spec->recv_batch, main-allocated
    struct iovec *           recv_iov;     ///< iov_base bound from the free list per turn
    struct sockaddr_storage *recv_peers;   ///< spec->recv_batch source addresses
    struct mmsghdr *         send_msgs;    ///< spec->send_batch
    struct iovec *           send_iov;     ///< spec->send_batch
  } packets;
  /**
   * @brief Commands: the queue the shard drains, its node pool and free stack.
   *
   * Many producers (signalling, console), one consumer: the shard thread.
   */
  struct {
    LocklessMpscQueue  queue;    ///< signalling + console, uflib cdt
    SfuCommandNode *   pool;     ///< capacity + 1, index 0 is the sentinel
    uint32_t           capacity; ///< usable nodes
    _Atomic uintptr_t  free;     ///< free-stack head for pool, uflib Treiber
  } commands;
  _Atomic unsigned         up;            /* main writes, the shard thread reads */
} SfuShard;

#endif /* UFSRVCORELIB_SHARD_TYPE_H */
