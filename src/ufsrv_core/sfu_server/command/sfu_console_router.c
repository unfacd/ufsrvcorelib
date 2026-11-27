/**
 * @file sfu_console_router.c
 * @brief Resolve \<server/\<route\> for the command console.
 *
 * Mirrors the sessions delegator's console sub-router: one stemmed console
 * handler owns the prefix, a resource router matches the remainder, and the
 * route callback appends JSON to the console's reply buffer.
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

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <uflib/resource_router/resource_router.h>

#include "data_sfu_console_router_table.h"

#include <ufsrvcorelib/ufsrv_core/sfu_server/command/sfu_console_router.h>

#include "sfu_server_priv.h"

/* The registry lives on the server it serves: a host may run several protocol
 * instances, so nothing here may be process-global. */
static SfuServer *
sServer(ResourceHandlerContext *ctx)
{
  return ctx ? (SfuServer *)ctx->user_data : NULL;
}

static void
sReplyError(BufferDescriptor *reply, const char *reason)
{
  if (!reply || !reply->data) return;
  reply->size = 0;
  reply->data[0] = '\0';
  BufferDescriptorAppendFormatted(reply, "{\"error\":\"%s\"}", reason);
}

static size_t
sQueueDepth(const SfuShardSpec *spec)
{
  for (size_t other = 0; other < spec->shard_count; other++) {
    if (spec->inbound[other]) return spec->inbound[other]->queue_sz;
  }
  return 0;
}

/**
 * The array answers from immutable spec data plus atomically read counters, so
 * it never blocks on a shard. The authoritative live view of one shard is
 * /server/shard/\<index\>, which the shard's own thread renders.
 */
static BufferDescriptor *
sShardsOnGet(ResourceHandlerContext *ctx)
{
  SfuServer *server = sServer(ctx);
  if (!ctx || !ctx->buffer || !server) return ctx ? ctx->buffer : NULL;

  const SfuShardSet *set = server->set;
  BufferDescriptor *  reply = ctx->buffer;

  BufferDescriptorAppendFormatted(reply, "{\"count\":%zu,\"shards\":[", set->count);
  for (size_t index = 0; index < set->count; index++) {
    const SfuShardSpec *spec = &set->specs[index];
    const SfuCounters * counters = spec->counters;
    if (index) BufferDescriptorAppendFormatted(reply, ",");
    BufferDescriptorAppendFormatted(
        reply,
        "{\"index\":%zu,\"port\":%u,\"fd\":%d,\"wake_fd\":%d,\"slab_packets\":%u,\"recv_batch\":%u,"
        "\"send_batch\":%u,\"peer_capacity\":%u,\"queue_depth\":%zu,\"commands_applied\":%llu,"
        "\"commands_dropped\":%llu,\"rx_datagrams\":%llu,\"tx_datagrams\":%llu}",
        spec->index, spec->port, spec->fd, spec->wake_fd, spec->slab_packets, spec->recv_batch, spec->send_batch,
        spec->peer_capacity, sQueueDepth(spec),
        (unsigned long long)__atomic_load_n(&counters->commands_applied, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&counters->commands_dropped, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&counters->rx_datagrams, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&counters->tx_datagrams, __ATOMIC_RELAXED));
  }
  BufferDescriptorAppendFormatted(reply, "]}");
  return reply;
}

/**
 * /server/shard/\<index\>: the command goes to that shard's own thread, which
 * renders its state document, and this call waits for it — the same hand-off
 * the console uses to reach the delegator.
 */
