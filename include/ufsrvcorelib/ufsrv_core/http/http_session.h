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

#ifndef UFSRVCORELIB_HTTP_SESSION_H
#define UFSRVCORELIB_HTTP_SESSION_H

#include <json-c/json.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <ufsrvcorelib/ufsrv_core/http/http_request_handler.h>
#include <ufsrvcorelib/ufsrv_core/session/session_layout.h>

typedef struct HttpSession
{
  onion_request  request;         ///< Parsed request of the connection in hand
  onion_response response;        ///< Reply being composed for that request
  json_object *  json;            ///< Request body, allocated once and emptied between requests
  int            send_file_fd;    ///< File being streamed, -1 when there is none
  size_t         send_file_size;  ///< Declared length of that file
} HttpSession;

PUBLIC_API SessionStorageLayout HttpSessionGetLayout(void);
PUBLIC_API HttpSession *HttpSessionInit(void *storage, size_t storage_size,
                                        size_t max_post_size, size_t max_file_size);
PUBLIC_API void HttpSessionDeinit(HttpSession *http);

PUBLIC_API void HttpSessionCleanRequest(HttpSession *http);
PUBLIC_API void HttpSessionClearResponse(HttpSession *http);

PUBLIC_API onion_request *HttpSessionGetRequest(HttpSession *http);
PUBLIC_API onion_response *HttpSessionGetResponse(HttpSession *http);
PUBLIC_API json_object *HttpSessionGetJson(const HttpSession *http);
PUBLIC_API void HttpSessionSetJson(HttpSession *http, json_object *json);
PUBLIC_API void HttpSessionClearJson(HttpSession *http);
PUBLIC_API void HttpSessionSetSendFile(HttpSession *http, int fd, size_t size);
PUBLIC_API void HttpSessionCloseSendFile(HttpSession *http);
PUBLIC_API int HttpSessionGetSendFileFd(const HttpSession *http);
PUBLIC_API size_t HttpSessionGetSendFileSize(const HttpSession *http);

#endif
