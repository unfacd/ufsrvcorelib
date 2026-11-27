/**
 * @file session_delegator_command.h
 * @brief Delegator-owned console command channel.
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

#ifndef UFNETCORELIB_SESSION_DELEGATOR_COMMAND_H
#define UFNETCORELIB_SESSION_DELEGATOR_COMMAND_H

#include <stddef.h>
#include <stdint.h>

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>
#include <ufsrvcorelib_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ConsoleDelegatorChannel ConsoleDelegatorChannel;

/**
 * Command body Drain runs on the delegator thread.
 * method and path are the channel's copies. Append the reply to reply.
 */
typedef void (*ConsoleDelegatorHandler)(const char *method, const char *path, BufferDescriptor *reply);

/**
 * @brief Open the wakeup pipe and store the command handler.
 *
 * @param[in,out] channel  Zeroed storage owned by SessionsDelegator.
 * @param[in]     handler  Command body Drain calls. May be NULL.
 *
 * @return 0 when the pipe is open.
 * @return 0 when channel is already open. The stored handler and pipe are left as they are.
 * @return -1 when channel is NULL, pipe2 fails, or the queue mutex cannot be created.
 *
 * @code{.c}
 * if (ConsoleDelegatorChannelOpen(&delegator->console_channel, sExecuteConsoleDelegatorCommand) != 0)
 *   return;
 * @endcode
 */
PUBLIC_API int ConsoleDelegatorChannelOpen(ConsoleDelegatorChannel *channel, ConsoleDelegatorHandler handler);

/**
 * @brief Return the pipe read end for the delegator epoll set.
 *
 * @param[in] channel  Opened channel.
 *
 * @return The read fd while channel is open.
 * @return -1 when channel is NULL or not open.
 *
 * @code{.c}
 * int fd = ConsoleDelegatorChannelReadFd(&delegator->console_channel);
 * if (fd < 0) return;
 * @endcode
 */
PUBLIC_API int ConsoleDelegatorChannelReadFd(const ConsoleDelegatorChannel *channel);

/**
 * @brief Return how many commands are queued and not yet popped.
 *
 * @param[in,out] channel  Opened channel. The count is taken under the channel mutex.
 *
 * @return The queued count while the channel is open.
 * @return 0 when channel is NULL or not open.
 *
 * @code{.c}
 * if (ConsoleDelegatorPending(&delegator->console_channel) == CONSOLE_DELEGATOR_QUEUE_CAPACITY)
 *   reject_further_requests();
 * @endcode
 */
PUBLIC_API size_t ConsoleDelegatorPending(ConsoleDelegatorChannel *channel);

/**
 * @brief Return the queued count of the process-wide console channel.
 *
 * For a command body that runs on the delegator thread and does not hold a
 * channel pointer — the channel is the one Drain is executing for.
 *
 * @return The queued count while the channel is open.
 * @return 0 when no channel was opened.
 *
 * @code{.c}
 * BufferDescriptorAppendFormatted(reply, "{\"queue_sz\":%zu}", ConsoleDelegatorCurrentPending());
 * @endcode
 */
PUBLIC_API size_t ConsoleDelegatorCurrentPending(void);

/**
 * @brief Read the wakeup pipe and run every queued command.
 *
 * Call this on the delegator thread when epoll reports CONSOLE_DELEGATOR_EPOLL_COOKIE,
 * after releasing the session work-queue mutex.
 *
 * @param[in,out] channel  Opened channel. NULL or a closed channel returns without effect.
 *
 * @code{.c}
 * if (event->data.u64 == CONSOLE_DELEGATOR_EPOLL_COOKIE) {
 *   saw_console = 1;
 *   continue;
 * }
 * WorkQueueUnLock(delegator);
 * if (saw_console) ConsoleDelegatorDrain(&delegator->console_channel);
 * @endcode
 */
PUBLIC_API void ConsoleDelegatorDrain(ConsoleDelegatorChannel *channel);

#ifdef __cplusplus
}
#endif

#endif