static BufferDescriptor *
sShardOnGet(ResourceHandlerContext *ctx)
{
  SfuServer *server = sServer(ctx);
  if (!ctx || !ctx->buffer) return ctx ? ctx->buffer : NULL;
  if (!server) {
    sReplyError(ctx->buffer, "no server");
    return ctx->buffer;
  }

  const char *remainder = ctx->remainder ? ctx->remainder : "";
  while (*remainder == '/') remainder++;
  if (!*remainder) {
    sReplyError(ctx->buffer, "usage: /server/shard/<index>");
    return ctx->buffer;
  }

  char *         end = NULL;
  const unsigned long index = strtoul(remainder, &end, 10);
  if (!end || *end != '\0' || index >= server->set->count) {
    sReplyError(ctx->buffer, "unknown shard");
    return ctx->buffer;
  }

  return SfuServerDescribeShard(server, (size_t)index, ctx->buffer);
}

/**
 * /server/transport/&lt;index&gt;: what signalling needs to write one SDP answer -
 * the shard's candidate, its own ICE credentials and the DTLS fingerprint.
 */
static BufferDescriptor *
sTransportOnGet(ResourceHandlerContext *ctx)
{
  SfuServer *server = sServer(ctx);
  if (!ctx || !ctx->buffer) return ctx ? ctx->buffer : NULL;
  if (!server) {
    sReplyError(ctx->buffer, "no server");
    return ctx->buffer;
  }

  const char *remainder = ctx->remainder ? ctx->remainder : "";
  while (*remainder == '/') remainder++;
  if (!*remainder) {
    sReplyError(ctx->buffer, "usage: /server/transport/<index>");
    return ctx->buffer;
  }

  char *              end = NULL;
  const unsigned long index = strtoul(remainder, &end, 10);
  if (!end || *end != '\0' || index >= server->set->count) {
    sReplyError(ctx->buffer, "unknown shard");
    return ctx->buffer;
  }

  return SfuServerDescribeTransport(server, (size_t)index, ctx->buffer);
}


/** Hex to bytes; returns the byte count or 0 on a malformed string. */
static size_t
sHexToBytes(const char *hex, uint8_t *out, size_t out_size)
{
  if (!hex || !hex[0] || strlen(hex) != out_size * 2u) return 0;
  for (size_t index = 0; index < out_size; index++) {
    unsigned value = 0;
    for (size_t nibble = 0; nibble < 2; nibble++) {
      const char digit = hex[index * 2u + nibble];
      unsigned   part;
      if (digit >= '0' && digit <= '9') part = (unsigned)(digit - '0');
      else if (digit >= 'a' && digit <= 'f') part = (unsigned)(digit - 'a' + 10);
      else if (digit >= 'A' && digit <= 'F') part = (unsigned)(digit - 'A' + 10);
      else return 0;
      value = (value << 4) | part;
    }
    out[index] = (uint8_t)value;
  }
  return out_size;
}

/**
 * PUT /server/peer/\<shard\>/\<ufrag\>/\<password\>/\<sha256-hex\>
 *
 * A provisioning seam for the local flow probe: signalling is not
 * implemented yet, and the probe has to tell a running server which
 * credentials and fingerprint to expect. The console is TLS-protected and
 * admin-only; this route disappears once the signalling bridge exists.
 */
