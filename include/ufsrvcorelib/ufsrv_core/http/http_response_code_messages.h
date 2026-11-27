/*

 Copyright (c) 2015-2027 unfacd works

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


#ifndef UFSRV_HTTP_RESPONSE_CODE_MESSAGES_H
#define UFSRV_HTTP_RESPONSE_CODE_MESSAGES_H

#define HTTP_RESPONSE_CODE_OK                   200
#define HTTP_RESPONSE_CODE_NO_CONTENT           204
#define HTTP_RESPONSE_CODE_GENERIC_ERROR        409
#define HTTP_RESPONSE_CODE_BAD_REQUEST          400
#define HTTP_RESPONSE_CODE_INVALID              401
#define HTTP_RESPONSE_CODE_NOT_ALLOWED          403
#define HTTP_RESPONSE_CODE_UNSUPPORTED          405
#define HTTP_RESPONSE_CODE_UNPROCESSABLE_ENTITY 422
#define HTTP_RESPONSE_CODE_INTERNAL_ERROR     500


#define HTTP_RESPONSE_CODE_GENERIC_ERROR_MSG   "Server unable to complete request."
#define HTTP_RESPONSE_CODE_401_MSG             "{\"error\": \"Invalid HTTP method: %s\"}"
#define HTTP_RESPONSE_CODE_401_UNAUTHORIZED_MSG "{\"error\": \"Authentication required\"}"
#define HTTP_RESPONSE_CODE_403_MSG             "{\"error\": \"The origin is not allowed access to this resource.\"}"
#define HTTP_RESPONSE_CODE_405_MSG             "{\"error\": \"Unsupported HTTP method: %s\"}"

#define HTTP_RESPONSE_CODE_400_UNSUPPORTED_RESPONSE_TYPE_MSG "{\"error\":\"unsupported_response_type\"}"
#define HTTP_RESPONSE_CODE_400_MISSING_PARAM_MSG             "{\"error\":\"invalid_request\",\"error_description\":\"missing required parameter\"}"
#define HTTP_RESPONSE_CODE_400_INVALID_CHALLENGE_METHOD_MSG  "{\"error\":\"invalid_request\",\"error_description\":\"invalid code_challenge_method\"}"
#define HTTP_RESPONSE_CODE_400_INVALID_CLIENT_MSG            "{\"error\":\"invalid_client\",\"error_description\":\"client not found or inactive\"}"
#define HTTP_RESPONSE_CODE_400_INVALID_GRANT_MSG             "{\"error\":\"invalid_grant\",\"error_description\":\"redirect_uri not registered\"}"
#define HTTP_RESPONSE_CODE_500_INTERNAL_ERROR_MSG            "{\"error\":\"server_error\"}"

#endif //UFSRV_HTTP_RESPONSE_CODE_MESSAGES_H
