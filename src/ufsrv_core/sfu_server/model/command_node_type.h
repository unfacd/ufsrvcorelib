/**
 * @file command_node_type.h
 * @brief Intrusive MPSC node carrying one signalling command to a shard.
 *
 * Nodes come from the shard's bounded pool; the queue itself is unbounded and
 * the pool is what bounds it. A node is either on the pool's free stack or in
 * the shard's queue, never both.
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

#ifndef UFSRVCORELIB_COMMAND_NODE_TYPE_H
#define UFSRVCORELIB_COMMAND_NODE_TYPE_H

#include <stdatomic.h>
#include <stdint.h>

#include <uflib/cdt/cdt_mpsc_queue_type.h>

#include "command_reply_type.h"
#include <ufsrvcorelib/ufsrv_core/sfu_server/model/command_type.h>

typedef struct SfuCommandNode {
  _Atomic uint32_t next_free;  /* free-stack link (uflib Treiber), never queued */
  mpsc_queue_node  node;       /* MPSC link while the command is in flight */
  SfuCommand       command;
  SfuCommandReply *reply;      /* requestor's envelope, only for commands that answer */
} SfuCommandNode;

#endif /* UFSRVCORELIB_COMMAND_NODE_TYPE_H */
