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

#ifndef UFSRV_ONION_HANDLER_AUTH_PAM_DATA_TYPE_H
#define UFSRV_ONION_HANDLER_AUTH_PAM_DATA_TYPE_H

#include <ufsrvcorelib/ufsrv_core/http/http_request_handler.h>

struct onion_handler_auth_pam_data_t {
    char *realm;
    char *pamname;
    onion_handler *inside;
};

typedef struct onion_handler_auth_pam_data_t  onion_handler_auth_pam_data;

#endif //UFSRV_ONION_HANDLER_AUTH_PAM_DATA_TYPE_H