static BufferDescriptor *
sPeerOnPut(ResourceHandlerContext *ctx)
{
  SfuServer *server = sServer(ctx);
  if (!ctx || !ctx->buffer) return ctx ? ctx->buffer : NULL;
  if (!server) {
    sReplyError(ctx->buffer, "no server");
    return ctx->buffer;
  }

  const char *remainder = ctx->remainder ? ctx->remainder : "";
  while (*remainder == '/') remainder++;

  char fields[4][SFU_COMMAND_UFRAG_MAX + 1];
  size_t field = 0;
  const char *cursor = remainder;
  while (field < 3 && *cursor) {
    const char *slash = strchr(cursor, '/');
    const size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
    if (!length || length > SFU_COMMAND_UFRAG_MAX) {
      sReplyError(ctx->buffer, "usage: /server/peer/<shard>/<ufrag>/<pwd>/<sha256-hex>");
      return ctx->buffer;
    }
    memcpy(fields[field], cursor, length);
    fields[field][length] = '\0';
    field++;
    if (!slash) break;
    cursor = slash + 1;
  }

  char *         end = NULL;
  const unsigned long shard = field == 3 ? strtoul(fields[0], &end, 10) : 0;
  if (field != 3 || !end || *end != '\0' || shard >= server->set->count) {
    sReplyError(ctx->buffer, "usage: /server/peer/<shard>/<ufrag>/<pwd>/<sha256-hex>");
    return ctx->buffer;
  }

  uint8_t fingerprint[32];
  if (!sHexToBytes(cursor, fingerprint, sizeof(fingerprint))) {
    sReplyError(ctx->buffer, "fingerprint must be 64 hex characters");
    return ctx->buffer;
  }

  if (!SfuServerRegisterPeer(server, (size_t)shard, 0, fields[1], fields[2], fingerprint)) {
    sReplyError(ctx->buffer, "registration rejected");
    return ctx->buffer;
  }

  BufferDescriptorAppendFormatted(ctx->buffer, "{\"registered\":true,\"shard\":%lu,\"ufrag\":\"%s\"}", shard,
                                  fields[1]);
  return ctx->buffer;
}

/**
 * @brief PUT /server/room/\<shard\>/\<room_id\>: create a room.
 *
 * The room half of the provisioning seam: signalling is not wired yet, so a
 * probe or an operator creates a room here and reads /server/shard/\<n\> for the
 * shard's room count and bound. Ids are the signalling server's to choose; this
 * route only carries them.
 *
 * @param ctx Console request context; its buffer receives the JSON reply.
 * @return The reply buffer, always: success carries the created room, failure
 *         carries an error string and no side effect.
 */
static BufferDescriptor *
sRoomRequest(ResourceHandlerContext *ctx, bool describe)
{
  SfuServer *server = sServer(ctx);
  if (!ctx || !ctx->buffer) return ctx ? ctx->buffer : NULL;
  if (!server) {
    sReplyError(ctx->buffer, "no server");
    return ctx->buffer;
  }

  const char *remainder = ctx->remainder ? ctx->remainder : "";
  while (*remainder == '/') remainder++;

  char        shard_text[12];
  const char *slash = strchr(remainder, '/');
  const size_t shard_length = slash ? (size_t)(slash - remainder) : strlen(remainder);
  if (!slash || !shard_length || shard_length >= sizeof(shard_text)) {
    sReplyError(ctx->buffer, "usage: /server/room/<shard>/<room-id>");
    return ctx->buffer;
  }
  memcpy(shard_text, remainder, shard_length);
  shard_text[shard_length] = '\0';

  char *        end = NULL;
  const unsigned long shard = strtoul(shard_text, &end, 10);
  if (!end || *end != '\0' || shard >= server->set->count) {
    sReplyError(ctx->buffer, "usage: /server/room/<shard>/<room-id>");
    return ctx->buffer;
  }

  const char *       room_text = slash + 1;
  const unsigned long long room_id = strtoull(room_text, &end, 10);
  if (!end || *end != '\0' || !room_id) {
    sReplyError(ctx->buffer, "usage: /server/room/<shard>/<room-id>");
    return ctx->buffer;
  }

  if (describe) return SfuServerDescribeRoom(server, (size_t)shard, (uint64_t)room_id, ctx->buffer);
  if (!SfuServerCreateRoom(server, (size_t)shard, (uint64_t)room_id)) {
    sReplyError(ctx->buffer, "room rejected");
    return ctx->buffer;
  }
  BufferDescriptorAppendFormatted(ctx->buffer, "{\"created\":true,\"shard\":%lu,\"room_id\":%llu}", shard,
                                  room_id);
  return ctx->buffer;
}

static BufferDescriptor *
sRoomOnPut(ResourceHandlerContext *ctx)
{
  return sRoomRequest(ctx, false);
}

static BufferDescriptor *
sRoomOnGet(ResourceHandlerContext *ctx)
{
  return sRoomRequest(ctx, true);
}

