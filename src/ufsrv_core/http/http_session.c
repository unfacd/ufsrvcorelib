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

#include <stdint.h>
#include <unistd.h>

#include <ufsrvcorelib/ufsrv_core/http/dictionary.h>
#include <ufsrvcorelib/ufsrv_core/http/http_session.h>
#include <ufsrvcorelib/ufsrv_core/http/request.h>

SessionStorageLayout
HttpSessionGetLayout(void)
{
  return (SessionStorageLayout){.size = sizeof(HttpSession), .alignment = _Alignof(HttpSession)};
}

HttpSession *
HttpSessionInit(void *storage, size_t storage_size, size_t max_post_size, size_t max_file_size)
{
  if (!storage || storage_size < sizeof(HttpSession) || (uintptr_t)storage % _Alignof(HttpSession)) return NULL;

  HttpSession *http     = storage;
  *http                 = (HttpSession){.send_file_fd = -1};
  http->request.headers = onion_dict_new();
  if (!http->request.headers) return NULL;

  onion_dict_set_flags(http->request.headers, OD_ICASE);
  http->request.max_post_size = max_post_size;
  http->request.max_file_size = max_file_size;
  return http;
}

void
HttpSessionDeinit(HttpSession *http)
{
  if (!http) return;

  onion_request_free(&http->request);
  HttpSessionClearResponse(http);
  HttpSessionSetJson(http, NULL);
  HttpSessionCloseSendFile(http);
  *http = (HttpSession){.send_file_fd = -1};
}

void
HttpSessionCleanRequest(HttpSession *http)
{
  onion_request_clean(&http->request);
  HttpSessionSetJson(http, NULL);
}

void
HttpSessionClearResponse(HttpSession *http)
{
  if (http->response.headers) onion_dict_free(http->response.headers);
  http->response.headers = NULL;
  http->response.flags   = 0;
}

onion_request *
HttpSessionGetRequest(HttpSession *http)
{
  return http ? &http->request : NULL;
}

onion_response *
HttpSessionGetResponse(HttpSession *http)
{
  return http ? &http->response : NULL;
}

json_object *
HttpSessionGetJson(const HttpSession *http)
{
  return http->json;
}

void
HttpSessionSetJson(HttpSession *http, json_object *json)
{
  if (http->json) json_object_put(http->json);
  http->json = json;
}

void
HttpSessionClearJson(HttpSession *http)
{
  if (!http || !http->json) return;

  json_object_object_foreach(http->json, key, value) {
    (void)value;
    json_object_object_del(http->json, key);
  }
}

void
HttpSessionSetSendFile(HttpSession *http, int fd, size_t size)
{
  http->send_file_fd   = fd;
  http->send_file_size = size;
}

void
HttpSessionCloseSendFile(HttpSession *http)
{
  if (http->send_file_fd >= 0) close(http->send_file_fd);
  HttpSessionSetSendFile(http, -1, 0);
}

int
HttpSessionGetSendFileFd(const HttpSession *http)
{
  return http ? http->send_file_fd : -1;
}

size_t
HttpSessionGetSendFileSize(const HttpSession *http)
{
  return http->send_file_size;
}
