/**
 * @file pretty_print_scope_enum.h
 * @brief pretty_print_scope_enum
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_PRETTY_PRINT_SCOPE_ENUM_H
#define UFSRVCORELIB_PRETTY_PRINT_SCOPE_ENUM_H

enum PrettyPrintScope
{
  PRETTY_PRINT_SCOPE_DEFAULT = 0, ///> Generally assumed PRETTY_PRINT_SCOPE_DIGEST
  PRETTY_PRINT_SCOPE_DIGEST  = 1, ///> generate minimal informational output
  PRETTY_PRINT_SCOPE_FULL    = 2, ///> generate full reflection of all settable attributes
};
#endif //UFSRVCORELIB_PRETTY_PRINT_SCOPE_ENUM_H
