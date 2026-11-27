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

#ifndef UFSRV_OAUTH_CLIENT_TYPE_H
#define UFSRV_OAUTH_CLIENT_TYPE_H

#include <ufsrv_core/oauth/oauth_limits.h>
#include <uflib/uuid4_type.h>

 typedef enum EnumGrantTypes {
    AUTHORIZATION_CODE=1,
    REFRESH_TOKEN,
    CLIENT_CREDENTIALS,
    PASSWORD,
    IMPLICIT,
    DEVICE_CODE
} EnumGrantTypes;

typedef enum EnumClientTypes {
    PUBLIC,
    CONFIDENTIAL
} EnumClientTypes;

/** DAO representing an OAUth client */
struct OAuthClient {
  EnumGrantTypes grant_types;
  EnumClientTypes client_type;
  time_t when_created;
  unsigned long user_id; /// system user id
  Uuid4 client_uuid;
  char *allowed_scopes; ///> json array
  char *redirect_uris;  ///> json array
  char *grant_types_json;
  struct {
      char by_value[CONFIGDEFAULT_OAUTH_CLIENT_NAME_MAX_SZ + 1];
      char *by_ref;
  } client_name;
  struct {
      char by_value[CONFIGDEFAULT_OAUTH_CLIENT_SECRET_SZ + 1];
      char *by_ref;
  } client_secret;
};

typedef struct OAuthClient OAuthClient;

#endif //UFSRV_OAUTH_CLIENT_TYPE_H
