/**
 * @file ratelimit_backend_redis.c
 * @brief Redis sliding-window backend (MULTI/EXEC ZSET path).
 *
 * Issues, atomically, per namespace:
 *   MULTI → ZREMRANGEBYSCORE → ZRANGE → ZADD → EXPIRE → EXEC
 * and interprets the returned window.  Depends only on the RateLimitRedisHooks
 * function pointers, so it plugs into the existing CacheBackend / hiredis
 * stack.
 *
 * Reply layout expected from get_reply (same as the original module):
 *   [0] MULTI  -> status OK
 *   [1..4]     -> status QUEUED
 *   [5] EXEC   -> array of 4 elements:
 *         [0] ZREMRANGEBYSCORE integer
 *         [1] ZRANGE array of member strings ("<score>-<nonce>" or pure score)
 *         [2] ZADD integer
 *         [3] EXPIRE integer
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

#define _POSIX_C_SOURCE 200809L

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <ufsrv_core/ratelimit_v2/ratelimit_backend.h>
#include "hiredis/hiredis.h"
#include <ufsrv_core/cache_backend/redis.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

typedef struct RedisState
{
  RateLimitRedisHooks hooks;
} RedisState;

static enum RateLimitResultCode
sRedisTick(struct RateLimitBackend *self, const RateLimitTickParams *params, RateLimitWindowSnapshot *out)
{
  RedisState *         st = (RedisState*)self->impl;
  RateLimitRedisHooks *h  = &st->hooks;
  memset(out, 0, sizeof(*out));

  if (!h->send_command_multi || !h->get_reply || !h->free_reply) return RL_ERR_MISSING_PARAM;

  /* The namespace is validated (no '%', space, control), so it is safe to pass
   * as a %s argument.  The key is stored under CONFIGDEFAULT_RATELIMIT_NS_PREFIX
   * (e.g. "_RL:<namespace>").  The literal spaces in the format strings below
   * still delimit the argv tokens (see redisvFormatCommand). */
  h->send_command_multi(h->ctx, h->pers, "MULTI");
  h->send_command_multi(h->ctx, h->pers, "ZREMRANGEBYSCORE %s:%s 0 %lld", CONFIGDEFAULT_RATELIMIT_NS_PREFIX, params->namespace_key, params->clear_before_us);
  h->send_command_multi(h->ctx, h->pers, "ZRANGE %s:%s 0 -1", CONFIGDEFAULT_RATELIMIT_NS_PREFIX, params->namespace_key);
  h->send_command_multi(h->ctx, h->pers, "ZADD %s:%s %lld %s", CONFIGDEFAULT_RATELIMIT_NS_PREFIX, params->namespace_key, params->score_us, params->member);
  h->send_command_multi(h->ctx, h->pers, "EXPIRE %s:%s %lu", CONFIGDEFAULT_RATELIMIT_NS_PREFIX, params->namespace_key, params->expire_sec);
  h->send_command_multi(h->ctx, h->pers, "EXEC");

  const size_t ncmds = 6;
  redisReply * replies[6];
  memset(replies, 0, sizeof(replies));

  size_t ok = 0;
  for (size_t i = 0; i < ncmds; i++) {
    redisReply *raw = NULL;
    if (h->get_reply(h->ctx, h->pers, &raw) != REDIS_OK) {
      if (raw) h->free_reply(raw);
      continue;
    }
    replies[i] = raw;
    ok++;
  }

  if (ok != ncmds) {
    for (size_t i = 0; i < ncmds; i++)
      if (replies[i]) h->free_reply(replies[i]);
    return RL_ERR_BACKEND;
  }

  /* Free the intermediate STATUS replies; keep the EXEC array. */
  for (size_t i = 0; i < ncmds - 1; i++) {
    h->free_reply(replies[i]);
    replies[i] = NULL;
  }

  redisReply *exec = replies[5];
  if (!exec || exec->type != REDIS_REPLY_ARRAY || exec->elements != 4) {
    if (exec) h->free_reply(exec);
    return RL_ERR_BACKEND;
  }

  redisReply *zrange = exec->element[1];
  if (!zrange || zrange->type != REDIS_REPLY_ARRAY) {
    h->free_reply(exec);
    return RL_ERR_BACKEND;
  }

  size_t n = zrange->elements;
  if (n > 0) {
    long long *ts = malloc(n * sizeof(long long));
    if (!ts) {
      h->free_reply(exec);
      return RL_ERR_NO_MEM;
    }
    for (size_t i = 0; i < n; i++) {
      redisReply *el = zrange->element[i];
      if (!el || !el->str) ts[i] = 0;
      else ts[i]                 = strtoll(el->str, NULL, 10); /* parses leading score */
    }
    out->timestamps      = ts;
    out->count           = n;
    out->owns_timestamps = true;
  }

  h->free_reply(exec);
  return RL_OK;
}

static enum RateLimitResultCode
sRedisClear(struct RateLimitBackend *self, const char *namespace_key)
{
  RedisState *         st = (RedisState*)self->impl;
  RateLimitRedisHooks *h  = &st->hooks;
  if (!h->send_command_multi || !h->get_reply || !h->free_reply) return RL_ERR_MISSING_PARAM;

  h->send_command_multi(h->ctx, h->pers, "DEL %s:%s", CONFIGDEFAULT_RATELIMIT_NS_PREFIX, namespace_key);

  redisReply *raw = NULL;
  if (h->get_reply(h->ctx, h->pers, &raw) != REDIS_OK) {
    if (raw) h->free_reply(raw);
    return RL_ERR_BACKEND;
  }
  if (raw) h->free_reply(raw);
  return RL_OK;
}

static void
sRedisDestroy(struct RateLimitBackend *self)
{
  if (!self) return;
  free(self->impl);
  free(self);
}

static const RateLimitBackendOps sRedisOps = {
  .name = "redis", .Tick = sRedisTick, .Clear = sRedisClear, .Destroy = sRedisDestroy,
};

void
RateLimitHandleBindRedis(RateLimitHandle *handle, CacheBackend *pers, RateLimitApplicationPolicy *app_policy)
{
  if (handle == NULL) return;

  *handle = (RateLimitHandle){
    .backend = NULL, .result = NULL, .app_policy = app_policy,
  };

  if (pers == NULL) {
    syslog(LOG_WARNING, "%s {pid:'%lu'}: ERROR: RATELIMIT COULD NOT BE DETERMINED (no usrmsg cache backend)..",
           __func__, pthread_self());
    return;
  }

  RateLimitRedisHooks hooks = {
    .pers = pers, .ctx = NULL, .send_command_multi = RedisSendCommandMulti, .get_reply = RedisGetReply,
    .free_reply = freeReplyObject,
  };
  handle->backend = RateLimitBackendRedisCreate(&hooks);
  if (handle->backend == NULL) {
    syslog(LOG_WARNING, "%s {pid:'%lu'}: ERROR: RATELIMIT COULD NOT BE DETERMINED (backend create failed)..", __func__,
           pthread_self());
  }
}

struct RateLimitBackend *
RateLimitBackendRedisCreate(const RateLimitRedisHooks *hooks)
{
  if (!hooks || !hooks->send_command_multi || !hooks->get_reply || !hooks->free_reply) return NULL;

  struct RateLimitBackend *be = calloc(1, sizeof(*be));
  if (!be) return NULL;

  RedisState *st = calloc(1, sizeof(*st));
  if (!st) {
    free(be);
    return NULL;
  }
  st->hooks = *hooks;

  be->ops  = &sRedisOps;
  be->impl = st;
  return be;
}
