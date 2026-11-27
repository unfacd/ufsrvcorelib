/*

 Copyright (c) 2015-2026 unfacd works

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

/**
 * @file udp_server_priv.h
 * @brief Internal UDP server and shard operation declarations.
 */

#ifndef UFSRVCORELIB_UDP_SERVER_PRIV_H
#define UFSRVCORELIB_UDP_SERVER_PRIV_H

#include "udp_server_priv_type.h"

bool UdpFlowResolve(UdpServer *server, UdpShard *shard, SSL *candidate, UdpFlowHandle *handle);
UdpFlow *UdpFlowFindDtls(UdpServer *server, UdpShard *shard);
void UdpFlowClose(UdpShard *shard, UdpFlow *flow);
bool UdpFlowTouch(UdpServer *server, UdpShard *shard, UdpFlowHandle handle);
bool UdpFlowIsActive(const UdpServer *server, const UdpShard *shard, UdpFlowHandle handle);
void UdpShardMaintainFlows(UdpServer *server, UdpShard *shard, bool stopping);
void UdpShardDrainSends(UdpServer *server, UdpShard *shard);

void UdpShardProcessDtls(UdpServer *server, UdpShard *shard, size_t length);
void UdpShardSendFlow(UdpShard *shard, UdpFlow *flow, const void *payload, size_t length);
void UdpShardDispatch(UdpServer *server, UdpShard *shard, size_t length);

void *UdpShardThread(void *ptr);
void *UdpWorkerThread(void *ptr);

#endif
