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

#ifndef UFSRVCORELIB_PROTOCOL_H
#define UFSRVCORELIB_PROTOCOL_H

#include <time.h>

#include <ufsrv_core/protocol/protocol_type.h>
#include <ufsrv_core/ufsrvresult_type.h>
#include <uflib/recycler/instance_type.h>
#include <uflib/net/socket_type.h>


/*
TBA
 - each protocol defines a protocol-specific include file that includes
	function declarations. eg protocol_websockets.h
 - this header file must be included in protocol_data.h to support
	function pointer definitions in the array
 - each protocol provide necessary calls back as per the definitions below
 - each protocol gets a slot in the statically defined array-of-structs 
	'static const ProtocolCallbacks ProtocolCallbackWebsockets []' defined in
	protocol_data.
 - You should not access 'ProtocolCallbackWebsockets' directly; rather, 
	use the pointer declared in protocol.c:
 - const  ProtocolCallbacks *const protocol_callbacks=ProtocolCallbackWebsockets
 - declare it extern and use that.
 - each protocol has a unique index defined in a set of enums in ufsrv.h
	websockets is 0
*/

PUBLIC_API void InitProtocolsForUfsrv(Protocol protocols_registry[], size_t protocols_registry_sz);
PUBLIC_API const Protocol *const ProtocolGet(unsigned protocol_id);

/**
 * @brief Attach a session pool to a registered protocol during startup.
 * @code{.c}
 * bool attached = ProtocolSetSessionPool(protocol, pool);
 * @endcode
 */
PUBLIC_API bool ProtocolSetSessionPool(const Protocol *protocol, const RecyclerPoolHandle *pool);

PUBLIC_API bool IsProtocolMsgQueueSubscriber(unsigned protocol_id);
PUBLIC_API bool IsProtocolMsgQueuePublisher(protocol_id_t protocol_id);

/**
 * @brief Report whether any registered protocol needs the shared publisher.
 * @code{.c}
 * bool enabled = ProtocolRegistryPublishesMessages();
 * @endcode
 */
PUBLIC_API bool ProtocolRegistryPublishesMessages(void);
PUBLIC_API bool IsProtocolCloudMsgOnIOError(protocol_id_t protocol_id);
PUBLIC_API bool IsProtocolReadBlockedSession(protocol_id_t protocol_id);
PUBLIC_API bool IsProtocolReadInserviceSession(protocol_id_t protocol_id);
PUBLIC_API bool IsProtocolRetainSessionOnError(protocol_id_t protocol_id);
PUBLIC_API bool IsProtocolAbortOnIOError(protocol_id_t protocol_id);
PUBLIC_API bool IsProtocolClientOfRecycler(protocol_id_t protocol_id);
PUBLIC_API bool IsProtocolPubSessionTransitions(protocol_id_t protocol_id);
PUBLIC_API UFSRVResult * ProtocolInvokePostHandshakeCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags);
PUBLIC_API UFSRVResult * ProtocolInvokeSessionResetCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, unsigned call_flags);
PUBLIC_API UFSRVResult * ProtocolInvokeHandshakeCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags, int **out_ptr);
PUBLIC_API UFSRVResult * ProtocolInvokeMsgCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags, size_t extra);
PUBLIC_API UFSRVResult * ProtocolInvokeMsgOutCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags, size_t extra);
PUBLIC_API UFSRVResult * ProtocolInvokeMsgDecodeCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags);
PUBLIC_API UFSRVResult * ProtocolInvokeMsgEncodeCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags);
PUBLIC_API UFSRVResult * ProtocolInvokeServiceTimeoutCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, time_t now, unsigned long call_flags);
PUBLIC_API UFSRVResult * ProtocolInvokeErrorCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, unsigned call_flags);
PUBLIC_API UFSRVResult * ProtocolInvokeRecyclerErrorCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, unsigned call_flags);
PUBLIC_API UFSRVResult * ProtocolInvokeCloseCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr);
PUBLIC_API UFSRVResult * ProtocolInvokeInitCallback(protocol_id_t protocol_id, const Protocol *protocol_ptr);
PUBLIC_API UFSRVResult * ProtocolInvokeConfigCallback(protocol_id_t protocol_id, ClientContextData *config_file_handler_ptr);
PUBLIC_API UFSRVResult * ProtocolInvokeWorkersDelegatorInitCallback(protocol_id_t protocol_id);
PUBLIC_API UFSRVResult * ProtocolInvokeSessionInitCallback(protocol_id_t protocol_id, ClientContextData *context_data_ptr, unsigned call_flags);
PUBLIC_API UFSRVResult * ProtocolInvokeMsgqueueTopicsCallback(protocol_id_t protocol_id, UFSRVResult *result_ptr);
PUBLIC_API UFSRVResult * ProtocolInvokeGenerateSessionIdCallback(protocol_id_t protocol_id, UFSRVResult *result_ptr, ClientContextData *context_data_ptr);
PUBLIC_API UFSRVResult * ProtocolInvokeOnRateLimitedCallback(protocol_id_t protocol_id, InstanceHolder *instance_holder, unsigned stage, ClientContextData *context_ptr);

#endif /* INCLUDE_PROTOCOLS_H_ */
