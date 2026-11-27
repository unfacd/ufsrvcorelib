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
#include <syslog.h>

#include <uflib/standard_defs.h>
#endif

#include "ufsrv_core/cache_backend/persistence.h"
#include <ufsrv_core/cache_backend/backendconfig_type.h>
#include <uflib/net/utils_net.h>
#include "ufsrv_core/cache_backend/redis.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Persist the backend configuration for the reconnect path.
 *
 * The reconnect loop reads per_ptr->backend_config (decoupled from the ufsrv
 * config domain), so it must be populated with valid host/label.  server_address
 * and backend_label are caller-owned and may alias the previous persisted copies
 * (the reconnect passes them back from backend_config), so strdup the new values
 * before freeing the old ones.  The original server_address is stored -- not the
 * resolved IP -- so a reconnect re-resolves the hostname.  On a fresh (zeroed)
 * backend the free is a no-op.
 */
static void
sPersistBackendConfig(PersistenceBackend *per_ptr, const char *server_address, const BackendConfig *config_backend)
{
	char *host_copy  = strdup(server_address);
	char *label_copy = strdup(config_backend->backend_label ? config_backend->backend_label : "");
	free((char *)per_ptr->backend_config.con_tcp.host);
	free((char *)per_ptr->backend_config.backend_label);
	per_ptr->backend_config = *config_backend;
	per_ptr->backend_config.con_tcp.host  = host_copy;
	per_ptr->backend_config.backend_label = label_copy;
}

//todo these functions ceased to be distinct: refactor into one calling point

//When passing in_per_ptr we assume we are reinitialisng an existing backend
//free context and reallocate
PersistenceBackend *
InitialisePersistenceBackend(PersistenceBackend * _Nullable in_per_ptr, const char* server_address, unsigned server_port, const char* descriptive_label, bool is_re_initialised)
{
	BackendConfig config_backend = {0};    //	this is currently transient and values are not retained, because this function is always called with seeding server information
	struct timeval tv;
	char resolved_ip[SBUF] = {0};

	GenericDnsResolve(server_address, resolved_ip, sizeof(resolved_ip));
	config_backend.con_tcp.host             = resolved_ip;
	config_backend.con_tcp.port             = server_port;
	config_backend.type											= SOCK_TCP;//TODO: read actual type
	tv.tv_sec               								=_CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_SEC;
	tv.tv_usec              								=_CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_USEC;
	config_backend.con_tcp.timeout					= tv;
  config_backend.backend_label						= descriptive_label;

	PersistenceBackend *per_ptr = BuildConnectionHandleForRedisBackend(in_per_ptr, &config_backend);
	if (per_ptr)	sPersistBackendConfig(per_ptr, server_address, &config_backend);

	return per_ptr;

}

UserMessageCacheBackend *
InitialiseCacheBackendUserMessage(PersistenceBackend * _Nullable in_per_ptr, const char* server_address, unsigned server_port, const char* descriptive_label, bool is_re_initialised)
{
		BackendConfig config_backend = {0};    //	this is currently transient and values are not retained, because this function is always called with seeding server information
	struct timeval tv;
	char resolved_ip[SBUF] = {0};

	GenericDnsResolve(server_address, resolved_ip, sizeof(resolved_ip));
	config_backend.con_tcp.host = resolved_ip;
	config_backend.con_tcp.port = server_port;
	config_backend.type																				 = SOCK_TCP;//TODO: read actual type
	tv.tv_sec   = _CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_SEC;
	tv.tv_usec  = _CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_USEC;
	config_backend.con_tcp.timeout  = tv;
  config_backend.backend_label    = descriptive_label;

	UserMessageCacheBackend *per_ptr = (UserMessageCacheBackend *) BuildConnectionHandleForRedisBackend((RedisBackend *) in_per_ptr, &config_backend);
	if (per_ptr)	sPersistBackendConfig(per_ptr, server_address, &config_backend);

	return per_ptr;

}

FenceCacheBackend *
InitialiseCacheBackendFence(PersistenceBackend * _Nullable in_per_ptr, const char* server_address, unsigned server_port, const char* descriptive_name, bool is_re_initialised)
{
	BackendConfig config_backend = {0};    //	this is currently transient and values are not retained, because this function is always called with seeding server information
	struct timeval tv;
	char resolved_ip[SBUF] = {0};

	GenericDnsResolve(server_address/*masterptr->cache_backend_address_fence*/, resolved_ip, sizeof(resolved_ip));
	config_backend.con_tcp.host = resolved_ip;
	config_backend.con_tcp.port = server_port;//masterptr->cache_backend_port_fence;
	config_backend.type = SOCK_TCP;//TODO: read actual type
	tv.tv_sec = _CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_SEC;
	tv.tv_usec = _CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_USEC;
	config_backend.con_tcp.timeout = tv;
  config_backend.backend_label = descriptive_name;//masterptr->server_descriptive_name;

	FenceCacheBackend *per_ptr = BuildConnectionHandleForRedisBackend((RedisBackend *) in_per_ptr, &config_backend);
	if (per_ptr)	sPersistBackendConfig(per_ptr, server_address, &config_backend);

	return per_ptr;

}
