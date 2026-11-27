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

#include <stdlib.h>

#include "uflib/main_types.h"
#include "ufsrv_core/jobworkers/worker_job_descriptor_type.h"
#include "ufsrv_core/delegator_sessions/workers/user_callback_job.h"
#include "ufsrv_core/delegator_sessions/workers/user_callback_job_descriptor_type.h"
#include "uflib/adt/adt_queue.h"

/**
 * Generic job executor which runs arbitrary user define jobs. This is complementary protocol to WorkerJobDescriptor
 * interface, running jobs on worker threads.
 * @param ctx_data UserCallbackJobDescriptor type describing job execution context
 * @return
 */
__attribute__((nonnull(1)))
int WorkerThreadUserCallbackJobExecutor(MessageContextData *ctx_data)
{
  UserCallbackJobDescriptor *callback_descriptor = AS_USER_CALLBACK_JOB_DESCRIPTOR(ctx_data);
  UserCallBackReturnArgs *return_args = callback_descriptor->handler(callback_descriptor->callback_args, &(InstanceHolder){});

  if (!IS_EMPTY(callback_descriptor->on_error_handler)) {
    callback_descriptor->on_error_handler(return_args);
  }

  if (IS_PRESENT(callback_descriptor->finaliser)) callback_descriptor->finaliser(callback_descriptor);

  return 0;
}

#include "ufsrv_core/delegator_sessions/sessions_delegator_type.h"
#include "ufsrv_core/delegator_sessions/delegator_session_worker_thread.h"

__attribute__((nonnull(1), access(read_only, 1)))
bool QueueInUserCallBackJob(UserCallbackJobDescriptor *callback_descriptor)
{
  WorkersConfigDescriptor *jobworkers_config    = GetJobWorkersConfigurationDescriptor();
  pthread_mutex_lock(&jobworkers_config->work_queue_mutex);

  if (unlikely(jobworkers_config->up_status != POOL_STATE_UP)) {
    pthread_mutex_unlock(&jobworkers_config->work_queue_mutex);

    return false;
  }

  QueueEntry *qe_ptr = AddQueue(&(jobworkers_config->ufsrv_work_queue));//remember this is mutex protected

  MessageQueueMsgPayload *mqp_ptr = calloc(1, sizeof(MessageQueueMsgPayload));
  mqp_ptr->delegator_type = DELEGTYPE_USER_CALLBACK;
  mqp_ptr->payload        = (void *)callback_descriptor;
  qe_ptr->whatever = mqp_ptr;

  pthread_cond_broadcast(&jobworkers_config->queue_not_empty_cond);
  pthread_mutex_unlock(&jobworkers_config->work_queue_mutex);

  return true;
}
