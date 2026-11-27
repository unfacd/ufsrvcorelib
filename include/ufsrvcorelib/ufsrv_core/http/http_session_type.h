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

#ifndef SRC_INCLUDE_HTTP_SESSION_TYPE_H_
#define SRC_INCLUDE_HTTP_SESSION_TYPE_H_

#include <ufsrvcorelib/ufsrv_core/http/response.h>
#include <ufsrvcorelib/ufsrv_core/http/protocol_http_type.h>
#include <ufsrvcorelib/ufsrv_core/http/http_session.h>

#define HTTPSESN_REQUEST(x) (*HttpSessionGetRequest(x))
#define HTTPSESN_RESPONSE(x) (*HttpSessionGetResponse(x))
#define HTTPSESN_JSONDATA(x) HttpSessionGetJson(x)
#define HTTPSESN_REQUEST_PTR(x) HttpSessionGetRequest(x)
#define HTTPSESN_RESPONSE_PTR(x) HttpSessionGetResponse(x)

#endif
