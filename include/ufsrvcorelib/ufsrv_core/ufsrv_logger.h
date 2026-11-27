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

#ifndef UFSRVCORELIB_UFSRV_CORE_UFSRV_LOGGER_H
#define UFSRVCORELIB_UFSRV_CORE_UFSRV_LOGGER_H

#include <uflib/logger/logger_type.h>

const UfLoggerConfig *ProvideDefaultLoggerConfig(void);
UfLogger *OpenLogger(const UfLoggerConfig *logger_config);
UfLogger *UfsrvLogger(void);

#endif /* UFSRVCORELIB_UFSRV_CORE_UFSRV_LOGGER_H */
