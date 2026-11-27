/**
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

/**
 * @file config_defs_ufsrvcorelib.h
 * @brief Compile-time configuration defaults for ufsrvcorelib.
 *
 * CONFIG_DEFAULT_* constants may be overridable at CMake configure time
 * via a generated config.h.  PRIV_CONFIG_DEFAULT_* constants are not.
 * Items are defined in cmake-modules/config_defaults_definitions.cmake
 */

#ifndef UFSRVCORELIB_CONFIG_DEFS_UFSRVCORELIB_H
#define UFSRVCORELIB_CONFIG_DEFS_UFSRVCORELIB_H

#define PRIV_CONFIG_DEFAULT_UFSRV_CONFIG_PRETTY_FORMATTER PRETTY_PRINT_JSON ///< enum as defined in pretty_print_format_enum.h

#define PRIV_CONFIG_DEFAULT_UFSRV_CONFIG_PRETTY_FORMATTER_SCOPE PRETTY_PRINT_SCOPE_DIGEST

#define CONFIG_DBBACKEND_DBNAME "ufsrv"

#define _CONFIDEFAULT_HASHTABLE_BASICAUTH_SZ		2
#define _BASICAUTH_PREFIX	"_BASICAUTH"
#define _BASICAUTH_CACHE_EXPIRY	86400UL

#endif /* UFSRVCORELIB_CONFIG_DEFS_UFSRVCORELIB_H */
