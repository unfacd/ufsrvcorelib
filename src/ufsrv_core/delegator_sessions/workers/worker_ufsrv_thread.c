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

/**
 * This is a worker thread responsible for executing jobs assigned through the job queue using broadcast semantics.
 */
#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <sys/prctl.h>//for naming thread
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <ufsrv_core/delegator_sessions/thread_context_type.h>
#include "ufsrv_core/delegator_sessions/workers/worker_ufsrv_thread.h"
#include "ufsrv_core/delegator_sessions/base_thread_context_data_type.h"
#include "ufsrv_core/http/http_request.h"
#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend.h>
#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>
#include "ufsrv_core/delegator_sessions/sessions_delegator_type.h"
#include "uflib/scheduled_jobs/scheduled_jobs.h"
#include "ufsrv_core/jobworkers/worker_job_descriptor_type.h"
#include "ufsrv_core/delegator_sessions/workers/user_callback_job.h"
#include "ufsrv_core/delegator_sessions/workers/ufsrvworker_pool_descriptor_type.h"

static MessageContextData *_WorkerThreadScheduledJobExtractArg(MessageQueueMsgPayload *msgqueue_payload_ptr);
static MessageContextData *_WorkerThreadGenericArgsExtractor(MessageQueueMsgPayload *msgqueue_payload_ptr);

//see scheduled_jobs.c as an example of how to make this dynamic and registerable at start time
//lookup table for matching delegator type to associated callback function
static WorkerJobDescriptor worker_job_specs[] = {
    {DELEGTYPE_UNINITIALISED,         NULL, NULL, 	                                          NULL				                              },
    {DELEGTYPE_USER_CALLBACK,         NULL, WorkerThreadUserCallbackJobExecutor,              _WorkerThreadGenericArgsExtractor         },
		{DELEGTYPE_TIMER,	 		            NULL, WorkerThreadScheduledJobExecutor,                 _WorkerThreadScheduledJobExtractArg				},
		{DELEGTYPE_MSGQUEUE, 	            NULL, NULL,                WorkerThreadMessageQueueParserExtractArg	},
    {DELEGTYPE_TIMER_FIRST_INSERTED,  NULL, WorkerThreadScheduledJobFirstInsertedExecutor,    _WorkerThreadScheduledJobExtractArg				},
    {DELEGTYPE_INVALID, 	            NULL, NULL, 	                                          NULL	                                    },
};

//todo this replaces old pthread thread-specific storage keys. Storage declared in delegator_session_io_worker_thread.c
extern __thread ThreadContext      ufsrv_thread_context; //thread local defined in delegator_session_io_worker_thread.c

//originally allocation was defined in old sfu delegator and is currently in delegator_session_io_worker_thread.c
extern __thread BaseThreadContext *base_thread_context;

__attribute__((const)) MessageContextData *
WorkerThreadMessageQueueParserExtractArg(MessageQueueMsgPayload *msgqueue_payload_ptr)
{
  return ((ClientContextData *)msgqueue_payload_ptr);
}

__attribute__((const)) static MessageContextData *
_WorkerThreadScheduledJobExtractArg(MessageQueueMsgPayload *msgqueue_payload_ptr)
{
  return ((MessageContextData *)msgqueue_payload_ptr->payload);
}

static __attribute__((const)) MessageContextData *
_WorkerThreadGenericArgsExtractor(MessageQueueMsgPayload *msgqueue_payload_ptr)
{
  return (MessageContextData *)(msgqueue_payload_ptr->payload);
}

_Noreturn static void *_ThreadUFServerWorker(void *ptr);

BaseThreadContext *
GetBaseThreadContextForUfsrvWorker(void)
{
  return base_thread_context;
}

ufsrvworker_thread_callback
GetUfsrvWorkerThreadHandler(void)
{
  return  &_ThreadUFServerWorker;
}

/**
 * Generic job worker thread
 * @param ptr pre-allocated BaseThreadContext which becomes the actual storage for the thread local base_thread_context
 * @return
 */
