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

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include <ufsrv_core/protocol/protocol_type.h>
#include <ufsrv_core/protocol/protocol.h>

#include <ufsrv_core/ufsrvresult_type.h>
#include <ufsrv_core/session/ufsrv_session_type.h>

static Protocol *protocols_registry_ptr;
static size_t protocols_registry_count;

static const Protocol *_protocol_accessor_panic(void) {
	fprintf(stderr, "FATAL: Protocol registry accessed before InitProtocolsForUfsrv()\n");
	abort();
}
static const Protocol *_protocol_accessor_direct(void) { return protocols_registry_ptr; }
static const Protocol *(*_protocol_accessor)(void) = _protocol_accessor_panic;

/**
 * Marshal the initialisation routines for supported protocols.
 * @param protocols_registry Pre-allocated and in initialised array of supported protocols.
 * @param protocols_registry_sz size of the registry
 */
void
InitProtocolsForUfsrv(Protocol protocols_registry[], const size_t protocols_registry_sz)
{
	syslog(LOG_INFO, "%s: INITIALISING SUPPORTED PROTOCOLS: Supporting %lu protocols...", __func__, protocols_registry_sz);

	if (protocols_registry_sz > 0) {
		protocols_registry_ptr = protocols_registry;
		protocols_registry_count = protocols_registry_sz;
		_protocol_accessor = _protocol_accessor_direct;

		for ( size_t i = 0; i <= protocols_registry_sz - 1; i++) {
			const Protocol *protocol_ptr = &protocols_registry[i];

			syslog(LOG_INFO, "%s: INITIALISING SUPPORTED PROTOCOL: '%s'...", __func__, PROTO_PROTOCOL_NAME(protocol_ptr));

			if (!protocol_ptr->protocol_controls.is_enabled) {
				syslog(LOG_NOTICE, "%s: PROTOCOL: '%s' DISABLED BY CONFIGURATION - INIT SKIPPED", __func__, PROTO_PROTOCOL_NAME(protocol_ptr));
				continue;
			}

			if (IS_PRESENT(protocol_ptr->protocol_callbacks.protocol_init_callback)) {
				(*protocol_ptr->protocol_callbacks.protocol_init_callback)(protocol_ptr);
			} else {
				syslog(LOG_INFO, "%s: PROTOCOL: '%s' DID NOT REGISTER AN INIT CALL...", __func__,PROTO_PROTOCOL_NAME(protocol_ptr));
			}
		}
	}
}

bool
ProtocolSetSessionPool(const Protocol *protocol, const RecyclerPoolHandle *pool)
{
  if (!protocol || !pool) return false;
	Protocol *match = NULL;
  for (size_t i = 0; i < protocols_registry_count; ++i) {
    Protocol *entry = &protocols_registry_ptr[i];
		if (entry == protocol) match = entry;
		else if (entry->session_pool == pool) return false;
  }
	if (!match || (match->session_pool && match->session_pool != pool)) return false;
	match->session_pool = pool;
	return true;
}

/**
  *
  * @param protocols_registry Preallocated and in initialised array of supported protocols.
  * @param protocol_id Protocol id are simply array indexes into the registry
  * @return static copy of Protocol
  */
const Protocol *const
ProtocolGet(unsigned protocol_id)
 {
	return _protocol_accessor() + protocol_id;
 }

 bool
IsProtocolMsgQueueSubscriber(protocol_id_t protocol_id)
 {
		return ProtocolGet(protocol_id)->protocol_controls.msgqueue_subscriber;
 }

 bool
IsProtocolMsgQueuePublisher(protocol_id_t protocol_id)
 {
		return ProtocolGet(protocol_id)->protocol_controls.msgqueue_publisher;
 }

bool
IsProtocolCloudMsgOnIOError(protocol_id_t protocol_id)
{
	return ProtocolGet(protocol_id)->protocol_controls.cloudmsg_on_ioderror;
}

bool
IsProtocolReadBlockedSession(protocol_id_t protocol_id)
{
	return ProtocolGet(protocol_id)->protocol_controls.read_blocked_session;
}

bool
IsProtocolReadInserviceSession(protocol_id_t protocol_id)
{
	return ProtocolGet(protocol_id)->protocol_controls.read_inservice_session;
}

bool
IsProtocolRetainSessionOnError(protocol_id_t protocol_id)
{
	return ProtocolGet(protocol_id)->protocol_controls.retain_session_on_error;
}

bool
IsProtocolAbortOnIOError(protocol_id_t protocol_id)
{
	return ProtocolGet(protocol_id)->protocol_controls.abort_on_ioerror;
}

bool
IsProtocolClientOfRecycler(protocol_id_t protocol_id)
{
	return ProtocolGet(protocol_id)->protocol_controls.client_of_recycler;
}

bool
IsProtocolPubSessionTransitions(protocol_id_t protocol_id)
{
	return ProtocolGet(protocol_id)->protocol_controls.pub_session_transitions;
}

UFSRVResult *
ProtocolInvokePostHandshakeCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage * sock_msg_ptr, unsigned call_flags)
{
	UFSRVResult *(*post_handshake_callback)(InstanceHolder *, SocketMessage *, unsigned) = ProtocolGet(protocol_id)->protocol_callbacks.post_handshake_callback;

	if (IS_PRESENT(post_handshake_callback)) {
		return post_handshake_callback(instance_sesn_ptr,sock_msg_ptr,call_flags);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)

}

