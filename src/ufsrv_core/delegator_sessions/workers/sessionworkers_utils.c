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
 * This is the main I/O event listener/dispatcher thread, delegating work to session based worker threads.
 */

#ifdef HAVE_CONFIG_UFNETCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <uflib/standard_defs.h>
#include <ufsrv_core/delegator_sessions/workers/sessionworkers_utils.h>
#include "ufsrv_core/delegator_sessions/workers/worker_session_io_thread.h"
#include <uflib/recycler/instance_type.h>
#include <uflib/cdt/cdt_lamport_queue.h>

#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>


/**
 * @brief Main orchestrator for session worker threads creation. Set the run-time data context necessary to launch session i/o workers
 * and initialise thread-local ThreadContext.
 * @param[in] sd_ptr Singleton reference to Sessions Delegator
 * @param[in] user_thread_ctx_allocation_sz Size of the thread worker specific run-time thread context. sessionworkers use ThreadContext. This will be pre-
 * allocated for all threads.
 */
void
LaunchSessionWorkerThreads(SessionsDelegator * _Nonnull sd_ptr, size_t user_thread_ctx_allocation_sz)
{
  if (IS_PRESENT(sd_ptr)) {
    sd_ptr->session_worker_ths = calloc(sd_ptr->setsize, sizeof(pthread_t));
    WorkerThreadCreationContext *thread_contexts = calloc(sd_ptr->setsize, sizeof(*thread_contexts));
    if (!sd_ptr->session_worker_ths || !thread_contexts) {
      syslog(LOG_ERR, "%s: COULD NOT allocate session worker startup state", __func__);
      exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < sd_ptr->setsize; i++) {
      thread_contexts[i].idx = i;
      const UfsrvDelegatorWorkersUserData *data = &sd_ptr->ufsrv_delegator_userdata;
      if (data->session_worker)
        thread_contexts[i].user_data = (unsigned char *)data->session_worker + i * data->slot_size;
      int result = pthread_create(&sd_ptr->session_worker_ths[i], NULL,
                                  GetSessionWorkerThreadHandler(), &thread_contexts[i]);
      if (result != 0) {
        syslog(LOG_ERR, "%s: COULD NOT spawn session worker %zu: %s", __func__, i, strerror(result));
        exit(EXIT_FAILURE);
      }
    }

    statsd_gauge(sd_ptr->instrumentation_backend_ptr, "worker.work.handshake_failed", 0, UfsrvConfigInstrumentationBackendServerAsPtr());

    syslog(LOG_INFO, "%s: SUCCESSFULLY spawned '%d' Session Worker Threads...", __func__, sd_ptr->setsize);
  }

}
