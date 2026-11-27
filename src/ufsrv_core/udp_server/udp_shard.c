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

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <poll.h>
#include <sys/prctl.h>

#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>

#include "udp_server_priv.h"

void
UdpShardDispatch(UdpServer *server, UdpShard *shard, size_t length)
{
  if (server->admit_datagram &&
      !server->admit_datagram(server, (const struct sockaddr *)&shard->peer, shard->peer_len, shard->input, length))
    return;

  pthread_mutex_lock(&shard->mutex);
  if (!atomic_load(&shard->up) ||
      (server->on_flow_datagram && !server->dtls_context &&
       !UdpFlowResolve(server, shard, NULL, &shard->flow))) {
    pthread_mutex_unlock(&shard->mutex);
    return;
  }

  shard->input_len = length;
  shard->state = UDP_WORK_PENDING;
  pthread_cond_broadcast(&shard->wake);
  // Let a running callback finish before releasing any borrowed session.
  while (shard->state == UDP_WORK_RUNNING ||
         (atomic_load(&shard->up) && shard->state == UDP_WORK_PENDING))
    pthread_cond_wait(&shard->wake, &shard->mutex);

  const bool reply_ready = atomic_load(&shard->up) &&
                           shard->reply_len > 0 && shard->reply_len <= sizeof(shard->reply) &&
                           (!server->on_flow_datagram || UdpFlowIsActive(server, shard, shard->flow));
  shard->state = UDP_WORK_IDLE;
  pthread_mutex_unlock(&shard->mutex);

  if (!reply_ready) return;
  if (server->on_flow_datagram) {
    UdpShardSendFlow(shard, &shard->flows[shard->flow.slot], shard->reply, shard->reply_len);
  } else {
    sendto(shard->fd, shard->reply, shard->reply_len, 0,
           (const struct sockaddr *)&shard->peer, shard->peer_len);
  }
}

static void
sShardTurn(UdpServer *server, UdpShard *shard)
{
  struct pollfd events[] = {
    {.fd = shard->fd, .events = POLLIN},
    {.fd = shard->send_wake, .events = POLLIN},
  };
  const int ready = poll(events, 2, 100);
  UdpShardMaintainFlows(server, shard, false);
  if (ready <= 0) return;
  if (events[1].revents & POLLIN) UdpShardDrainSends(server, shard);
  if (!atomic_load(&shard->up) || !(events[0].revents & POLLIN)) return;

  shard->peer_len = sizeof(shard->peer);
  ssize_t length  = recvfrom(shard->fd, shard->input, sizeof(shard->input), 0, (struct sockaddr*)&shard->peer,
                             &shard->peer_len);
  if (length < 0) return;
  if (server->dtls_context) UdpShardProcessDtls(server, shard, (size_t)length);
  else UdpShardDispatch(server, shard, (size_t)length);
}

void *
UdpShardThread(void *ptr)
{
  UdpThreadStart *start = ptr;
  UdpShard *      shard = start->shard;

  prctl(PR_SET_NAME, "ufUdpShard");
  UfsrvCpuAffinityPin(UFSRV_THREAD_LISTENER, (int)shard->index);

  pthread_mutex_lock(&shard->mutex);
  start->ready = true;
  pthread_cond_broadcast(&shard->wake);
  pthread_mutex_unlock(&shard->mutex);

  while (atomic_load(&shard->up)) sShardTurn(start->server, shard);
  UdpShardMaintainFlows(start->server, shard, true);
  return NULL;
}
