/**
 * @file ratelimit_type.h
 * @brief Value types and result codes shared by the ratelimit_v2 public API and
 *        its storage backends.
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

#ifndef RATELIMIT_V2_TYPE_H_
#define RATELIMIT_V2_TYPE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! Logical rate-limit categories (index into the built-in specs table). */
enum RateLimitCategory {
    RL_CAT_CONNECTONS = 1,  ///< connection establishment rate limit
    RL_CAT_REQUESTS,        ///< per-interval request count limit
    RL_CAT_MAXVALUE         ///< sentinel; values >= this are out of range
};

/*!
 * Policy for one namespace category.
 *
 * `interval` and `min_difference` are expressed in milliseconds.
 */
typedef struct RateLimitPolicy {
    enum RateLimitCategory namespace; ///< category this policy belongs to
    size_t interval;                           ///< window length (ms)
    size_t max_in_interval;                    ///< max requests allowed inside the window
    size_t min_difference;                     ///< min gap between successive requests (ms); 0 = off
} RateLimitPolicy;

/*!
 * Outcome of a single rate-limit check (after recording the current attempt).
 *
 * `remaining_actions` semantics:
 *   - > 0 : how many more are allowed in the window
 *   - = 0 : this was the last allowed
 *   - < 0 : exceeded
 *
 * `remaining_time_ms` is the wait hint when exceeded; 0 when allowed.
 */
typedef struct RateLimitStatus {
    size_t  remaining_time_ms;  ///< wait hint (ms) when exceeded; 0 when allowed
    ssize_t remaining_actions;  ///< see semantics above
} RateLimitStatus;

/*! Result codes returned by the rate-limit API. */
enum RateLimitResultCode {
    RL_OK = 0,                 ///< success; status_out was filled
    RL_ERR_MISSING_PARAM,      ///< NULL or otherwise invalid argument
    RL_ERR_INVALID_NAMESPACE,  ///< namespace failed safety validation
    RL_ERR_BACKEND,            ///< backend error (fail-closed)
    RL_ERR_NO_MEM              ///< allocation failure
};

/*! Per-call result record; `user_data` typically points at RateLimitStatus. */
typedef struct RateLimitResult {
    enum RateLimitResultCode code;      ///< result code
    void                    *user_data; ///< typically RateLimitStatus *
} RateLimitResult;

#ifdef __cplusplus
}
#endif

#endif /* RATELIMIT_V2_TYPE_H_ */
