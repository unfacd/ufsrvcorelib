/**
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif
#include <collection_descriptor_type.h>
#include <standard_defs.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <ufsrvcorelib/ufsrv_console_script/ufsrv_console_script_type.h>
#include <ufsrv_core/delegator_sessions/console_delegator_channel_type.h>
#include <ufsrv_core/delegator_sessions/console_delegator_command_type.h>
#include <ufsrv_core/delegator_sessions/command/session_delegator_command.h>

static ConsoleDelegatorChannel *s_channel;

static void
sReplyText(BufferDescriptor *reply, const char *text)
{
  if (!reply || !text) return;
  BufferDescriptorAppendFormatted(reply, "%s", text);
}

static bool
sCommandInit(ConsoleDelegatorCommand *command)
{
  memset(command, 0, sizeof *command);
  if (pthread_mutex_init(&command->mutex, NULL) != 0) return false;
  if (pthread_cond_init(&command->done, NULL) != 0) {
    pthread_mutex_destroy(&command->mutex);
    return false;
  }
  BufferDescriptorInit(&command->reply, 64);
  if (!command->reply.data) {
    pthread_cond_destroy(&command->done);
    pthread_mutex_destroy(&command->mutex);
    return false;
  }
  command->state = CONSOLE_DELEGATOR_COMMAND_IDLE;
  command->initialised = true;
  return true;
}

static void
sCommandClear(ConsoleDelegatorCommand *command)
{
  if (!command->initialised) return;
  BufferDescriptorRelease(&command->reply);
  pthread_cond_destroy(&command->done);
  pthread_mutex_destroy(&command->mutex);
  command->initialised = false;
}

static void
sWake(const ConsoleDelegatorChannel *channel)
{
  const char token = 'G';

  for (;;) {
    ssize_t wrote = write(channel->write_fd, &token, 1);
    if (wrote == 1) return;
    if (wrote < 0 && errno == EINTR) continue;
    return;
  }
}

static void
sReadWake(const ConsoleDelegatorChannel *channel)
{
  char buf[256];

  if (channel->read_fd < 0) return;
  for (;;) {
    ssize_t n = read(channel->read_fd, buf, sizeof buf);
    if (n > 0) continue;
    if (n < 0 && errno == EINTR) continue;
    return;
  }
}

static ConsoleDelegatorSubmit
sEnqueue(ConsoleDelegatorChannel *channel, ConsoleDelegatorCommand *command, const char *method, const char *path)
{
  size_t method_len = strlen(method);
  size_t path_len = strlen(path);

  if (method_len == 0 || method_len >= CONSOLE_DELEGATOR_METHOD_CAP || path_len >= CONSOLE_DELEGATOR_PATH_CAP) {
    return CONSOLE_DELEGATOR_SUBMIT_MALFORMED;
  }

  pthread_mutex_lock(&command->mutex);
  memcpy(command->method, method, method_len);
  command->method[method_len] = '\0';
  memcpy(command->path, path, path_len);
  command->path[path_len] = '\0';
  if (command->reply.data) {
    command->reply.data[0] = '\0';
    command->reply.size = 0;
  }
  command->state = CONSOLE_DELEGATOR_COMMAND_QUEUED;
  pthread_mutex_unlock(&command->mutex);

  pthread_mutex_lock(&channel->mutex);
  if (channel->count == CONSOLE_DELEGATOR_QUEUE_CAPACITY) {
    pthread_mutex_unlock(&channel->mutex);
    pthread_mutex_lock(&command->mutex);
    command->state = CONSOLE_DELEGATOR_COMMAND_REJECTED;
    pthread_mutex_unlock(&command->mutex);
    return CONSOLE_DELEGATOR_SUBMIT_FULL;
  }
  channel->slots[channel->tail] = command;
  channel->tail = (channel->tail + 1) % CONSOLE_DELEGATOR_QUEUE_CAPACITY;
  channel->count++;
  pthread_mutex_unlock(&channel->mutex);
  sWake(channel);
  return CONSOLE_DELEGATOR_SUBMIT_OK;
}

static void
sWait(ConsoleDelegatorCommand *command)
{
  pthread_mutex_lock(&command->mutex);
  while (command->state != CONSOLE_DELEGATOR_COMMAND_DONE) {
    pthread_cond_wait(&command->done, &command->mutex);
  }
  pthread_mutex_unlock(&command->mutex);
}

PUBLIC_API int
ConsoleDelegatorChannelOpen(ConsoleDelegatorChannel *channel, ConsoleDelegatorHandler handler)
{
  int fds[2];

  if (!channel) return -1;
  if (channel->open) return 0;
  if (pipe2(fds, O_NONBLOCK) == -1) {
    syslog(LOG_ERR, "%s: pipe2 failed errno=%d", __func__, errno);
    return -1;
  }
  if (pthread_mutex_init(&channel->mutex, NULL) != 0) {
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  channel->read_fd = fds[0];
  channel->write_fd = fds[1];
  channel->head = 0;
  channel->tail = 0;
  channel->count = 0;
  channel->handler = handler;
  channel->open = true;
  s_channel = channel;
  return 0;
}

PUBLIC_API int
ConsoleDelegatorChannelReadFd(const ConsoleDelegatorChannel *channel)
{
  if (!channel || !channel->open) return -1;
  return channel->read_fd;
}

PUBLIC_API size_t
ConsoleDelegatorPending(ConsoleDelegatorChannel *channel)
{
  size_t count;

  if (!channel || !channel->open) return 0;
  pthread_mutex_lock(&channel->mutex);
  count = channel->count;
  pthread_mutex_unlock(&channel->mutex);
  return count;
}

PUBLIC_API size_t
ConsoleDelegatorCurrentPending(void)
{
  return s_channel ? ConsoleDelegatorPending(s_channel) : 0;
}

/**
 * @brief Console command handler for the delegator prefix.
 *
 * Posts one command, waits until the delegator finishes it, and appends the reply
 * to reply. The console calls this only because it was registered for that prefix.
 *
 * @param[in]     method  Command verb.
 * @param[in]     path    Command path.
 * @param[in,out] reply   Buffer the console passed to every handler. NULL skips the copy.
 *
 * @return reply.
 */
