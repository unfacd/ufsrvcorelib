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


#ifndef UFSRV_WORKER_UFSRV_THREAD_H
#define UFSRV_WORKER_UFSRV_THREAD_H

#include "ufsrv_core/delegator_sessions/base_thread_context_data_type.h"
#include <ufsrv_core/jobworkers/message_queue_msg_payload_type.h>

typedef void * (*ufsrvworker_thread_callback)(void *);
ufsrvworker_thread_callback GetUfsrvWorkerThreadHandler(void);
BaseThreadContext *GetBaseThreadContextForUfsrvWorker(void);
MessageContextData *WorkerThreadMessageQueueParserExtractArg(MessageQueueMsgPayload *msgqueue_payload_ptr);

#endif //UFSRV_WORKER_UFSRV_THREAD_H
