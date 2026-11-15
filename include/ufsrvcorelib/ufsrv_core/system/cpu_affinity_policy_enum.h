/**
 * @file cpu_affinity_policy_enum.h
 * @brief CPU affinity policy enum and string conversion helpers.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CPU_AFFINITY_POLICY_ENUM_H
#define CPU_AFFINITY_POLICY_ENUM_H

/*!
 * CPU affinity policy — supplied by the consumer's config layer.
 *
 * The module does not read config itself; the host parses its own
 * config and passes the resulting enum value to UfsrvCpuAffinityInit().
 */
typedef enum CpuAffinityPolicy {
  CPU_AFFINITY_SYSTEM_OVERRIDE = 0,  ///< no pinning — OS scheduler decides
  CPU_AFFINITY_MANAGED         = 1,  ///< server chooses strategy, may cap pools
  CPU_AFFINITY_USER_OVERRIDE   = 2   ///< user pools honoured, grouping used
} CpuAffinityPolicy;

/*!
 * Return the config-literal string for a CpuAffinityPolicy value.
 * Unknown values fall back to "system_override".
 */
static inline const char *
CpuAffinityPolicyToLiteral(CpuAffinityPolicy policy)
{
  switch (policy) {
    case CPU_AFFINITY_MANAGED:       return "managed";
    case CPU_AFFINITY_USER_OVERRIDE: return "user_override";
    case CPU_AFFINITY_SYSTEM_OVERRIDE:
    default:                         return "system_override";
  }
}

#endif /* CPU_AFFINITY_POLICY_ENUM_H */
