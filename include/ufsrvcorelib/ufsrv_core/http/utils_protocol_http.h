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


#ifndef UFSRV_UTILS_PROTOCOL_HTTP_H
#define UFSRV_UTILS_PROTOCOL_HTTP_H

#include <stdbool.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include "protocol_http_type.h"
#include "ufsrvcorelib/ufsrv_core/http/http_session_type.h"
#include <ufsrvcorelib/ufsrv_core/http/http_request_handler.h>


typedef onion_handler *(*HttpProtocolAuthFactory)(void *context, onion_handler *next);
typedef onion_handler *(*HttpProtocolErrorFactory)(void *context);

PUBLIC_API ProtocolHttp *HttpProtocolCreate(size_t max_post_size, size_t max_file_size);
PUBLIC_API void HttpProtocolDestroy(ProtocolHttp *http);
PUBLIC_API bool HttpProtocolAddRoute(ProtocolHttp *http, const char *pattern, onion_handler_handler endpoint);
PUBLIC_API bool HttpProtocolSetAuth(ProtocolHttp *http, HttpProtocolAuthFactory factory, void *context);
PUBLIC_API bool HttpProtocolSetError(ProtocolHttp *http, HttpProtocolErrorFactory factory, void *context);
PUBLIC_API onion_connection_status HttpProtocolDispatch(const ProtocolHttp *http, InstanceHolderForSession *session,
                                                        onion_request *request, onion_response *response);

size_t HttpMaxPostSize(const ProtocolHttp *protocol_http_ptr);
size_t HttpMaxFileSize(const ProtocolHttp *protocol_http_ptr);

void WriteResponseCodeXXX(InstanceHolderForSession * _Nonnull instance_sesn_ptr, HttpSession * _Nonnull http_ptr, unsigned response_code, const char *code_xxx, __unused const char *user_msg);
void WriteAllowedOrigin(onion_response *response_ptr, const char *origin);
void WriteAccessControlAllowMethods(onion_response *response_ptr, const char *allowed_methods);
void WriteAccessControlAllowHeaders(onion_response *response_ptr, const char *allowed_headers);

void WriteCORSMandatedHeaders(HttpSession * _Nonnull http_ptr, const char *origin, unsigned response_code);

#endif //UFSRV_UTILS_PROTOCOL_HTTP_H

