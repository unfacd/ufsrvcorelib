/**
 * @file console_delegator_channel_type_priv.h
 * @brief Private console-delegator queue. Embedded in SessionsDelegator.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufnetcorelib source code.
 * Created by ayman on 14/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_CONSOLE_DELEGATOR_CHANNEL_TYPE_H
#define UFSRVCORELIB_CONSOLE_DELEGATOR_CHANNEL_TYPE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ufsrv_core/delegator_sessions/console_delegator_command_type.h>

typedef struct ConsoleDelegatorChannel ConsoleDelegatorChannel;

/*! epoll_event.data.u64 for this pipe. SessionIoRegister does not issue this value. */
#define CONSOLE_DELEGATOR_EPOLL_COOKIE UINT64_MAX
/*! Queued commands the channel holds. One more request returns CONSOLE_DELEGATOR_SUBMIT_FULL. */
#define CONSOLE_DELEGATOR_QUEUE_CAPACITY 32u

typedef void (*ConsoleDelegatorHandler)(const char *method, const char *path, BufferDescriptor *reply);

/*!
 * @brief Console command ring and the pipe that wakes the delegator.
 *
 * Embedded in SessionsDelegator. Zero it before ConsoleDelegatorChannelOpen.
 */
struct ConsoleDelegatorChannel {
  ConsoleDelegatorCommand *slots[CONSOLE_DELEGATOR_QUEUE_CAPACITY]; ///< Queued commands. The oldest is at head
  size_t head; ///< Index Drain pops next
  size_t tail; ///< Index the next accepted request uses
  size_t count; ///< Commands waiting for Drain
  pthread_mutex_t mutex; ///< Guards slots, head, tail, and count
  int read_fd; ///< Pipe read end. Valid only while open is true
  int write_fd; ///< Pipe write end. Each accepted request writes one byte
  bool open; ///< True after ConsoleDelegatorChannelOpen succeeds
  ConsoleDelegatorHandler handler; ///< Drain calls this. NULL finishes the command with an empty reply
};

#endif
