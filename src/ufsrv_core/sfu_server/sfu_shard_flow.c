/**
 * @file sfu_shard_flow.c
 * @brief Family-aware wrappers over the shard's two flow tables.
 *
 * A dual-stack socket reports IPv4 peers as v4-mapped v6 addresses; those are
 * unmapped and keyed in the IPv4 table so an IPv4 peer keeps its compact slot.
 * The shard's destination half is constant for its socket, which is all the
 * tuple needs: destination is either the bound address or zero for a wildcard.
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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

#include "sfu_server_priv.h"
#include "sfu_shard_flow.h"

SfuFlowKey
SfuFlowKeyFromSource(const SfuShard *shard, const struct sockaddr_storage *source)
{
  SfuFlowKey key;
  memset(&key, 0, sizeof(key));

  if (source->ss_family == AF_INET6) {
    const struct sockaddr_in6 *peer = (const struct sockaddr_in6 *)source;
    if (!IN6_IS_ADDR_V4MAPPED(&peer->sin6_addr)) {
      key.is_v6 = true;
      memcpy(key.v6.source, &peer->sin6_addr, sizeof(key.v6.source));
      memcpy(key.v6.destination, shard->spec->bound_address6, sizeof(key.v6.destination));
      key.v6.source_port = peer->sin6_port;
      key.v6.destination_port = (uint16_t)shard->spec->port;
      return key;
    }
    memcpy(&key.v4.source_ip, &peer->sin6_addr.s6_addr[12], sizeof(key.v4.source_ip));
    key.v4.destination_ip = shard->spec->bound_address;
    key.v4.source_port = peer->sin6_port;
    key.v4.destination_port = (uint16_t)shard->spec->port;
    return key;
  }

  const struct sockaddr_in *peer = (const struct sockaddr_in *)source;
  key.v4.source_ip = peer->sin_addr.s_addr;
  key.v4.destination_ip = shard->spec->bound_address;
  key.v4.source_port = peer->sin_port;
  key.v4.destination_port = (uint16_t)shard->spec->port;
  return key;
}

bool
SfuFlowFind(const SfuShard *shard, const SfuFlowKey *key, uint32_t *slot)
{
  return key->is_v6 ? FlowTableFindV6(shard->peers.v6, &key->v6, slot)
                    : FlowTableFindV4(shard->peers.v4, &key->v4, slot);
}

bool
SfuFlowInsert(SfuShard *shard, const SfuFlowKey *key, uint32_t slot)
{
  return (key->is_v6 ? FlowTableInsertV6(shard->peers.v6, &key->v6, slot)
                     : FlowTableInsertV4(shard->peers.v4, &key->v4, slot)) != FLOW_TABLE_ERROR;
}

bool
SfuFlowErase(SfuShard *shard, const SfuFlowKey *key)
{
  return key->is_v6 ? FlowTableEraseV6(shard->peers.v6, &key->v6) : FlowTableEraseV4(shard->peers.v4, &key->v4);
}
