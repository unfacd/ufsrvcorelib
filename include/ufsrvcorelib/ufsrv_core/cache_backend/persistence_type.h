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

#ifndef SRC_INCLUDE_PERSISTANCE_TYPE_H_
#define SRC_INCLUDE_PERSISTANCE_TYPE_H_

#include <stdbool.h>
#include <ufsrv_core/cache_backend/backendconfig_type.h>

//	convenient semantic type naming
typedef struct PersistenceBackend UserMessageCacheBackend;
typedef struct PersistenceBackend FenceCacheBackend;
typedef struct PersistenceBackend	CacheBackend;
typedef struct PersistenceBackend RedisBackend;

typedef void BackendHandle;	///> persistence_connection_handle;

typedef void SessionType; ///> generic type for Session object

//A specialised type exists in struct RedisBackend. For the time being they must match in layout.
typedef struct PersistenceBackend PersistenceBackend;

struct PersistenceBackend	{
	BackendHandle *backend_handle_ptr;  ///> opaque type representing persistence agent connection handle to the backend server eg. 'redisContext *'
	void *(*send_command)(SessionType *sesn_ptr_this, CacheBackend *pers_ptr, const char *format, ...);
	void *(*send_command_multi)(SessionType *sesn_ptr, CacheBackend *pers_ptr, const char *format, ...);	///>	pipelined synchronous redis command
	void *(*send_command_sessionless)(void *ptr, const char *format, ...);
	PersistenceBackend *(*init_connection)(PersistenceBackend *in_per_ptr, const char* server_address, unsigned server_port, const char* descriptive_label, bool is_re_initialised); ///> server connection re-initialiser
	BackendConfig backend_config; ///> carries full definition of server host address identifying information
};

#define PERSISTENCE_BACKEND_REINITIALISED	true

#define AS_PERSISTENCE_BACKEND(x) ((PersistenceBackend *)(x))

#endif /* SRC_INCLUDE_PERSISTANCE_TYPE_H_ */
