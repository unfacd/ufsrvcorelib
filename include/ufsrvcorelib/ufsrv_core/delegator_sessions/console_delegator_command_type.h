/**
 * @file console_delegator_command_type.h
 * @brief console_delegator_command_type
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

#ifndef UFSRVCORELIB_CONSOLE_DELEGATOR_COMMAND_TYPE_H
#define UFSRVCORELIB_CONSOLE_DELEGATOR_COMMAND_TYPE_H


#define CONSOLE_DELEGATOR_METHOD_CAP 16u
#define CONSOLE_DELEGATOR_PATH_CAP 512u

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>

/*!
 * Lifecycle of one command record on the console thread's stack.
 * sWait returns only for DONE. Drain is the only writer of RUNNING and DONE.
 */
typedef enum ConsoleDelegatorCommandState {
  CONSOLE_DELEGATOR_COMMAND_IDLE = 0,  ///< Mutex and cond exist. Not linked into the ring
  CONSOLE_DELEGATOR_COMMAND_QUEUED,    ///< method and path copied. Link follows, unless the ring is full
  CONSOLE_DELEGATOR_COMMAND_RUNNING,   ///< Popped. Drain calls the handler without holding mutex
  CONSOLE_DELEGATOR_COMMAND_DONE,      ///< Reply stored. done is signaled under mutex
  CONSOLE_DELEGATOR_COMMAND_REJECTED   ///< Ring was full after QUEUED. Not linked. Request does not wait
} ConsoleDelegatorCommandState;

/*! Result of posting one command onto the delegator channel. */
typedef enum ConsoleDelegatorSubmit {
  CONSOLE_DELEGATOR_SUBMIT_OK = 0,        ///< Delegator finished; reply was copied when a buffer was supplied
  CONSOLE_DELEGATOR_SUBMIT_MALFORMED = 1, ///< method is NULL or empty or exceeds 15 bytes, path is NULL, or path exceeds 511 bytes
  CONSOLE_DELEGATOR_SUBMIT_FULL = 2,      ///< Channel already holds its in-flight limit; this call did not wait
  CONSOLE_DELEGATOR_SUBMIT_CLOSED = 3     ///< channel is NULL or the delegator has not opened it
} ConsoleDelegatorSubmit;

/*!
 * One in-flight console request: each command cycle gets one of those. It's a common envelope between the requestor and delegator.
 * The Delegator will signal command readiness by raising the /p done condition, which teh requestor will block on.
 * Lives on the stack of the console callback. The channel holds a pointer only while the command is linked.
 * sCommandClear releases reply and destroys mutex and done before Request returns.
 */
typedef struct ConsoleDelegatorCommand {
  char method[CONSOLE_DELEGATOR_METHOD_CAP];  ///< Copy of the verb, including the NUL. Cap is 16
  char path[CONSOLE_DELEGATOR_PATH_CAP];      ///< Copy of the path, including the NUL. Cap is 512
  BufferDescriptor reply;                     ///< Handler JSON. reply.size is the byte count, excluding the NUL
  ConsoleDelegatorCommandState state;         ///< IDLE, QUEUED, RUNNING, DONE, or REJECTED
  bool initialised;                           ///< mutex and done were created. Clear destroys them only when this is set
  pthread_mutex_t mutex;                      ///< Guards state and the buffers at enqueue and completion
  pthread_cond_t done;                        ///< Signaled when state becomes DONE
} ConsoleDelegatorCommand;

#endif //UFSRVCORELIB_CONSOLE_DELEGATOR_COMMAND_TYPE_H
