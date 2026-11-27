/**
 * @file command_reply_type.h
 * @brief Completion envelope for a signal command that expects a reply.
 *
 * The requestor allocates one, queues it with the command and waits on it under
 * a deadline. The shard fills the reply buffer and signals done. If the
 * deadline expires first the requestor marks the envelope ABANDONED and stops
 * touching it: the shard frees it when the command eventually completes, so a
 * wedged shard can never hang the console and a late reply is never written
 * into a dead stack frame.
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

#ifndef UFSRVCORELIB_COMMAND_REPLY_TYPE_H
#define UFSRVCORELIB_COMMAND_REPLY_TYPE_H

#include <pthread.h>
#include "../sfu_peer_config_priv.h"
#include <stdbool.h>

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>

/**
 * Lifecycle of one in-flight signal command. DONE is terminal for the requestor;
 * ABANDONED is terminal for it too, and tells the shard to free the envelope.
 */
typedef enum SfuCommandState {
  SFU_COMMAND_STATE_IDLE = 0,
  SFU_COMMAND_STATE_QUEUED,
  SFU_COMMAND_STATE_RUNNING,
  SFU_COMMAND_STATE_DONE,
  SFU_COMMAND_STATE_ABANDONED
} SfuCommandState;

typedef struct SfuCommandReply {
  SfuPeerConfigSnapshot *config; ///< Owned request payload, in the envelope allocation
  SfuPeerConfigResult config_result;
  BufferDescriptor reply;       ///< Shard-owned JSON. size excludes the NUL
  SfuCommandState  state;       ///< Guarded by mutex
  bool             accepted;   ///< Result of a command that reports admission
  pthread_mutex_t  mutex;       ///< Guards state
  pthread_cond_t   done;        ///< Signalled when state becomes DONE
} SfuCommandReply;

#endif /* UFSRVCORELIB_COMMAND_REPLY_TYPE_H */
