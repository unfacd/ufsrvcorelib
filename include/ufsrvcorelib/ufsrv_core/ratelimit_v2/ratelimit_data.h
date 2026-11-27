/**
 * @file ratelimit_data.h
 * @brief Built-in rate-limit policy table for the RL_CAT_* categories.
 *
 * Internal-linkage data included by ratelimit.c only; consumers obtain a
 * policy through RateLimitSpecsFor() rather than this header.
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

#ifndef RATELIMIT_V2_DATA_H_
#define RATELIMIT_V2_DATA_H_

#include <ufsrv_core/ratelimit_v2/ratelimit_type.h>

/*!
 * Built-in policy table, indexed by (enum value - 1).  Same numbers as the
 * original module:
 *   - RL_CAT_CONNECTONS: 1 connection / 1000 ms, 100 ms minimum gap.
 *   - RL_CAT_REQUESTS:   20 requests / 1000 ms, no minimum gap.
 */
static const RateLimitPolicy sRateLimitSpecs[] = {
    { .namespace       = RL_CAT_CONNECTONS,
      .interval        = 1000,
      .max_in_interval = 1,
      .min_difference  = 100 },
    { .namespace       = RL_CAT_REQUESTS,
      .interval        = 1000,
      .max_in_interval = 20,
      .min_difference  = 0 },
};

#endif /* RATELIMIT_V2_DATA_H_ */
