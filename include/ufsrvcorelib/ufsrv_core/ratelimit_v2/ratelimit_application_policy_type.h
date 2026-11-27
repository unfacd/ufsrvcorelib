/**
 * @file ratelimit_application_policy_type.h
 * @brief ratelimit_application_policy_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufnetcorelib source code.
 * Created by ayman on 7/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFNETCORELIB_RATELIMIT_APPLICATION_POLICY_TYPE_H
#define UFNETCORELIB_RATELIMIT_APPLICATION_POLICY_TYPE_H
#include <stdbool.h>

typedef struct RateLimitApplicationPolicy
{
  bool is_enabled;
  bool is_bypass_private_ip;
} RateLimitApplicationPolicy;

#endif //UFNETCORELIB_RATELIMIT_APPLICATION_POLICY_TYPE_H
