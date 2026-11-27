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

#ifndef SRC_INCLUDE_REDIS_BACKEND_TYPE_H_
#define SRC_INCLUDE_REDIS_BACKEND_TYPE_H_

#include "hiredis/hiredis.h"
#include <ufsrv_core/cache_backend/persistence_type.h>

/* Redis reply budget: the library never waited for a reply beyond these. */
#ifndef _CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_SEC
#define _CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_SEC 0
#endif
#ifndef _CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_USEC
#define _CONFIG_CACHE_BACKEND_REPLY_TIMEOUT_USEC 0
#endif

#define AS_REDIS_CONTEXT(x)	((redisContext *) (x))

//A generalised type exists in struct PersistenceBackend. For the time being they must match in layout.
/*typedef struct RedisBackend RedisBackend;
struct RedisBackend	{
		redisContext *persistance_agent;
		void *(*send_command)();
		void *(*send_command_multi)();	//pipelined synchronous redis command
		void *(*send_command_sessionless)();
		RedisBackend *(*init_connection)(PersistenceBackend *in_per_ptr, const char* server_address, unsigned server_port, const char* descriptive_label, bool is_re_initialised);	///> server connection re-initialiser
		BackendConfig backend_config; ///> carries full definition of server host address identifying information
	};*/

#endif /* SRC_INCLUDE_REDIS_BACKEND_TYPE_H_ */
