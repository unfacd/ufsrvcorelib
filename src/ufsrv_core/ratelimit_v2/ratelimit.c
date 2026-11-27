/**
 * @file ratelimit.c
 * @brief Backend-agnostic sliding-window rate-limit core.
 *
 * The algorithm here is independent of any storage backend: it validates the
 * namespace, computes the window parameters, drives one atomic backend tick,
 * and interprets the returned snapshot.  Storage lives behind
 * RateLimitBackendOps (see ratelimit_backend.h).
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

#include <time.h>
#include <ufsrv_core/ratelimit_v2/ratelimit.h>
#include <ufsrv_core/ratelimit_v2/ratelimit_data.h>

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static RateLimitApplicationPolicy s_application_policy;

void
RateLimitSetApplicationPolicy(const RateLimitApplicationPolicy *application_policy)
{
  if (application_policy) s_application_policy = *application_policy;
}

/* Monotonic per-process counter: each Redis ZADD member suffixes the timestamp
 * with this value so identical-microsecond requests stay distinct (finding
 * M3). */
static _Atomic unsigned long sMemberSeq = 0;

const RateLimitPolicy *
RateLimitSpecsFor(enum RateLimitCategory category)
{
  if (category < RL_CAT_CONNECTONS || category >= RL_CAT_MAXVALUE) return NULL;
  return &sRateLimitSpecs[category - 1];
}

bool
RateLimitIsNamespaceValid(const char *namespace_key)
{
  if (namespace_key == NULL || namespace_key[0] == '\0') return false;
  for (const unsigned char *p = (const unsigned char*)namespace_key; *p; p++) {
    if (isspace(*p) || iscntrl(*p) || *p == '%') return false;
  }
  return true;
}

bool
RateLimitFormatSessionNamespace(char *buf, size_t buflen, unsigned long userid, unsigned long cid, enum RateLimitCategory category)
{
  if (buf == NULL || buflen == 0) return false;
  int n = snprintf(buf, buflen, "%lu:%lu:%d", userid, cid, (int)category);
  return n > 0 && (size_t)n < buflen;
}

static long long
sMinLl(long long a, long long b)
{
  return a < b ? a : b;
}

static void
sFillResult(RateLimitHandle *handle, enum RateLimitResultCode code, void *ud)
{
  if (handle && handle->result) {
    handle->result->code      = code;
    handle->result->user_data = ud;
  }
}

/* Weak so a test translation unit can override this with a strong fixed-clock
 * definition.  ld --wrap=... cannot do this: it only rewrites *undefined*
 * (cross-object) references, so it silently misses the intra-object call from
 * RateLimitCheck in this same file.  A weak symbol stays interposable, so a
 * strong definition elsewhere wins even for that same-file call — keeping the
 * clock here while still letting the tests drive a fixed time. */
__attribute__((weak))
long long
RateLimitNowInMicros(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    return 0;
  return (long long)ts.tv_sec * 1000000LL + (long long)ts.tv_nsec / 1000LL;
}

enum RateLimitResultCode
RateLimitCheck(RateLimitHandle *handle, const char *namespace_key, const RateLimitPolicy *spec, RateLimitStatus *status_out)
{
  if (handle == NULL || handle->backend == NULL || namespace_key == NULL || spec == NULL || status_out == NULL) {
    sFillResult(handle, RL_ERR_MISSING_PARAM, NULL);
    return RL_ERR_MISSING_PARAM;
  }
  if (!RateLimitIsNamespaceValid(namespace_key)) {
    sFillResult(handle, RL_ERR_INVALID_NAMESPACE, NULL);
    return RL_ERR_INVALID_NAMESPACE;
  }

  memset(status_out, 0, sizeof(*status_out));

  long long     now_us       = RateLimitNowInMicros();
  long long     clear_before = now_us - (long long)spec->interval * 1000LL;
  unsigned long expire_sec   = (unsigned long)((spec->interval + 999) / 1000);
  if (expire_sec == 0) expire_sec = 1;

  unsigned long nonce = atomic_fetch_add(&sMemberSeq, 1) + 1;
  char          member[64];
  snprintf(member, sizeof(member), "%lld-%lu", now_us, nonce);

  RateLimitTickParams params = {
    .namespace_key = namespace_key, .now_us = now_us, .clear_before_us = clear_before, .expire_sec = expire_sec,
    .score_us      = now_us, .member        = member,
  };

  RateLimitWindowSnapshot snap;
  memset(&snap, 0, sizeof(snap));

  enum RateLimitResultCode rc = RateLimitBackendTick(handle->backend, &params, &snap);
  if (rc != RL_OK) {
    sFillResult(handle, rc, NULL);
    return rc;
  }

  size_t userset_sz = snap.count;

  if (userset_sz == 0) {
    status_out->remaining_actions = (ssize_t)spec->max_in_interval - 1;
    status_out->remaining_time_ms = 0;
    goto done;
  }

  bool too_many = (userset_sz >= spec->max_in_interval);

  long long newest = snap.timestamps[userset_sz - 1];
  long long oldest = snap.timestamps[0];

  long long time_since_last_us = 0;
  if (spec->min_difference > 0) time_since_last_us = now_us - newest;

  if (too_many || (spec->min_difference > 0 && time_since_last_us < (long long)spec->min_difference * 1000LL)) {
    long long v1       = (oldest - now_us) + (long long)spec->interval * 1000LL;
    long long gap_wait = 0;
    if (spec->min_difference > 0) gap_wait = (long long)spec->min_difference * 1000LL - time_since_last_us;

    /* Window slide time is the default wait.  Only let a *positive* gap_wait
     * (min_difference still unsatisfied) shorten it — when the gap is already
     * satisfied, gap_wait is non-positive and must not drag the wait to zero. */
    long long wait_us = v1;
    if (gap_wait > 0) wait_us = sMinLl(v1, gap_wait);

    if (wait_us < 0) wait_us = 0;

    status_out->remaining_time_ms = (size_t)(wait_us / 1000);
    status_out->remaining_actions = -1;
  }
  else {
    status_out->remaining_actions = (ssize_t)spec->max_in_interval - (ssize_t)userset_sz - 1;
    status_out->remaining_time_ms = 0;
  }

done:
  if (snap.owns_timestamps && snap.timestamps) free(snap.timestamps);

  sFillResult(handle, RL_OK, status_out);
  return RL_OK;
}

bool
RateLimitIsExceeded(RateLimitHandle *handle, const char *namespace_key, const RateLimitPolicy *spec)
{
  if (handle == NULL || namespace_key == NULL || spec == NULL) return true;

  RateLimitStatus st;
  if (RateLimitCheck(handle, namespace_key, spec, &st) != RL_OK) return true;

  if (spec->min_difference > 0) return !(st.remaining_time_ms == 0 && st.remaining_actions >= 0);
  return st.remaining_actions < 0;
}

bool
RateLimitIsExceededForCategory(RateLimitHandle *handle, const char *namespace_key, enum RateLimitCategory category)
{
  const RateLimitPolicy *spec = RateLimitSpecsFor(category);
  if (spec == NULL) return true;
  return RateLimitIsExceeded(handle, namespace_key, spec);
}