PUBLIC_API int
SfuCommandConsoleAttachResourceRouter(SfuServer *server)
{
  if (!server) return -1;
  if (server->console_routes) return 0;

  const size_t routes = sizeof(s_sfu_console_routes) / sizeof(s_sfu_console_routes[0]);
  ResourceRouteEntry *  entries = calloc(routes, sizeof(*entries));
  ResourceRouteHandler *handlers = calloc(routes, sizeof(*handlers));
  if (!entries || !handlers) {
    free(entries);
    free(handlers);
    return -1;
  }

  /* Each instance gets handler copies carrying its own server, so two
   * instances of this protocol cannot answer for one another. */
  for (size_t i = 0; i < routes; i++) {
    handlers[i] = *s_sfu_console_routes[i].handler;
    handlers[i].user_data = server;
    entries[i] = s_sfu_console_routes[i];
    entries[i].handler = &handlers[i];
  }

  ResourceRouteRegistry *registry = ResourceRouteRegistryCreate();
  if (!registry || ResourceRouteRegisterTable(registry, entries, routes) != 0) {
    ResourceRouteRegistryDestroy(registry);
    free(entries);
    free(handlers);
    return -1;
  }

  free(entries);
  free(handlers);
  server->console_routes = registry;
  return 0;
}

PUBLIC_API void
SfuCommandConsoleDetachResourceRouter(SfuServer *server)
{
  if (!server) return;

  ResourceRouteRegistryDestroy(server->console_routes);
  server->console_routes = NULL;
}

PUBLIC_API BufferDescriptor *
SfuCommandConsoleRouteResource(SfuServer *server, const char *method, const char *path, BufferDescriptor *reply)
{
  if (!reply) return NULL;
  if (!server || !server->console_routes) {
    sReplyError(reply, "server router not started");
    return reply;
  }

  unsigned method_bit = 0;
  if (!method || strcmp(method, "GET") == 0) method_bit = RESOURCE_ROUTE_METHOD_GET;
  else if (strcmp(method, "DEL") == 0) method_bit = RESOURCE_ROUTE_METHOD_DEL;
  else if (strcmp(method, "PUT") == 0) method_bit = RESOURCE_ROUTE_METHOD_PUT;
  if (!method_bit) {
    sReplyError(reply, "unsupported method");
    return reply;
  }

  /* The console passes the remainder with a trailing separator for /server/x/. */
  char normalised[256];
  const char *source = path ? path : "";
  while (*source == '/') source++;
  const size_t length = strlen(source);
  if (length >= sizeof(normalised)) {
    sReplyError(reply, "path too long");
    return reply;
  }
  memcpy(normalised, source, length);
  normalised[length] = '\0';
  while (length && normalised[length - 1] == '/') normalised[length - 1] = '\0';

  const ResourceRouteHandler *handler = NULL;
  const char *                remainder = NULL;
  switch (ResourceRouteMatch(server->console_routes, method_bit, normalised, &handler, &remainder)) {
    case RESOURCE_ROUTE_MATCH: {
      if (!handler) {
        sReplyError(reply, "no handler");
        return reply;
      }
      ResourceRouteCallback callback = method_bit == RESOURCE_ROUTE_METHOD_GET   ? handler->on_get
                                       : method_bit == RESOURCE_ROUTE_METHOD_DEL ? handler->on_del
                                                                                 : handler->on_put;
      if (!callback) {
        sReplyError(reply, "method not allowed");
        return reply;
      }
      ResourceHandlerContext ctx = {.remainder = remainder, .user_data = handler->user_data, .buffer = reply};
      return callback(&ctx);
    }
    case RESOURCE_ROUTE_METHOD_NOT_ALLOWED:
      sReplyError(reply, "method not allowed");
      return reply;
    case RESOURCE_ROUTE_NO_MATCH:
    default:
      sReplyError(reply, "unknown shard command");
      return reply;
  }
}
