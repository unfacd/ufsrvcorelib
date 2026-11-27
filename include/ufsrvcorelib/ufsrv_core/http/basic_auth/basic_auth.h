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

#ifndef UFSRV_BASIC_AUTH_H
#define UFSRV_BASIC_AUTH_H

#include <ufsrvcorelib/ufsrv_core/ufsrvresult_type.h>
#include <ufsrvcorelib/ufsrv_core/http/basic_auth/basic_auth_descriptor_type.h>
#include <ufsrvcorelib/ufsrv_core/http/http_request_handler.h>

typedef struct onion_handler_auth_pam_data_t onion_handler_auth_pam_data;

#define INVALIDATE_CACHEBACKEND true

int CacheValidateBasicAuth(const char *basicauth_b64encoded, const char *basicauth_decoded, unsigned long *);
int CacheInvalidateBasicAuth(unsigned long userid, bool is_invalidate_backend);
BasicAuthDescriptor *CacheLocalLruSetBasicAuthItem(const char *basicauth_b64encoded, const char *basicauth_decoded, unsigned long userid, BasicAuthDescriptor *basicauth_ptr_in);
UFSRVResult *CacheBackendSetBasicAuthItem(const char *basicauth_b64encoded, const char *basicauth_decoded, unsigned long userid);
void InitialiseBasicAuthLruCache(size_t allocation_sz);

#endif //UFSRV_BASIC_AUTH_H