_Noreturn static void *
_ThreadUFServerWorker(void *ptr)
{
	//thread local assignments (todo future roadmap: consolidate into a single base_thread_context, carrying job-worker specific ThreadContext)
  base_thread_context = ptr;
  if (IS_PRESENT(base_thread_context->worker_thread_context)) {
    ufsrv_thread_context = *(ThreadContext *)base_thread_context->worker_thread_context; //struct-copy
  }

  ufsrv_thread_context.user_data = base_thread_context->user_data;
  const SessionsDelegator *delegator = SessionsDelegatorPtr();
  const UfsrvDelegatorWorkersUserData *data = &delegator->ufsrv_delegator_userdata;
  if (base_thread_context->user_data && data->on_worker_start)
    data->on_worker_start(&ufsrv_thread_context);
  if (base_thread_context->worker_thread_context)
    *(ThreadContext *)base_thread_context->worker_thread_context = ufsrv_thread_context;

  ufsrv_thread_context.random_state = time(NULL) ^ getpid() ^ pthread_self(); //set to a seed value before rand_r is called for the first time


  WorkerPoolDescriptor *workers_pool = base_thread_context->pool_descriptor;

	{
		#define MAX_NAME_LEN 15
		char proc_name [MAX_NAME_LEN + 1];	/* Name must be <= 15 characters + a null */

		strncpy(proc_name, "ufServerWorker", MAX_NAME_LEN);
		proc_name [MAX_NAME_LEN] = 0;
		prctl(PR_SET_NAME, (unsigned long)&proc_name);
		UfsrvCpuAffinityPin(UFSRV_THREAD_WORKER, (int)base_thread_context->thread_idx);
		#undef MAX_NAME_LEN
	}

	if (!IS_EMPTY(workers_pool->thread_handlers.on_instantiated)) {
    workers_pool->thread_handlers.on_instantiated(base_thread_context);
	}

  syslog(LOG_INFO, "%s: --> Launching into main loop: pid:'%lu', ufsrv_th_ctx:'%p'", __func__, pthread_self(), base_thread_context);

	while (1) {
#if __UF_FULLDEBUG
		syslog(LOG_DEBUG, "ThreadUFServerWorker (pid:'%lu): --------- ", pthread_self());
		syslog(LOG_DEBUG, "ThreadUFServerWorker (pid:'%lu'): BEGIN COND_WAIT EVENT: Acquiring work queue mutex lock and fetching job... I may block", pthread_self());
#endif

		//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
		pthread_mutex_lock(&(workers_pool->workers_pool_config_descriptor.work_queue_mutex));
		//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

#if __UF_FULLDEBUG
		syslog(LOG_DEBUG, "ThreadUFServerWorker (pid:'%lu'): Successfully acquired mutex lock... (1)", pthread_self());
#endif

		while ((workers_pool->workers_pool_config_descriptor.ufsrv_work_queue.nEntries == 0) && (workers_pool->workers_pool_config_descriptor.up_status == POOL_STATE_UP)) {
#if __UF_FULLDEBUG
			syslog(LOG_DEBUG, "%s (pid:'%lu'): Mutex automatically released(-1): Blocking on condition: waiting for signal: queue_not_empty_cond", __func__, pthread_self());
#endif
			pthread_cond_wait(&(workers_pool->workers_pool_config_descriptor.queue_not_empty_cond), &(workers_pool->workers_pool_config_descriptor.work_queue_mutex));
		}

#if __UF_FULLDEBUG
		syslog(LOG_DEBUG, "ThreadUFServerWorker (pid:%lu): WORK SIGNAL: mutex lock automatically acquired (1) ", pthread_self());
#endif

		//lock now acquired automatically by pthreads.. we unlock at the end
		if (workers_pool->workers_pool_config_descriptor.up_status != POOL_STATE_UP)
		{
			syslog(LOG_INFO, "ThreadUFServerWorker (pid:%lu): UFServer Worker Manager is shutting down: releasing mutext lock (-1) exiting...", pthread_self());
			pthread_mutex_unlock(&(workers_pool->workers_pool_config_descriptor.work_queue_mutex));
			pthread_exit(NULL);
		}

		syslog(LOG_DEBUG, "%s (pid:%lu): RETRIEVING JOB: Queue size (cnt='%lu')...", __func__, pthread_self(), workers_pool->workers_pool_config_descriptor.ufsrv_work_queue.nEntries);

		//work_queue_mutex lock state: locked
		QueueEntry *qe_ptr = NULL;
		qe_ptr = deQueue(&(workers_pool->workers_pool_config_descriptor.ufsrv_work_queue));

		if (workers_pool->workers_pool_config_descriptor.ufsrv_work_queue.nEntries == 0)	pthread_cond_signal(&(workers_pool->workers_pool_config_descriptor.queue_empty_cond));//<<<<<<<

#if __UF_FULLDEBUG
		syslog(LOG_DEBUG, "%s (pid:%lu): END COND_WAIT EVENT: Releasing mutex lock (-1):  performing ufsrv work...", __func__, pthread_self());
#endif

		//reengage the pool main loop
		//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
		pthread_mutex_unlock(&(workers_pool->workers_pool_config_descriptor.work_queue_mutex));
		//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

		//do work
		{
#if __UF_FULLDEBUG
			syslog(LOG_DEBUG, "%s (pid:%lu): BEGIN:  performing ufsrv work...", __func__, pthread_self());
#endif
			if (IS_PRESENT(qe_ptr)  && IS_PRESENT(qe_ptr->whatever)) {
				MessageQueueMsgPayload 		*mqp_ptr						=	(MessageQueueMsgPayload *)qe_ptr->whatever;
        const WorkerJobDescriptor *job = &worker_job_specs[mqp_ptr->delegator_type];
        CallbackWorkExecutor execute = mqp_ptr->delegator_type == DELEGTYPE_MSGQUEUE
                                      ? delegator->msgqueue_executor : job->work_exec;
        if (execute && job->fetch_work_arg) {
          execute(job->fetch_work_arg(mqp_ptr));
        } else {
          syslog(LOG_ERR, "%s: No executor for job type %u", __func__, mqp_ptr->delegator_type);
          if (mqp_ptr->delegator_type == DELEGTYPE_MSGQUEUE) free(mqp_ptr->payload);
        }

				free(qe_ptr->whatever);
				free(qe_ptr);
			} else {
				syslog(LOG_ERR, "%s (pid:%lu): ERROR: QUEUE Entry was NULL...", __func__, pthread_self());
			}
		}
	}

}



