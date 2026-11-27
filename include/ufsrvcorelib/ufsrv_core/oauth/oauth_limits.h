/*

 Copyright (c) 2015-2026 unfacd works

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */


#ifndef UFSRV_OAUTH_LIMITS_H
#define UFSRV_OAUTH_LIMITS_H

#define CONFIGDEFAULT_OAUTH_CLIENT_NAME_MAX_SZ    128 ///* 128 char max size for client name
#define CONFIGDEFAULT_OAUTH_CLIENT_NAME_MIN_SZ    2 ///* 128 char max size for client name
#define CONFIGDEFAULT_OAUTH_CLIENT_SECRET_SZ      40
#define CONFIGDEFAULT_OAUTH_AUTH_CODE_EXPIRY     600 //(seconds) 10 minutes
#define CONFIGDEFAULT_OAUTH_AUTH_CODE_SZ          64  //hex chars for authorization code
#define CONFIGDEFAULT_OAUTH_AUTH_REDIRECTS_MAX    5   ///> maximum amount of url redirects per client setup
#define CONFIGDEFAULT_OAUTH_TOKEN_SZ              64  //hex chars for access and refresh tokens
#define CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_MAX_SZ   2048 //a signed access token outgrows the opaque handle
#define CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_EXPIRY   3600    //(seconds) 1 hour
#define CONFIGDEFAULT_OAUTH_REFRESH_TOKEN_EXPIRY  2592000 //(seconds) 30 days
#define CONFIGDEFAULT_OAUTH_REFRESH_FAMILY_SZ     32      //hex chars identifying one rotation lineage
#define CONFIGDEFAULT_OAUTH_CIMD_URL_MAX_SZ              512
#define CONFIGDEFAULT_OAUTH_CIMD_DOCUMENT_MAX_SZ         5120
#define CONFIGDEFAULT_OAUTH_CIMD_FETCH_CONNECT_TIMEOUT   3
#define CONFIGDEFAULT_OAUTH_CIMD_FETCH_TIMEOUT           5
#define CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL               300
#define CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL_MIN            60
#define CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL_MAX          3600
#define CONFIGDEFAULT_OAUTH_CIMD_FAILURE_THROTTLE         30
#define CONFIGDEFAULT_OAUTH_PUBLIC_CLIENT_ALLOWED_SCOPES "[\"openid\",\"profile\",\"email\",\"read\",\"write\"]"

#endif
