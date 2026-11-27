/**
 * @file ratelimit.h
 * @brief Public backend-agnostic rate-limit API.
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

#ifndef RATELIMIT_V2_H_
#define RATELIMIT_V2_H_

#include <ufsrvcorelib_defs.h>

#include <ufsrv_core/ratelimit_v2/ratelimit_type.h>
#include <ufsrv_core/delegator_sessions/ratelimit_handle_type.h>
#include <ufsrv_core/ratelimit_v2/ratelimit_backend.h>
#include <ufsrv_core/ratelimit_v2/ratelimit_application_policy_type.h>

#ifdef __cplusplus
extern "C" {
#endif


/** @brief Install the policy the ratelimit module applies; the value is copied. */
void RateLimitSetApplicationPolicy (const RateLimitApplicationPolicy *application_policy);

/*!
 * Atomically query-and-update the sliding window for `namespace_key`.
 *
 * On RL_OK, `*status_out` is filled (required to be non-NULL).  If
 * `handle->result` is set, it is also updated (`user_data` points at
 * `status_out`).  Fail-closed helpers treat any non-RL_OK result as
 * "exceeded".
 *
 * @code{.c}
 * RateLimitStatus st;
 * if (RateLimitCheck(&handle, "42:7:2", spec, &st) == RL_OK && st.remaining_actions >= 0)
 *     allow();
 * else
 *     deny();
 * @endcode
 */
PUBLIC_API enum RateLimitResultCode
RateLimitCheck(RateLimitHandle *handle, const char *namespace_key, const RateLimitPolicy *spec, RateLimitStatus *status_out);

/*! Built-in specs for RL_CAT_* categories; NULL if the category is out of range. */
PUBLIC_API const RateLimitPolicy *RateLimitSpecsFor(enum RateLimitCategory category);

/*!
 * Convenience: true if the namespace is over limit (or on any error).
 * Uses RateLimitSpecsFor(category) when `spec` is NULL.
 */
PUBLIC_API bool
RateLimitIsExceeded(RateLimitHandle *handle, const char *namespace_key, const RateLimitPolicy *spec);

/*! Convenience: RateLimitIsExceeded with a built-in category spec. */
PUBLIC_API bool
RateLimitIsExceededForCategory(RateLimitHandle *handle, const char *namespace_key, enum RateLimitCategory category);

/*!
 * Session-style key helper: writes "<uid>:<cid>:<category>" into `buf`.
 * Returns false if snprintf would truncate or an argument is invalid.
 */
PUBLIC_API bool
RateLimitFormatSessionNamespace(char *                 buf, size_t buflen, unsigned long userid, unsigned long cid,
                                enum RateLimitCategory category);

/*! Validate a namespace for backend safety (no empty, space, control, '%'). */
PUBLIC_API bool
RateLimitIsNamespaceValid(const char *namespace_key);

/*!
 * Current wall-clock time in microseconds (CLOCK_REALTIME).
 *
 * Defined as a weak symbol so deterministic tests can override it with a
 * strong definition returning a fixed clock.  Weak is used rather than a linker
 * `--wrap` target because `--wrap` only rewrites *undefined* references — it
 * cannot intercept an intra-translation-unit call — whereas a weak symbol stays
 * interposable and is overridden by the test's strong definition.  Consumers
 * should not normally call this directly.
 */
PUBLIC_API long long
RateLimitNowInMicros(void);

#ifdef __cplusplus
}
#endif

#endif /* RATELIMIT_V2_H_ */
