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

#ifndef SRC_INCLUDE_DELEGATOR_SESSION_WORKER_THREAD_H_
#define SRC_INCLUDE_DELEGATOR_SESSION_WORKER_THREAD_H_

#include <uflib/cdt/cdt_lamport_queue_type.h>
#include <uflib/adt/adt_hashtable.h>
#include <ufsrv_core/delegator_sessions/workers_config_descriptor_type.h>
#include <ufsrv_core/delegator_sessions/sessions_delegator_type.h>

// typedef void * ufsrvType; // forward declaration of opaque type for struct ufsrv

int AddSessionToMonitoredWorkEvents(InstanceHolderForSession *);
int RemoveSessionToMonitoredWorkEvents(InstanceHolderForSession *);
size_t UfsrvGetSessionWorkersSize(void);
unsigned UfsrvSessionWorkersPoolState(void);
int WorkQueueLock(SessionsDelegator *, int try_flag);
int WorkQueueUnLock(SessionsDelegator *);
int LaunchSessionsDelegatorThread(SessionsDelegator *);
SessionsDelegator *InitialiseDelegator(SessionsDelegatorInitSpecs *);

/**
* 	@brief This SPSC queue is shared between the accept loop (producer) and ThreadWorkerDelegator (consumer).
* 	Ensure instrumentation is already initialised in order to correctly report on queue capacity.
*/
PUBLIC_API void InitNewConnectionsQueue(QueueClientData *listener_connection_queue, size_t queue_sz);

PUBLIC_API bool SessionDelegatorPushIncomingConnection(QueueClientData *instance_sesn_ptr);

PUBLIC_API size_t SessionDelegatorIncomingConnectionsQueueSize();

PUBLIC_API HashTable *SessionDelegatorHashedSessionsTable();

WorkersConfigDescriptor *GetJobWorkersConfigurationDescriptor();
void RegisterJobWorkersConfigurationDescriptor(WorkersConfigDescriptor *workers_config_descriptor);
void UfsrvInitiateShutdown(void);

unsigned SessionDelegatorSessionIoHold(void);
void SessionDelegatorSetSessionIoHold(unsigned hold);
unsigned SessionDelegatorListenerHold(void);
void SessionDelegatorSetListenerHold(unsigned hold);
size_t SessionDelegatorIncomingConnectionsQueueCapacity(void);
unsigned long SessionDelegatorListenerRefused(void);
unsigned long SessionDelegatorListenerRefusedDuringHold(void);

#endif /* SRC_INCLUDE_DELEGATOR_SESSION_WORKER_THREAD_H_ */
