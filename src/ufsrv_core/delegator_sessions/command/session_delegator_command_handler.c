/**
 * @file session_delegator_command_handler.c
 * @brief Delegator command subrouter: registry lifetime and match-to-callback dispatch.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufnetcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <time.h>

#include <pthread.h>

#include <uflib/adt/adt_queue.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <uflib/resource_router/resource_router.h>

#include <ufsrv_core/delegator_sessions/delegator_session_worker_thread.h>
#include <ufsrv_core/delegator_sessions/session_io_state.h>
#include <ufsrv_core/delegator_sessions/workers_config_descriptor_type.h>

#include <ufsrv_core/delegator_sessions/command/session_delegator_command.h>
#include <ufsrv_core/delegator_sessions/command/session_delegator_command_handler.h>

#include "data_session_delegator_command_handler_table.h"

static ResourceRouteRegistry *s_routes;

static unsigned
sMethodBit(const char *method)
{
  if (!strcasecmp(method, "GET")) return RESOURCE_ROUTE_METHOD_GET;
  if (!strcasecmp(method, "DEL") || !strcasecmp(method, "DELETE")) return RESOURCE_ROUTE_METHOD_DEL;
  if (!strcasecmp(method, "PUT")) return RESOURCE_ROUTE_METHOD_PUT;
  return 0;
}

static ResourceRouteCallback
sCallbackFor(const ResourceRouteHandler *handler, unsigned method)
{
  switch (method) {
  case RESOURCE_ROUTE_METHOD_GET: return handler->on_get;
  case RESOURCE_ROUTE_METHOD_DEL: return handler->on_del;
  case RESOURCE_ROUTE_METHOD_PUT: return handler->on_put;
  default:                        return NULL;
  }
}

static void
sReplyError(BufferDescriptor *reply, const char *reason)
{
  if (!reply || !reply->data) return;
  reply->size    = 0;
  reply->data[0] = '\0';
  BufferDescriptorAppendFormatted(reply, "{\"error\":\"%s\"}", reason);
}

static void
sReplyNumber(BufferDescriptor *reply, const char *field, long long value)
{
  if (!reply || !reply->data) return;
  BufferDescriptorAppendFormatted(reply, "{\"%s\":%lld}", field, value);
}

static unsigned long
sJobWorkersQueueDepth(WorkersConfigDescriptor *pool)
{
  unsigned long queued;

  if (!pool) return 0;

  pthread_mutex_lock(&pool->work_queue_mutex);
  queued = pool->ufsrv_work_queue.nEntries;
  pthread_mutex_unlock(&pool->work_queue_mutex);

  return queued;
}

static const char *
sJobPoolStateName(enum JobWorkersPoolOperatingState state)
{
  switch (state) {
  case POOL_STATE_UP:            return "up";
  case POOL_STATE_SUSPENDED:     return "suspended";
  case POOL_STATE_DOWN:          return "down";
  case POOL_STATE_UNINITIALISED:
  default:                       return "uninitialised";
  }
}

BufferDescriptor *
SessionDelegatorCommandOnNowSeconds(ResourceHandlerContext *ctx)
{
  struct timespec ts;

  if (!ctx || !ctx->buffer) return NULL;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    sReplyError(ctx->buffer, "clock");
    return ctx->buffer;
  }

  sReplyNumber(ctx->buffer, "secs", (long long)ts.tv_sec);
  return ctx->buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnNowMillis(ResourceHandlerContext *ctx)
{
  struct timespec ts;

  if (!ctx || !ctx->buffer) return NULL;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    sReplyError(ctx->buffer, "clock");
    return ctx->buffer;
  }

  sReplyNumber(ctx->buffer, "ms", (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L));
  return ctx->buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnNowNanos(ResourceHandlerContext *ctx)
{
  struct timespec ts;

  if (!ctx || !ctx->buffer) return NULL;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    sReplyError(ctx->buffer, "clock");
    return ctx->buffer;
  }

  sReplyNumber(ctx->buffer, "nano", (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec);
  return ctx->buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnQueueSize(ResourceHandlerContext *ctx)
{
  if (!ctx || !ctx->buffer) return NULL;

  sReplyNumber(ctx->buffer, "queue_sz", (long long)ConsoleDelegatorCurrentPending());
  return ctx->buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnQueues(ResourceHandlerContext *ctx)
{
  if (!ctx || !ctx->buffer) return NULL;

  BufferDescriptorAppendFormatted(
      ctx->buffer,
      "{\"listener_handoff\":%zu,\"awaiting_worker\":%zu,\"job_workers\":%lu,\"console\":%zu}",
      SessionDelegatorIncomingConnectionsQueueSize(),
      SessionIoQueueDepth(),
      sJobWorkersQueueDepth(GetJobWorkersConfigurationDescriptor()),
      ConsoleDelegatorCurrentPending());

  return ctx->buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnListenerQueue(ResourceHandlerContext *ctx)
{
  if (!ctx || !ctx->buffer) return NULL;

  BufferDescriptorAppendFormatted(ctx->buffer, "{\"awaiting_worker\":%zu,\"handoff\":%zu}",
                                  SessionIoQueueDepth(),
                                  SessionDelegatorIncomingConnectionsQueueSize());
  return ctx->buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnSessionWorkersPool(ResourceHandlerContext *ctx)
{
  if (!ctx || !ctx->buffer) return NULL;

  BufferDescriptorAppendFormatted(ctx->buffer, "{\"state\":\"%s\",\"workers\":%zu}",
                                  UfsrvSessionWorkersPoolState() ? "up" : "down",
                                  UfsrvGetSessionWorkersSize());
  return ctx->buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnJobWorkersPool(ResourceHandlerContext *ctx)
{
  WorkersConfigDescriptor *pool;
  unsigned long            queued;
  unsigned long            submitted;
  unsigned long            completed;
  size_t                   workers;
  const char              *state;

  if (!ctx || !ctx->buffer) return NULL;

  pool = GetJobWorkersConfigurationDescriptor();
  if (!pool) {
    BufferDescriptorAppendFormatted(
        ctx->buffer,
        "{\"state\":\"uninitialised\",\"workers\":0,\"queued\":0,\"submitted\":0,\"completed\":0}");
    return ctx->buffer;
  }

  pthread_mutex_lock(&pool->work_queue_mutex);
  queued    = pool->ufsrv_work_queue.nEntries;
  submitted = pool->ufsrv_work_queue.rolling_counter_add;
  completed = pool->ufsrv_work_queue.rolling_counter_de;
  pthread_mutex_unlock(&pool->work_queue_mutex);

  workers = pool->pool_sz;
  state   = sJobPoolStateName(atomic_load(&pool->up_status));

  BufferDescriptorAppendFormatted(
      ctx->buffer,
      "{\"state\":\"%s\",\"workers\":%zu,\"queued\":%lu,\"submitted\":%lu,\"completed\":%lu}",
      state, workers, queued, submitted, completed);

  return ctx->buffer;
}

static int
sParsePauseArgument(const char *argument, unsigned *on)
{
  const char *value;

  if (!argument || strncmp(argument, "pause=", 6) != 0) return -1;

  value = argument + 6;
  if (!strcasecmp(value, "on"))  { *on = 1; return 0; }
  if (!strcasecmp(value, "off")) { *on = 0; return 0; }
  return -1;
}

static BufferDescriptor *
sReplyQueueState(BufferDescriptor *buffer, unsigned paused)
{
  BufferDescriptorAppendFormatted(buffer, "{\"paused\":%u,\"unserviced\":%zu}",
                                  paused ? 1u : 0u, SessionIoQueueDepth());
  return buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnSessionWorkerQueueGet(ResourceHandlerContext *ctx)
{
  if (!ctx || !ctx->buffer) return NULL;

  if (ctx->remainder && *ctx->remainder && strcasecmp(ctx->remainder, "pause") != 0) {
    sReplyError(ctx->buffer, "unknown argument");
    return ctx->buffer;
  }

  return sReplyQueueState(ctx->buffer, SessionDelegatorSessionIoHold());
}

BufferDescriptor *
SessionDelegatorCommandOnSessionWorkerQueuePut(ResourceHandlerContext *ctx)
{
  unsigned on;

  if (!ctx || !ctx->buffer) return NULL;

  if (sParsePauseArgument(ctx->remainder, &on) != 0) {
    sReplyError(ctx->buffer, "expected pause=on|off");
    return ctx->buffer;
  }

  SessionDelegatorSetSessionIoHold(on);
  return sReplyQueueState(ctx->buffer, SessionDelegatorSessionIoHold());
}

static BufferDescriptor *
sReplyListenerHold(BufferDescriptor *buffer)
{
  BufferDescriptorAppendFormatted(
      buffer, "{\"paused\":%u,\"queued\":%zu,\"capacity\":%zu,\"refused\":%lu,\"refused_during_hold\":%lu}",
      SessionDelegatorListenerHold() ? 1u : 0u,
      SessionDelegatorIncomingConnectionsQueueSize(),
      SessionDelegatorIncomingConnectionsQueueCapacity(),
      SessionDelegatorListenerRefused(),
      SessionDelegatorListenerRefusedDuringHold());
  return buffer;
}

BufferDescriptor *
SessionDelegatorCommandOnListenerHoldGet(ResourceHandlerContext *ctx)
{
  if (!ctx || !ctx->buffer) return NULL;

  if (ctx->remainder && *ctx->remainder && strcasecmp(ctx->remainder, "pause") != 0) {
    sReplyError(ctx->buffer, "unknown argument");
    return ctx->buffer;
  }

  return sReplyListenerHold(ctx->buffer);
}

BufferDescriptor *
SessionDelegatorCommandOnListenerHoldPut(ResourceHandlerContext *ctx)
{
  unsigned on;

  if (!ctx || !ctx->buffer) return NULL;

  if (sParsePauseArgument(ctx->remainder, &on) != 0) {
    sReplyError(ctx->buffer, "expected pause=on|off");
    return ctx->buffer;
  }

  SessionDelegatorSetListenerHold(on);
  return sReplyListenerHold(ctx->buffer);
}

PUBLIC_API int
SessionDelegatorCommandHandlerInit(void)
{
  int rc;

  if (s_routes) return 0;

  s_routes = ResourceRouteRegistryCreate();
  if (!s_routes) {
    syslog(LOG_ERR, "%s: could not allocate the command route registry", __func__);
    return -1;
  }

  rc = ResourceRouteRegisterTable(s_routes, s_session_delegator_command_routes,
                                  SESSION_DELEGATOR_COMMAND_ROUTE_COUNT);
  if (rc != 0) {
    syslog(LOG_ERR, "%s: command route table rejected at row %d", __func__, rc);
    ResourceRouteRegistryDestroy(s_routes);
    s_routes = NULL;
    return -1;
  }

  return 0;
}

PUBLIC_API void
SessionDelegatorCommandHandlerDestroy(void)
{
  if (!s_routes) return;

  ResourceRouteRegistryDestroy(s_routes);
  s_routes = NULL;
}

PUBLIC_API void
SessionDelegatorCommandHandlerExecute(const char *method, const char *path, BufferDescriptor *reply)
{
  const ResourceRouteHandler *handler   = NULL;
  const char                 *remainder = NULL;
  ResourceHandlerContext      ctx;
  ResourceRouteCallback       callback;
  unsigned                    method_bit;

  if (!reply || !method || !path) return;

  if (!s_routes) {
    sReplyError(reply, "not initialised");
    return;
  }

  method_bit = sMethodBit(method);
  if (!method_bit) {
    sReplyError(reply, "unknown method");
    return;
  }

  while (*path == '/') {
    path++;
  }
  if (!*path) {
    if (method_bit == RESOURCE_ROUTE_METHOD_GET) {
      if (ResourceRouteRegistryDescribe(s_routes, reply) == NULL) {
        sReplyError(reply, "unavailable");
      }
    } else {
      sReplyError(reply, "missing command");
    }
    return;
  }

  switch (ResourceRouteMatch(s_routes, method_bit, path, &handler, &remainder)) {
  case RESOURCE_ROUTE_MATCH:
    if (!handler) {
      sReplyError(reply, "no handler");
      return;
    }
    callback = sCallbackFor(handler, method_bit);
    if (!callback) {
      sReplyError(reply, "method not allowed");
      return;
    }
    ctx.remainder = remainder;
    ctx.user_data = handler->user_data;
    ctx.buffer    = reply;
    if (callback(&ctx) == NULL) {
      sReplyError(reply, "no reply");
    }
    return;

  case RESOURCE_ROUTE_METHOD_NOT_ALLOWED:
    sReplyError(reply, "method not allowed");
    return;

  case RESOURCE_ROUTE_NO_MATCH:
  default:
    sReplyError(reply, "unknown route");
    return;
  }
}
