/**
 * @file ratelimit_backend.h
 * @brief Storage-backend interface for the ratelimit_v2 sliding-window core.
 *
 * The core algorithm never talks to Redis or an in-process map directly; it
 * only calls the operations declared here.  Consumers pick a backend at
 * construction time and otherwise use the same public API.
 *
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

#ifndef RATELIMIT_V2_BACKEND_H_
#define RATELIMIT_V2_BACKEND_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ufsrvcorelib_defs.h>

#include <ufsrv_core/cache_backend/persistence_type.h>

/*!
 * Prefix of every ratelimit namespace key, as stored in the backend:
 * "<prefix>:<namespace>". Shared by the memory and redis backends.
 */
#define CONFIGDEFAULT_RATELIMIT_NS_PREFIX  "_RL"
#include <ufsrv_core/session/ufsrv_session_type.h>
#include "hiredis/hiredis.h"

#include <ufsrv_core/ratelimit_v2/ratelimit_type.h>
#include <ufsrv_core/delegator_sessions/ratelimit_handle_type.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RateLimitBackend;

/*!
 * Snapshot of the sliding window *after* pruning stale events and *before*
 * recording the current request.  `timestamps` are microseconds since an
 * arbitrary epoch, sorted ascending.
 *
 * Ownership: `timestamps` points into storage owned by the backend for the
 * duration of the calling tick only, unless `owns_timestamps` is true, in
 * which case the caller must `free()` it.
 */
typedef struct RateLimitWindowSnapshot {
    size_t     count;            ///< number of timestamps
    long long *timestamps;       ///< ascending; may be NULL when count == 0
    bool       owns_timestamps;  ///< when true, caller must free(timestamps)
} RateLimitWindowSnapshot;

/*!
 * Parameters for one atomic sliding-window tick.
 */
typedef struct RateLimitTickParams {
    const char   *namespace_key;   ///< validated key; no whitespace/control/'%'
    long long     now_us;          ///< current time (microseconds)
    long long     clear_before_us; ///< prune threshold (drop events older than this)
    unsigned long expire_sec;      ///< soft TTL for the key (Redis EXPIRE; memory GC)
    long long     score_us;        ///< ZADD score / event time (usually now_us)
    const char   *member;          ///< unique member string (Redis); ignored by memory
} RateLimitTickParams;

/*! Callback: atomically prune, snapshot, record and refresh one window tick. */
typedef enum RateLimitResultCode (*RateLimitTickCallback)(
    struct RateLimitBackend *self,
    const RateLimitTickParams *params,
    RateLimitWindowSnapshot *out);

/*! Callback: best-effort delete of one namespace key (tests / admin). */
typedef enum RateLimitResultCode (*RateLimitClearCallback)(
    struct RateLimitBackend *self,
    const char *namespace_key);

/*! Callback: release backend-owned resources. */
typedef void (*RateLimitDestroyCallback)(struct RateLimitBackend *self);

/*! Backend operation table (vtable). */
typedef struct RateLimitBackendOps {
    const char *name;                ///< human-readable backend name

    RateLimitTickCallback    Tick;    ///< atomic window tick
    RateLimitClearCallback   Clear;   ///< delete one namespace key
    RateLimitDestroyCallback Destroy; ///< release the backend
} RateLimitBackendOps;

struct RateLimitBackend {
    const RateLimitBackendOps *ops;  ///< operation table (never NULL)
    void                      *impl; ///< implementation private state
};

/*!
 * Atomically tick the sliding window for `params->namespace_key`.
 *
 * @return RL_OK on success (with *out filled), else an RL_ERR_* code and
 *         *out left zeroed.
 */
static inline enum RateLimitResultCode
RateLimitBackendTick(struct RateLimitBackend *be,
                const RateLimitTickParams *params,
                RateLimitWindowSnapshot *out)
{
    if (!be || !be->ops || !be->ops->Tick || !params || !out)
        return RL_ERR_MISSING_PARAM;
    return be->ops->Tick(be, params, out);
}

/*! Best-effort clear of one namespace key. */
static inline enum RateLimitResultCode
RateLimitBackendClear(struct RateLimitBackend *be, const char *namespace_key)
{
    if (!be || !be->ops || !be->ops->Clear || !namespace_key)
        return RL_ERR_MISSING_PARAM;
    return be->ops->Clear(be, namespace_key);
}

/*! Release a backend previously returned by a constructor. */
static inline void
RateLimitBackendDestroy(struct RateLimitBackend *be)
{
    if (be && be->ops && be->ops->Destroy)
        be->ops->Destroy(be);
}

/* ---- constructors (implementations) ---- */

/*!
 * In-process backend.  Thread-safe, no external dependencies.  Suitable for
 * single-process limits and unit tests.
 */
PUBLIC_API struct RateLimitBackend *RateLimitBackendMemoryCreate(void);

/*! Callback: issue a pipelined Redis command (append, do not read reply).
 *  Signature mirrors CacheBackend::send_command_multi (SessionType = void). */
typedef void *(*RateLimitSendCommandMultiCallback)(void *sesn, CacheBackend *pers, const char *fmt, ...);

/*! Callback: read the next Redis reply (REDIS_OK / REDIS_ERR).
 *  Signature mirrors RedisGetReply. */
typedef int (*RateLimitGetReplyCallback)(UfsrvSession *sesn, CacheBackend *pers, redisReply **reply);

/*! Callback: free a reply object returned by get_reply. */
typedef void (*RateLimitFreeReplyCallback)(void *reply);

/*!
 * Hooks that let the Redis backend plug into the existing CacheBackend /
 * hiredis stack.  The callback signatures match the real functions exactly so
 * the function pointers can be assigned without a type-changing cast (a cast
 * through an incompatible function-pointer type is UB).
 *
 * `pers` is the CacheBackend* passed through to the callbacks; `ctx` is an
 * optional session/context (may be NULL).
 */
typedef struct RateLimitRedisHooks {
    CacheBackend *pers;                          ///< CacheBackend* passed through to the hooks
    void         *ctx;                           ///< UfsrvSession * / SessionType* (may be NULL)
    RateLimitSendCommandMultiCallback send_command_multi; ///< append a command
    RateLimitGetReplyCallback         get_reply;          ///< read a reply
    RateLimitFreeReplyCallback        free_reply;         ///< free a reply
} RateLimitRedisHooks;

/*!
 * Redis backend (MULTI/EXEC sliding-window ZSET).  Returns NULL if `hooks` is
 * NULL or any required hook is missing.
 */
PUBLIC_API struct RateLimitBackend *RateLimitBackendRedisCreate(const RateLimitRedisHooks *hooks);

PUBLIC_API void RateLimitHandleBindRedis(RateLimitHandle *handle, CacheBackend *pers,
                                          RateLimitApplicationPolicy *app_policy);

#ifdef __cplusplus
}
#endif

#endif /* RATELIMIT_V2_BACKEND_H_ */
