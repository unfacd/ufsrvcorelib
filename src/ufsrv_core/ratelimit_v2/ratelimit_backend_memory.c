/**
 * @file ratelimit_backend_memory.c
 * @brief In-process sliding-window backend (mutex + per-namespace ordered events).
 *
 * Suitable for single-process limits and deterministic unit tests.  A single
 * mutex guards a list of per-namespace buckets, each holding an ordered list
 * of event timestamps (microseconds).  Idle buckets are evicted once the
 * bucket count exceeds RL_MEM_MAX_BUCKETS, bounding long-run memory use.
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bound on the number of live namespace buckets.  Exceeding it triggers a
 * sweep that evicts idle buckets (the Redis backend bounds this via EXPIRE). */
#define RL_MEM_MAX_BUCKETS 4096

/* Max size of a composed storage key ("_RL:" + namespace). */
#define RL_NS_STORAGE_KEY_MAX 256

typedef struct MemEvent
{
  long long t_us;
} MemEvent;

typedef struct MemBucket
{
  char *            key;
  MemEvent *        events;
  size_t            len;
  size_t            cap;
  unsigned long     expire_sec; /* soft TTL; used as the idle-eviction threshold */
  long long         last_touch_us;
  struct MemBucket *next;
} MemBucket;

typedef struct MemState
{
  pthread_mutex_t lock;
  MemBucket *     buckets; /* singly-linked; fine for modest cardinality */
  size_t          count;   /* number of live buckets */
} MemState;

static MemBucket *
sFind(MemState *st, const char *key)
{
  for (MemBucket *b = st->buckets; b; b = b->next) {
    if (strcmp(b->key, key) == 0) return b;
  }
  return NULL;
}

static MemBucket *
sGetOrCreate(MemState *st, const char *key)
{
  MemBucket *b = sFind(st, key);
  if (b) return b;

  b = calloc(1, sizeof(*b));
  if (!b) return NULL;
  b->key = strdup(key);
  if (!b->key) {
    free(b);
    return NULL;
  }
  b->next     = st->buckets;
  st->buckets = b;
  st->count++;
  return b;
}

static void
sBucketPrune(MemBucket *b, long long clear_before_us)
{
  size_t w = 0;
  for (size_t r = 0; r < b->len; r++) {
    if (b->events[r].t_us >= clear_before_us) b->events[w++] = b->events[r];
  }
  b->len = w;
}

static enum RateLimitResultCode
sBucketPush(MemBucket *b, long long t_us)
{
  if (b->len == b->cap) {
    size_t    ncap = b->cap ? b->cap * 2 : 16;
    MemEvent *ne   = realloc(b->events, ncap * sizeof(MemEvent));
    if (!ne) return RL_ERR_NO_MEM;
    b->events = ne;
    b->cap    = ncap;
  }
  b->events[b->len++].t_us = t_us;
  return RL_OK;
}

/* Drop buckets whose last touch is older than their soft TTL.  Caller holds
 * the lock. */
static void
sEvictIdle(MemState *st, long long now_us)
{
  MemBucket **pp = &st->buckets;
  while (*pp) {
    MemBucket *b       = *pp;
    long long  idle_us = now_us - b->last_touch_us;
    if (b->last_touch_us != 0 && idle_us > (long long)b->expire_sec * 1000000LL) {
      *pp = b->next;
      free(b->key);
      free(b->events);
      free(b);
      st->count--;
    }
    else {
      pp = &(*pp)->next;
    }
  }
}

/* Compose the backend storage key ("_RL:<namespace>").  Returns false if it
 * would not fit (defensive; validated namespaces are short). */
static bool
sStorageKey(char *buf, size_t buflen, const char *namespace_key)
{
  int n = snprintf(buf, buflen, "%s:%s", CONFIGDEFAULT_RATELIMIT_NS_PREFIX, namespace_key);
  return n >= 0 && (size_t)n < buflen;
}

static enum RateLimitResultCode
sMemTick(struct RateLimitBackend *self, const RateLimitTickParams *params, RateLimitWindowSnapshot *out)
{
  MemState *st = (MemState*)self->impl;
  memset(out, 0, sizeof(*out));

  char key[RL_NS_STORAGE_KEY_MAX];
  if (!sStorageKey(key, sizeof key, params->namespace_key))
    return RL_ERR_INVALID_NAMESPACE;

  pthread_mutex_lock(&st->lock);

  MemBucket *b = sGetOrCreate(st, key);
  if (!b) {
    pthread_mutex_unlock(&st->lock);
    return RL_ERR_NO_MEM;
  }

  sBucketPrune(b, params->clear_before_us);

  /* Snapshot the window (before recording this request). */
  if (b->len > 0) {
    long long *copy = malloc(b->len * sizeof(long long));
    if (!copy) {
      pthread_mutex_unlock(&st->lock);
      return RL_ERR_NO_MEM;
    }
    for (size_t i = 0; i < b->len; i++) copy[i] = b->events[i].t_us;
    out->timestamps      = copy;
    out->count           = b->len;
    out->owns_timestamps = true;
  }

  enum RateLimitResultCode rc = sBucketPush(b, params->score_us);
  if (rc != RL_OK) {
    if (out->owns_timestamps) {
      free(out->timestamps);
      memset(out, 0, sizeof(*out));
    }
    pthread_mutex_unlock(&st->lock);
    return rc;
  }

  b->expire_sec    = params->expire_sec;
  b->last_touch_us = params->now_us;

  if (st->count > RL_MEM_MAX_BUCKETS) sEvictIdle(st, params->now_us);

  pthread_mutex_unlock(&st->lock);
  return RL_OK;
}

static enum RateLimitResultCode
sMemClear(struct RateLimitBackend *self, const char *namespace_key)
{
  MemState *st = (MemState*)self->impl;

  char key[RL_NS_STORAGE_KEY_MAX];
  if (!sStorageKey(key, sizeof key, namespace_key))
    return RL_ERR_INVALID_NAMESPACE;

  pthread_mutex_lock(&st->lock);

  MemBucket **pp = &st->buckets;
  while (*pp) {
    if (strcmp((*pp)->key, key) == 0) {
      MemBucket *dead = *pp;
      *pp             = dead->next;
      free(dead->key);
      free(dead->events);
      free(dead);
      st->count--;
      pthread_mutex_unlock(&st->lock);
      return RL_OK;
    }
    pp = &(*pp)->next;
  }

  pthread_mutex_unlock(&st->lock);
  return RL_OK; /* missing key is fine */
}

static void
sMemDestroy(struct RateLimitBackend *self)
{
  if (!self) return;
  MemState *st = (MemState*)self->impl;
  if (st) {
    MemBucket *b = st->buckets;
    while (b) {
      MemBucket *n = b->next;
      free(b->key);
      free(b->events);
      free(b);
      b = n;
    }
    pthread_mutex_destroy(&st->lock);
    free(st);
  }
  free(self);
}

static const RateLimitBackendOps sMemOps = {
  .name = "memory", .Tick = sMemTick, .Clear = sMemClear, .Destroy = sMemDestroy,
};

struct RateLimitBackend *
RateLimitBackendMemoryCreate(void)
{
  struct RateLimitBackend *be = calloc(1, sizeof(*be));
  if (!be) return NULL;

  MemState *st = calloc(1, sizeof(*st));
  if (!st) {
    free(be);
    return NULL;
  }
  if (pthread_mutex_init(&st->lock, NULL) != 0) {
    free(st);
    free(be);
    return NULL;
  }

  be->ops  = &sMemOps;
  be->impl = st;
  return be;
}
