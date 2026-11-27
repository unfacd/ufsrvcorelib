/**
 * @file shard_specs_type.h
 * @brief The main-thread contract handed to every shard.
 *
 * The main thread builds the array before any shard exists and the shard thread
 * never writes to its entry. Anything shared and mutable is deliberately absent:
 * it crosses threads as packets and commands, never as a field here.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 21/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRVCORELIB_SHARD_SPECS_TYPE_H
#define UFSRVCORELIB_SHARD_SPECS_TYPE_H

#include <stddef.h>
#include <stdint.h>

#include <uflib/cdt/cdt_lamport_queue_type.h>

/* This shard's own ICE credentials. A browser signs its checks with the
 * credentials from the SDP answer, so they cannot be the peer's. */
#define SFU_ICE_UFRAG_LENGTH 8u
#define SFU_ICE_PWD_LENGTH   24u

/* Main thread builds this before any shard exists; read-only afterwards. */
typedef struct SfuShardSpec {
  size_t                    index;
  size_t                    shard_count;   /* how many shards this set holds */
  unsigned                  port;          /* router port this shard serves */
  int                       family;        /* AF_INET, or AF_INET6 for a dual-stack socket */
  uint32_t                  bound_address; /* IPv4 address as bound, network order; 0 when not bound v4 */
  uint8_t                   bound_address6[16]; /* IPv6 address as bound; zero for a wildcard bind */
  char                      ice_ufrag[SFU_ICE_UFRAG_LENGTH + 1u]; /* shard's own ufrag, NUL terminated */
  char                      ice_pwd[SFU_ICE_PWD_LENGTH + 1u];     /* shard's own pwd, NUL terminated */
  int                       fd;            /* bound by main: a port clash fails startup */
  int                       wake_fd;       /* eventfd, cross-shard kick */
  const struct SfuConfigSnapshot *config;  /* immutable config snapshot */
  const struct SfuSharedTables *  shared;  /* read-only registries */
  LocklessSpscQueue *const *inbound;       /* N-1 queues this shard drains */
  LocklessSpscQueue *const *outbound;      /* N-1 queues this shard pushes into */
  struct SfuPacket *        slab;          /* pre-allocated packet array, base */
  unsigned                  slab_packets;
  unsigned                  recv_batch;
  unsigned                  send_batch;
  unsigned                  peer_capacity;
  unsigned                  rooms_max;      /* room-id map bound per shard */
  unsigned                  queue_depth;
  unsigned                  poll_ms;
  unsigned                  peers_idle_timeout_seconds;
  unsigned                  ice_consent_timeout_seconds;
  unsigned                  rtcp_interval_seconds;
  unsigned                  rtcp_pli_min_interval_ms;
  unsigned                  nack_retransmit_per_second;
  struct SfuCounters *      counters;      /* per-shard counters, read by console */
} SfuShardSpec;

#endif //UFSRVCORELIB_SHARD_SPECS_TYPE_H
