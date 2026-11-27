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

#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>

#include "udp_server_priv.h"

void *
UdpWorkerThread(void *ptr)
{
  UdpThreadStart *             start      = ptr;
  WorkerThreadCreationContext *th_ctx_ptr = &start->creation;

  ufsrv_thread_context.random_state = time(NULL) ^ getpid() ^ pthread_self();
  ufsrv_thread_context.user_data    = th_ctx_ptr->user_data;
  if (th_ctx_ptr->user_data && start->server->on_worker_start) start->server->on_worker_start(&ufsrv_thread_context);

  {
    char proc_name[16];
    strncpy(proc_name, "ufUdpWorker", sizeof(proc_name) - 1);
    proc_name[sizeof(proc_name) - 1] = 0;
    prctl(PR_SET_NAME, (unsigned long)proc_name);
  }
  UfsrvCpuAffinityPin(UFSRV_THREAD_WORKER, (int)th_ctx_ptr->idx);
  /* Missing: pin to the shard core, not a separate worker-pool slot. */


  UdpShard *shard = start->shard;
  pthread_mutex_lock(&shard->mutex);
  start->ready = true;
  pthread_cond_broadcast(&shard->wake);

  while (atomic_load(&shard->up)) {
    if (shard->state != UDP_WORK_PENDING) {
      pthread_cond_wait(&shard->wake, &shard->mutex);
      continue;
    }

    shard->active_worker = pthread_self();
    shard->state = UDP_WORK_RUNNING;
    pthread_mutex_unlock(&shard->mutex);
    const struct sockaddr *peer = (const struct sockaddr *)&shard->peer;
    UdpServer *server = start->server;
    size_t reply_len;
    if (server->on_flow_datagram) {
      reply_len = server->on_flow_datagram(server, shard->flow, peer, shard->peer_len,
                                           shard->input, shard->input_len, shard->reply, sizeof(shard->reply));
    } else {
      reply_len = server->on_datagram(peer, shard->peer_len, shard->input, shard->input_len,
                                       shard->reply, sizeof(shard->reply));
    }
    pthread_mutex_lock(&shard->mutex);

    shard->reply_len = reply_len;
    shard->state = UDP_WORK_DONE;
    pthread_cond_broadcast(&shard->wake);
  }

  pthread_mutex_unlock(&shard->mutex);
  return NULL;
}
