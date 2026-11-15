//
// Created by ayman on 7/09/2026.
//

/**
 * @file authenticated_access_type.h
 * @brief authenticated_access_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 7/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_AUTHENTICATED_ACCESS_TYPE_H
#define UFSRVCORELIB_AUTHENTICATED_ACCESS_TYPE_H
#include <standard_defs.h>
#include <sys/time.h>

enum AccessTier
{
  ACCESS_TIER_DEFAULT = 0, ///< system defined
  ACCESS_TIER_GOD,
  ACCESS_TIER_MANAGER,  ///< may change resources
  ACCESS_TIER_BROWSER,  ///< read-only
  ACCESS_TIER_UNTRUSTED
};

typedef struct AuthenticatedAccess AuthenticatedAccess;
struct AuthenticatedAccess
{
  enum AccessTier access_tier;
  char fingerprint[MBUF]; ///< certificate
  time_t access_duration; ///< (seconds) how much time access is granted for since access granted
  time_t  when_access_granted;  ///<
};
#endif //UFSRVCORELIB_AUTHENTICATED_ACCESS_TYPE_H
