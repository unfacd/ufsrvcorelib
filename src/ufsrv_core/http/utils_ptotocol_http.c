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

#include <ufsrvcorelib/ufsrv_core/http/utils_protocol_http.h>
#include <ufsrvcorelib/ufsrv_core/http/http_session_type.h>
#include <ufsrvcorelib/ufsrv_core/http/h_handler.h>
#include <ufsrvcorelib/ufsrv_core/http/url.h>
#include <ufsrvcorelib/ufsrv_core/http/request.h>

#include <stdlib.h>
#include <string.h>

struct ProtocolHttp
{
  size_t         max_post_size;
  size_t         max_file_size;
  onion_url *    routes;
  onion_handler *auth_handler;
  onion_handler *error_handler;
};

ProtocolHttp *
HttpProtocolCreate(size_t max_post_size, size_t max_file_size)
{
  ProtocolHttp *http = calloc(1, sizeof(*http));
  if (!http) return NULL;

  http->max_post_size = max_post_size;
  http->max_file_size = max_file_size;
  http->routes        = onion_root_url();
  http->error_handler = onion_handler_new(onion_default_error, NULL, NULL);
  if (!http->routes || !http->error_handler) {
    HttpProtocolDestroy(http);
    return NULL;
  }

  return http;
}

void
HttpProtocolDestroy(ProtocolHttp *http)
{
  if (!http) return;

  if (http->auth_handler) {
    onion_handler_free(http->auth_handler);
  }
  else {
    onion_url_free(http->routes);
  }
  onion_handler_free(http->error_handler);
  free(http);
}

bool
HttpProtocolAddRoute(ProtocolHttp *http, const char *pattern, onion_handler_handler endpoint)
{
  if (!http || http->auth_handler || !pattern || !endpoint) return false;

  onion_handler *handler = onion_handler_new(endpoint, NULL, NULL);
  if (!handler) return false;
  if (onion_url_add_handler(http->routes, pattern, handler) == 0) return true;

  onion_handler_free(handler);
  return false;
}

bool
HttpProtocolSetAuth(ProtocolHttp *http, HttpProtocolAuthFactory factory, void *context)
{
  if (!http || http->auth_handler || !factory) return false;

  http->auth_handler = factory(context, (onion_handler*)http->routes);
  return http->auth_handler != NULL;
}

bool
HttpProtocolSetError(ProtocolHttp *http, HttpProtocolErrorFactory factory, void *context)
{
  if (!http || !factory) return false;

  onion_handler *handler = factory(context);
  if (!handler) return false;

  onion_handler_free(http->error_handler);
  http->error_handler = handler;

  return true;
}

onion_connection_status
HttpProtocolDispatch(const ProtocolHttp *http, InstanceHolderForSession *session, onion_request *request,
                     onion_response *    response)
{
  if (!http || !http->auth_handler) return OCS_INTERNAL_ERROR;

  onion_connection_status status = onion_handler_handle(session, http->auth_handler, request, response);
  switch (status) {
  case OCS_INTERNAL_ERROR: request->flags |= OR_INTERNAL_ERROR;
    break;
  case OCS_NOT_IMPLEMENTED: request->flags |= OR_NOT_IMPLEMENTED;
    break;
  case OCS_NOT_PROCESSED: request->flags |= OR_NOT_FOUND;
    break;
  case OCS_FORBIDDEN: request->flags |= OR_FORBIDDEN;
    break;
  default: return status;
  }

  return onion_handler_handle(session, http->error_handler, request, response);
}

size_t
HttpMaxPostSize(const ProtocolHttp *protocol_http_ptr)
{
  return protocol_http_ptr->max_post_size;
}

size_t
HttpMaxFileSize(const ProtocolHttp *protocol_http_ptr)
{
  return protocol_http_ptr->max_file_size;
}

/**
 * @brief Name the request's origin as the one allowed to read the response.
 *
 * The origin is echoed rather than fixed because a response is readable by
 * exactly one origin, and which one is a policy decision the caller owns. A request without an Origin header is not cross-origin and gets no CORS headers.
 *
 * @param response_ptr The response to decorate.
 * @param origin       Value of the request's Origin header, or NULL.
 */
void
WriteAllowedOrigin(onion_response *response_ptr, const char *origin)
{
  if (!origin || !*origin) return;

  onion_response_set_AccessControlAllowOrigin(response_ptr, origin);
  onion_response_set_header(response_ptr, "Vary", "Origin");
}

void
WriteAccessControlAllowMethods(onion_response *response_ptr, const char *allowed_methods)
{
  onion_response_set_AccessControlAllowMethods(response_ptr, allowed_methods);
}

void
WriteAccessControlAllowHeaders(onion_response *response_ptr, const char *allowed_headers)
{
  onion_response_set_AccessControlAllowHeaders(response_ptr, allowed_headers);
}

void
WriteResponseCodeXXX(InstanceHolderForSession *_Nonnull instance_sesn_ptr, HttpSession *_Nonnull http_ptr,
                     unsigned response_code, const char *code_xxx, __unused const char *user_msg)
{
  const char *body    = code_xxx ? code_xxx : "";
  size_t      body_sz = strlen(body);

  onion_response_set_code(HTTPSESN_RESPONSE_PTR(http_ptr), response_code);
  onion_response_set_length(HTTPSESN_RESPONSE_PTR(http_ptr), body_sz);
  onion_response_set_content_type(HTTPSESN_RESPONSE_PTR(http_ptr), "application/json");
  onion_response_write(instance_sesn_ptr, HTTPSESN_RESPONSE_PTR(http_ptr), body, body_sz);
}

void
WriteCORSMandatedHeaders(HttpSession *_Nonnull http_ptr, const char *origin, unsigned response_code)
{
  WriteAllowedOrigin(HTTPSESN_RESPONSE_PTR(http_ptr), origin);
  WriteAccessControlAllowMethods(HTTPSESN_RESPONSE_PTR(http_ptr), "GET, POST, DELETE, OPTIONS");
  WriteAccessControlAllowHeaders(HTTPSESN_RESPONSE_PTR(http_ptr), "Authorization, Content-Type");
  onion_response_set_code(HTTPSESN_RESPONSE_PTR(http_ptr), response_code);
}
