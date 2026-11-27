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

#ifndef UFSRVCORELIB_VERSION_H
#define UFSRVCORELIB_VERSION_H

#define UFSRVCORELIB_MAJOR 2
#define UFSRVCORELIB_MINOR 0
#define UFSRVCORELIB_PATCH 0
#define UFSRVCORELIB_INTERNAL 0
#define UFSRV_MONOREPO_STEM_VERSION "3.9.1" //historical, representing the version point at which the library was separated from the original monorepo ufsrv

const char *UfsrvCoreLibVersion()      __attribute__((const));
const char *UfsrvCoreLibVersionMajor() __attribute__((const));
const char *UfsrvCoreLibVersionMinor() __attribute__((const));
const char *UfsrvCoreLibVersionPatch() __attribute__((const));
const char *UfsrvCoreLibVersionInternal() __attribute__((const));

#endif //UFSRVCORELIB_VERSION_H
