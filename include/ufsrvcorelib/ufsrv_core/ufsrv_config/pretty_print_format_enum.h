/**
* @file pretty_print_format_enum.h
 * @brief Build-time descriptor, runtime field table entry, domain/source/validation enums.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_PRETTY_PRINT_FORMAT_ENUM_H
#define UFSRVCORELIB_PRETTY_PRINT_FORMAT_ENUM_H

enum PrettyPrintFormat
{
  PRETTY_PRINT_DEFAULT      = 0, ///< When unassigned, will default to PRIV_CONFIG_DEFAULT_UFSRV_CONFIG_PRETTY_FORMATTER
  PRETTY_PRINT_JSON         = 1, ///  standatd json
  PRETTY_PRINT_INI          = 2, ///< INI style
  PRETTY_PRINT_YAML         = 3, ///< YAML
  PRETTY_PRINT_UFSRV_CONFIG = 4, ///<  ufsrv config format
};

#endif //UFSRVCORELIB_PRETTY_PRINT_FORMAT_ENUM_H
