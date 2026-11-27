/**
 * @file data_sfu_console_router_table.h
 * @brief Routes the /server sub-router answers. Private to sfu_console_router.c.
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

#ifndef UFSRVCORELIB_DATA_SFU_CONSOLE_ROUTER_TABLE_H
#define UFSRVCORELIB_DATA_SFU_CONSOLE_ROUTER_TABLE_H

#include <uflib/resource_router/resource_router_type.h>

static BufferDescriptor *sShardsOnGet(ResourceHandlerContext *ctx);
static BufferDescriptor *sShardOnGet(ResourceHandlerContext *ctx);
static BufferDescriptor *sTransportOnGet(ResourceHandlerContext *ctx);
static BufferDescriptor *sPeerOnPut(ResourceHandlerContext *ctx);
static BufferDescriptor *sRoomOnPut(ResourceHandlerContext *ctx);
static BufferDescriptor *sRoomOnGet(ResourceHandlerContext *ctx);

static const ResourceRouteHandler s_sfu_shards_handler = {
  .on_get = sShardsOnGet,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_sfu_shard_handler = {
  .on_get = sShardOnGet,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_sfu_transport_handler = {
  .on_get = sTransportOnGet,
  .methods = RESOURCE_ROUTE_METHOD_GET,
};

static const ResourceRouteHandler s_sfu_peer_handler = {
  .on_put = sPeerOnPut,
  .methods = RESOURCE_ROUTE_METHOD_PUT,
};

static const ResourceRouteHandler s_sfu_room_handler = {
  .on_put = sRoomOnPut,
  .on_get = sRoomOnGet,
  .methods = RESOURCE_ROUTE_METHOD_GET | RESOURCE_ROUTE_METHOD_PUT,
};

static const ResourceRouteEntry s_sfu_console_routes[] = {
  { "shards", "every shard's contract and counters", &s_sfu_shards_handler, RESOURCE_ROUTE_NONE },
  { "shard", "one shard's live state document; /server/shard/<index>", &s_sfu_shard_handler, RESOURCE_ROUTE_STEMMED },
  { "transport", "SDP-facing transport: /server/transport/<index>", &s_sfu_transport_handler, RESOURCE_ROUTE_STEMMED },
  { "peer", "provision a peer: PUT /server/peer/<shard>/<ufrag>/<pwd>/<sha256-hex>",
    &s_sfu_peer_handler, RESOURCE_ROUTE_STEMMED },
  { "room", "create or inspect a room: PUT/GET /server/room/<shard>/<room-id>", &s_sfu_room_handler, RESOURCE_ROUTE_STEMMED },
};

#endif /* UFSRVCORELIB_DATA_SFU_CONSOLE_ROUTER_TABLE_H */
