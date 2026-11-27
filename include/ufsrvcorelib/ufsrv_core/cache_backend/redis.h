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

#ifndef SRC_INCLUDE_REDIS_H_
#define SRC_INCLUDE_REDIS_H_

#include <ufsrv_core/cache_backend/backendconfig_type.h>
#include <ufsrv_core/session/ufsrv_session_type.h>
#include <uflib/main_types.h>
#include "redis_backend_type.h"

#if 0
	struct RedisBackend	{
		redisContext *persistance_agent;//redisConetx *
		void *(*send_command)();
		void *(*send_command_multi)();//pipelined synchronous redis command
	};
	typedef struct RedisBackend RedisBackend;

//#define PERSIST_DATA(x, ...) (*x->persistance_backend.send_command)(...)
#endif

#if 0
	enum connection_type {
	    CONN_TCP,
	    CONN_UNIX,
	    CONN_FD
	};
#endif

/**
 * 1)currently scripts are saved in teh same working directory as redis instalation
 * 2)at startup time, the script must be loaded into redis: /opt/redis/redis-cli -p 19705 SCRIPT LOAD "$(cat redis_id_generation.lua)"
 *
 * 3)at ufsrv startup (InitUfsrv()) time we check for the presence of the script with command (SCRIPT EXISTS %s", REDIS_SCRIPT_SHA1_UNIQUE_ID);
 * This generated at redis start up time using the command "/opt/redis/redis-cli -p 19705 SCRIPT LOAD "$(cat redis_id_generation.lua)""
 * It needs to be regenerated overtime the script is modified.
 */
#define REDIS_SCRIPT_SHA1_UNIQUE_ID "5ddb0d7aa876a99a1f03ae483ada43b30d3731be"
#define REDIS_SCRIPT_SHA1_FENCE_CLAIM "d35020e8ea7bfc30fc7399db92270c9822f32782" //etc/redis/redis_fence_claim.lua
#define REDIS_SCRIPT_SHA1_DEL_LOCK	"3d84bffbe72d4ec06e345cb3db2eae1345524faf"

unsigned DisconnectRedisBackend(UfsrvSession *, int);
RedisBackend *BuildConnectionHandleForRedisBackend(RedisBackend *in_per_ptr, struct BackendConfig *cfg);
void PrintPersistenceError(UfsrvSession *sesn_ptr, char *user_str);
/* The send/reply functions now return an error (NULL / REDIS_ERR) on
 * connection-loss exhaustion instead of exit(2) — warn if a caller ignores it. */
__attribute__((warn_unused_result))
void *RedisSendCommandSessionless(void *ptr, const char *format, ...);
__attribute__((warn_unused_result))
void *RedisSendSessionCommand(SessionType *sesn_ptr_this, CacheBackend *pers_ptr_in, const char *format, ...);
__attribute__((warn_unused_result))
void *RedisSendCommand(CacheBackend *pers_ptr_in, const char *format, ...);
void *RedisSendCommandMulti(SessionType *, CacheBackend *, const char *format, ...);
__attribute__((warn_unused_result))
void *RedisSendCommandWithCollection(UfsrvSession *sesn_ptr, CacheBackend *pers_ptr, CollectionDescriptorPair *collection_argv_argvlen);
__attribute__((warn_unused_result))
int RedisGetReply(UfsrvSession *sesn_ptr, CacheBackend *pers_ptr, redisReply	**reply);
/* Verifies that a script is loaded on the given backend. Returns 0 when the server reports it
 * present, -1 when it is absent or the check itself fails; never exits, so the caller decides. */
int RedisVerifyScriptLoaded(RedisBackend *backend_ptr, const char *script_sha1, const char *label);
unsigned long GenerateCacheBackendId(PersistenceBackend *backend_ptr);

#endif /* SRC_INCLUDE_REDIS_H_ */