static BufferDescriptor *
sSessionDelegatorCommand(const char *method, const char *path, BufferDescriptor *reply)
{
  ConsoleDelegatorCommand command;
  ConsoleDelegatorSubmit submit;
  size_t method_len;
  size_t path_len;
  ConsoleDelegatorChannel *channel = s_channel;

  if (!method || !path) {
    sReplyText(reply, "rejected");
    return reply;
  }
  method_len = strlen(method);
  path_len = strlen(path);
  if (method_len == 0 || method_len >= CONSOLE_DELEGATOR_METHOD_CAP || path_len >= CONSOLE_DELEGATOR_PATH_CAP) {
    sReplyText(reply, "rejected");
    return reply;
  }
  if (!channel || !channel->open) {
    sReplyText(reply, "rejected");
    return reply;
  }
  if (!sCommandInit(&command)) {
    sReplyText(reply, "rejected");
    return reply;
  }

  submit = sEnqueue(channel, &command, method, path);
  if (submit == CONSOLE_DELEGATOR_SUBMIT_FULL) {
    sCommandClear(&command);
    sReplyText(reply, "full");
    return reply;
  }
  if (submit != CONSOLE_DELEGATOR_SUBMIT_OK) {
    sCommandClear(&command);
    sReplyText(reply, "rejected");
    return reply;
  }
  sWait(&command);
  if (reply && command.reply.data && command.reply.size > 0) {
    BufferDescriptorAppendFormatted(reply, "%s", command.reply.data);
  }
  sCommandClear(&command);
  return reply;
}

PUBLIC_API void
ConsoleDelegatorDrain(ConsoleDelegatorChannel *channel)
{
  if (!channel || !channel->open) return;
  sReadWake(channel);
  for (;;) {
    ConsoleDelegatorCommand *command;

    pthread_mutex_lock(&channel->mutex);
    if (channel->count == 0) {
      pthread_mutex_unlock(&channel->mutex);
      return;
    }
    command = channel->slots[channel->head];
    channel->slots[channel->head] = NULL;
    channel->head = (channel->head + 1) % CONSOLE_DELEGATOR_QUEUE_CAPACITY;
    channel->count--;
    pthread_mutex_unlock(&channel->mutex);

    pthread_mutex_lock(&command->mutex);
    command->state = CONSOLE_DELEGATOR_COMMAND_RUNNING;
    pthread_mutex_unlock(&command->mutex);

    if (channel->handler) {
      channel->handler(command->method, command->path, &command->reply);
    } else if (command->reply.data) {
      command->reply.data[0] = '\0';
      command->reply.size = 0;
    }

    pthread_mutex_lock(&command->mutex);
    command->state = CONSOLE_DELEGATOR_COMMAND_DONE;
    pthread_cond_signal(&command->done);
    pthread_mutex_unlock(&command->mutex);
  }
}

static const char *
sStemmedPath(ScriptHandlerContext *ctx)
{
  return (ctx && ctx->stemmed_path) ? ctx->stemmed_path : "";
}

static BufferDescriptor *
sDelegatorCommandHandlerOnGet(ScriptHandlerContext *ctx)
{
  return sSessionDelegatorCommand("GET", sStemmedPath(ctx), ctx ? ctx->buffer : NULL);
}

static BufferDescriptor *
sDelegatorCommandHandlerOnDel(ScriptHandlerContext *ctx)
{
  return sSessionDelegatorCommand("DEL", sStemmedPath(ctx), ctx ? ctx->buffer : NULL);
}

static BufferDescriptor *
sDelegatorCommandHandlerOnPut(ScriptHandlerContext *ctx)
{
  return sSessionDelegatorCommand("PUT", sStemmedPath(ctx), ctx ? ctx->buffer : NULL);
}

#include "ufsrv_core/delegator_sessions/command/session_delegator_command_handler.h"

PUBLIC_API UfsrvConsoleScriptHandler *
SessionDelegatorCommandProvideHandler(UfsrvConsoleScriptHandler *handler_provided)
{
  UfsrvConsoleScriptHandler *handler;
  if (IS_EMPTY(handler_provided)) {
    handler = calloc(1, sizeof(UfsrvConsoleScriptHandler));
  } else {
    handler = handler_provided;
  }

  handler->on_del = sDelegatorCommandHandlerOnDel;
  handler->on_get = sDelegatorCommandHandlerOnGet;
  handler->on_put = sDelegatorCommandHandlerOnPut;
  handler->methods = UFSRV_CONSOLE_METHOD_GET | UFSRV_CONSOLE_METHOD_DEL | UFSRV_CONSOLE_METHOD_PUT;
  handler->options.is_stemmed = true;//sub-router: owns everything past /internals/delegator

  return handler;
}
