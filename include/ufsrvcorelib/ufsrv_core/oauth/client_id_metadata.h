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

#ifndef UFSRV_CLIENT_ID_METADATA_H
#define UFSRV_CLIENT_ID_METADATA_H

#include <ufsrv_core/oauth/oauth_limits.h>
#include <stdbool.h>
#include <uflib/uflib_defs.h>
#include <stdint.h>
#include <ufsrv_core/ufsrvresult_type.h>
#include <ufsrvcorelib/ufsrv_core/oauth/oauth_client_type.h>

#define OAUTH_CIMD_REDIRECT_URI_MAX_SZ  CONFIGDEFAULT_OAUTH_CIMD_URL_MAX_SZ
#define OAUTH_CIMD_GRANT_TYPES_MAX_SZ   256
#define OAUTH_CIMD_REDIRECTS_BUF_SZ     ((CONFIGDEFAULT_OAUTH_AUTH_REDIRECTS_MAX * (OAUTH_CIMD_REDIRECT_URI_MAX_SZ + 4)) + 3)

typedef struct OAuthClientMetadata {
  char url[CONFIGDEFAULT_OAUTH_CIMD_URL_MAX_SZ + 1];
  char name[CONFIGDEFAULT_OAUTH_CLIENT_NAME_MAX_SZ + 1];
  char redirect_uris[OAUTH_CIMD_REDIRECTS_BUF_SZ];
  char grant_types[OAUTH_CIMD_GRANT_TYPES_MAX_SZ + 1];
  long cache_ttl;
} OAuthClientMetadata;

PUBLIC_API bool OAuthClientIdentifierIsUrl(const char *identifier);
PUBLIC_API bool OAuthClientMetadataUrlIsValid(const char *url);
PUBLIC_API bool OAuthClientMetadataAddressIsPublic(uint32_t address);
PUBLIC_API bool OAuthClientMetadataDocumentValidate(const char *url, const char *document, OAuthClientMetadata *metadata_out);
PUBLIC_API bool OAuthClientRedirectUriIsDeclared(const char *redirect_uris_json, const char *redirect_uri);
PUBLIC_API bool OAuthClientGrantTypeIsAllowed(const char *grant_types_json, const char *grant_type);
PUBLIC_API UFSRVResult *OAuthClientMetadataRegister(const char *url, const char *document, long cache_ttl, OAuthClient *client_out, unsigned long *client_db_id_out);
PUBLIC_API UFSRVResult *OAuthClientResolveIdentifier(const char *identifier, OAuthClient *client_out, unsigned long *client_db_id_out);

#endif //UFSRV_CLIENT_ID_METADATA_H
