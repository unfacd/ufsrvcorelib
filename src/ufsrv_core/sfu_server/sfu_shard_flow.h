/**
 * @file sfu_shard_flow.h
 * @brief The shard's peer index: a flow tuple to peer-slot mapping.
 *
 * One table per address family, because a v4-mapped peer and a native v6 peer
 * must not collide and their keys differ in width. The shard thread is the only
 * caller; nothing here locks.
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

#ifndef UFSRVCORELIB_SFU_SHARD_FLOW_H
#define UFSRVCORELIB_SFU_SHARD_FLOW_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#include "model/shard_type.h"

/** The flow key for one datagram, tagged with the family that owns it. */
typedef struct SfuFlowKey {
  bool           is_v6;
  FlowTableKeyV4 v4;
  FlowTableKeyV6 v6;
} SfuFlowKey;

/** Build the key a datagram from @p source is looked up by. */
SfuFlowKey SfuFlowKeyFromSource(const SfuShard *shard, const struct sockaddr_storage *source);

/** Find the peer slot for a key. False when absent. */
bool SfuFlowFind(const SfuShard *shard, const SfuFlowKey *key, uint32_t *slot);

/** Insert or replace the peer slot for a key. False when the family table rejects it. */
bool SfuFlowInsert(SfuShard *shard, const SfuFlowKey *key, uint32_t slot);

/** Remove a key. False when it was absent or the table rejected it. */
bool SfuFlowErase(SfuShard *shard, const SfuFlowKey *key);

#endif /* UFSRVCORELIB_SFU_SHARD_FLOW_H */