UFSRVResult *
ProtocolInvokeSessionResetCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, unsigned call_flags)
{
	UFSRVResult *(*callback)(InstanceHolder *, unsigned) = ProtocolGet(protocol_id)->protocol_callbacks.session_reset_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, call_flags);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeHandshakeCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags, int **out_ptr)
{
	UFSRVResult *(*callback)(InstanceHolder *, SocketMessage *, unsigned, int **) = ProtocolGet(protocol_id)->protocol_callbacks.handshake_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, sock_msg_ptr, call_flags, out_ptr);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeMsgCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags, size_t extra)
{
	UFSRVResult *(*callback)(InstanceHolder *, SocketMessage *, unsigned, size_t) = ProtocolGet(protocol_id)->protocol_callbacks.msg_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, sock_msg_ptr, call_flags, extra);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeMsgOutCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags, size_t extra)
{
	UFSRVResult *(*callback)(InstanceHolder *, SocketMessage *, unsigned, size_t) = ProtocolGet(protocol_id)->protocol_callbacks.msg_out_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, sock_msg_ptr, call_flags, extra);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeMsgDecodeCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags)
{
	UFSRVResult *(*callback)(InstanceHolder *, SocketMessage *, unsigned) = ProtocolGet(protocol_id)->protocol_callbacks.msg_decode_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, sock_msg_ptr, call_flags);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeMsgEncodeCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned call_flags)
{
	UFSRVResult *(*callback)(InstanceHolder *, SocketMessage *, unsigned) = ProtocolGet(protocol_id)->protocol_callbacks.msg_encode_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, sock_msg_ptr, call_flags);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeServiceTimeoutCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, time_t now, unsigned long call_flags)
{
	UFSRVResult *(*callback)(InstanceHolder *, time_t, unsigned long) = ProtocolGet(protocol_id)->protocol_callbacks.service_timeout_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, now, call_flags);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeErrorCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, unsigned call_flags)
{
	UFSRVResult *(*callback)(InstanceHolder *, unsigned) = ProtocolGet(protocol_id)->protocol_callbacks.error_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, call_flags);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeRecyclerErrorCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr, unsigned call_flags)
{
	UFSRVResult *(*callback)(InstanceHolder *, unsigned) = ProtocolGet(protocol_id)->protocol_callbacks.recycler_error_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr, call_flags);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeCloseCallback(protocol_id_t protocol_id, InstanceHolder *instance_sesn_ptr)
{
	UFSRVResult *(*callback)(InstanceHolder *) = ProtocolGet(protocol_id)->protocol_callbacks.close_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_sesn_ptr);
	}

	_RETURN_RESULT(((UfsrvSession *)GetInstance(instance_sesn_ptr)), EMPTY_USERDATA, RESULT_TYPE_WARN_NOOP, RESCODE_NOT_IMPLEMENTED)
}

UFSRVResult *
ProtocolInvokeInitCallback(protocol_id_t protocol_id, const Protocol *protocol_ptr)
{
	UFSRVResult *(*callback)(const Protocol *) = ProtocolGet(protocol_id)->protocol_callbacks.protocol_init_callback;

	if (IS_PRESENT(callback)) {
		return callback(protocol_ptr);
	}

	return NULL;
}

UFSRVResult *
ProtocolInvokeConfigCallback(protocol_id_t protocol_id, ClientContextData *config_file_handler_ptr)
{
	UFSRVResult *(*callback)(ClientContextData *) = ProtocolGet(protocol_id)->protocol_callbacks.config_callback;

	if (IS_PRESENT(callback)) {
		return callback(config_file_handler_ptr);
	}

	return NULL;
}

UFSRVResult *
ProtocolInvokeWorkersDelegatorInitCallback(protocol_id_t protocol_id)
{
	UFSRVResult *(*callback)(void) = ProtocolGet(protocol_id)->protocol_callbacks.init_workers_delegator_callback;

	if (IS_PRESENT(callback)) {
		return callback();
	}

	return NULL;
}

UFSRVResult *
ProtocolInvokeSessionInitCallback(protocol_id_t protocol_id, ClientContextData *context_data_ptr, unsigned call_flags)
{
	UFSRVResult *(*callback)(ClientContextData *, unsigned) = ProtocolGet(protocol_id)->protocol_callbacks.session_init_callback;

	if (IS_PRESENT(callback)) {
		return callback(context_data_ptr, call_flags);
	}

	return NULL;
}

UFSRVResult *
ProtocolInvokeMsgqueueTopicsCallback(protocol_id_t protocol_id, UFSRVResult *result_ptr)
{
	UFSRVResult *(*callback)(UFSRVResult *) = ProtocolGet(protocol_id)->protocol_callbacks.msgqueue_topics_callback;

	if (IS_PRESENT(callback)) {
		return callback(result_ptr);
	}

	return NULL;
}

UFSRVResult *
ProtocolInvokeGenerateSessionIdCallback(protocol_id_t protocol_id, UFSRVResult *result_ptr, ClientContextData *context_data_ptr)
{
	UFSRVResult *(*callback)(UFSRVResult *, ClientContextData *) = ProtocolGet(protocol_id)->protocol_callbacks.generate_session_id_callback;

	if (IS_PRESENT(callback)) {
		return callback(result_ptr, context_data_ptr);
	}

	return NULL;
}

UFSRVResult *
ProtocolInvokeOnRateLimitedCallback(protocol_id_t protocol_id, InstanceHolder *instance_holder, unsigned stage, ClientContextData *context_ptr)
{
	UFSRVResult *(*callback)(InstanceHolder *, unsigned, ClientContextData *) = ProtocolGet(protocol_id)->protocol_callbacks.on_ratelimited_callback;

	if (IS_PRESENT(callback)) {
		return callback(instance_holder, stage, context_ptr);
	}

	return NULL;
}

bool
ProtocolRegistryPublishesMessages(void)
{
  for (size_t i = 0; i < protocols_registry_count; ++i) {
    if (protocols_registry_ptr[i].protocol_controls.msgqueue_publisher) return true;
  }
  return false;
}
