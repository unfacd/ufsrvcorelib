/**
 * @file data_session_delegator_command_handler_table.h
 * @brief Routes the delegator command subrouter answers.
 *
 * Colocated data header, private to session_delegator_command_handler.c. Each
 * path is registered verbatim into the delegator's registry; the stem
 * (/internals/delegator) has already been consumed by the console.
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

#ifndef UFNETCORELIB_DATA_SESSION_DELEGATOR_COMMAND_HANDLER_TABLE_H
#define UFNETCORELIB_DATA_SESSION_DELEGATOR_COMMAND_HANDLER_TABLE_H

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>
#include <uflib/resource_router/resource_router_type.h>

BufferDescriptor *SessionDelegatorCommandOnNowSeconds(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnNowMillis(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnNowNanos(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnQueueSize(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnQueues(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnListenerQueue(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnSessionWorkerQueueGet(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnSessionWorkerQueuePut(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnListenerHoldGet(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnListenerHoldPut(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnSessionWorkersPool(ResourceHandlerContext *ctx);
BufferDescriptor *SessionDelegatorCommandOnJobWorkersPool(ResourceHandlerContext *ctx);

static const ResourceRouteHandler s_session_delegator_handler_now_seconds = {
  .on_get  = SessionDelegatorCommandOnNowSeconds,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_session_delegator_handler_now_millis = {
  .on_get  = SessionDelegatorCommandOnNowMillis,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_session_delegator_handler_now_nanos = {
  .on_get  = SessionDelegatorCommandOnNowNanos,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_session_delegator_handler_queue_size = {
  .on_get  = SessionDelegatorCommandOnQueueSize,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_session_delegator_handler_queues = {
  .on_get  = SessionDelegatorCommandOnQueues,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_session_delegator_handler_listener_queue = {
  .on_get  = SessionDelegatorCommandOnListenerQueue,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_session_delegator_handler_session_workers_pool = {
  .on_get  = SessionDelegatorCommandOnSessionWorkersPool,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_session_delegator_handler_job_workers_pool = {
  .on_get  = SessionDelegatorCommandOnJobWorkersPool,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_session_delegator_handler_session_worker_queue = {
  .on_get  = SessionDelegatorCommandOnSessionWorkerQueueGet,
  .on_put  = SessionDelegatorCommandOnSessionWorkerQueuePut,
  .methods = RESOURCE_ROUTE_METHOD_GET | RESOURCE_ROUTE_METHOD_PUT,
};

static const ResourceRouteHandler s_session_delegator_handler_listener_hold = {
  .on_get  = SessionDelegatorCommandOnListenerHoldGet,
  .on_put  = SessionDelegatorCommandOnListenerHoldPut,
  .methods = RESOURCE_ROUTE_METHOD_GET | RESOURCE_ROUTE_METHOD_PUT,
};

static const ResourceRouteEntry s_session_delegator_command_routes[] = {
  { "now",      "wall-clock time in seconds",            &s_session_delegator_handler_now_seconds, 0 },
  { "now/ms",   "wall-clock time in milliseconds",       &s_session_delegator_handler_now_millis,  0 },
  { "now/nano", "wall-clock time in nanoseconds",        &s_session_delegator_handler_now_nanos,   0 },
  { "queue_sz", "delegator console command queue depth", &s_session_delegator_handler_queue_size,  0 },
  { "queues", "every handoff queue's depth in one document", &s_session_delegator_handler_queues, 0 },
  { "listener_queue", "listener-to-delegator connection handoff queue",
    &s_session_delegator_handler_listener_queue, 0 },
  { "session_workers_pool", "session worker pool state and size",
    &s_session_delegator_handler_session_workers_pool, 0 },
  { "job_workers_pool", "job worker pool state, size and queue depth",
    &s_session_delegator_handler_job_workers_pool, 0 },
  { "session_worker_queue", "session worker i/o queue state; pause=on|off",
    &s_session_delegator_handler_session_worker_queue, RESOURCE_ROUTE_STEMMED },
  { "main_listener", "connections accepted by main listener waiting on the delegator; pause=on|off",
    &s_session_delegator_handler_listener_hold, RESOURCE_ROUTE_STEMMED },
};

#define SESSION_DELEGATOR_COMMAND_ROUTE_COUNT \
  (sizeof(s_session_delegator_command_routes) / sizeof(s_session_delegator_command_routes[0]))

#endif
