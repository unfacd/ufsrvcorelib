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


/**
 * @file utils_oauth.h
 * @brief OAuth client, grant and token operations.
 *
 * Database operations use the calling thread's MariaDB handle and result
 * storage in ThreadContext. The schema must provide accounts, oauth_clients,
 * oauth_authorization_codes, oauth_access_tokens and oauth_refresh_tokens.
 * User access tokens use the serialized accounts.ufsrvuid as their subject.
 */

#ifndef UFSRV_UTILS_OAUTH_H
#define UFSRV_UTILS_OAUTH_H

#include <uflib/uflib_defs.h>
#include <uflib/file_loader_type.h>
#include <uflib/ufsrvuid_type.h>
#include "ufsrv_core/ufsrvresult_type.h"
#include "ufsrv_core/oauth/oauth_client_type.h"
#include "ufsrv_core/oauth/authorization_code_grant_type.h"
#include "ufsrv_core/oauth/oauth_token_introspection_type.h"
#include "ufsrv_core/oauth/oauth_token_pair_type.h"


PUBLIC_API __const size_t ProvideResourceProtectedMetadataBufferSize();
PUBLIC_API const char  *ProvideProtectedResourceMetadata(char *allocated_buffer, const char *server_uri);
PUBLIC_API UFSRVResult *DbOAuthClientAddRedirectUri(unsigned long user_id, const char *redirect_uri);

PUBLIC_API UFSRVResult *DbOAuthClientLookupByUuid(const char *client_uuid_str, OAuthClient *auth_client_provided_ptr, unsigned long *client_id_out);
PUBLIC_API bool OAuthClientValidateRedirectUri(const char *redirect_uris_json, const char *requested_uri);
PUBLIC_API UFSRVResult *DbStoreAuthorizationCode(const char *code, unsigned long client_id, unsigned long user_id, const char *redirect_uri, const char *scopes_json, const char *code_challenge, const char *code_challenge_method, const char *state, const char *nonce, time_t ttl_seconds);
PUBLIC_API char *GenerateAuthorizationCode(char *authorization_buffer_ptr_provided);
PUBLIC_API char *BuildScopesJson(const char *scope_string, char *buffer, size_t buffer_sz);
PUBLIC_API char *DbGetUserEmailByUserId(unsigned long user_id);
PUBLIC_API unsigned long DbGetUserIdByEmail(const char *email);
PUBLIC_API UFSRVResult *DbLookupAuthorizationCode(const char *code, const char *client_uuid, char *redirect_uri_out, size_t redirect_uri_sz, char *state_out, size_t state_sz);
PUBLIC_API UFSRVResult *DbLookupAuthorizationCodeGrant(const char *code, const char *client_uuid, bool include_redeemed, AuthorizationCodeGrant *grant_out);
PUBLIC_API UFSRVResult *DbRevokeTokensForCode(const char *authorization_code);
PUBLIC_API UFSRVResult *DbIntrospectOAuthToken(const char *token, OAuthTokenIntrospection *out);
PUBLIC_API UFSRVResult *DbGetUfsrvuidByUserId(unsigned long user_id, UfsrvUid *uid_out);
PUBLIC_API UFSRVResult *DbRevokeOAuthToken(const char *token, unsigned long client_db_id, bool prefer_refresh);
PUBLIC_API UFSRVResult *DbRedeemAuthorizationCode(const char *code, const char *client_uuid, bool *is_replay_out);
PUBLIC_API OAuthExchangeStatus DbExchangeAuthorizationCode(const char *client_uuid, const char *client_secret,
                                                const char *code, const char *redirect_uri,
                                                const char *code_verifier, OAuthTokenPair *tokens_out);
PUBLIC_API OAuthExchangeStatus OAuthIssueClientCredentialsToken(const char *client_uuid, const char *client_secret,
                                                     const char *requested_scope, OAuthTokenPair *tokens_out);
PUBLIC_API OAuthExchangeStatus DbExchangeRefreshToken(const char *client_uuid, const char *client_secret,
                                           const char *refresh_token, const char *requested_scope,
                                           OAuthTokenPair *tokens_out);
PUBLIC_API bool OAuthVerifyPKCE(const char *code_verifier, const char *code_challenge, const char *code_challenge_method);
PUBLIC_API UFSRVResult *DbOAuthClientAuthenticate(const char *client_uuid, const char *client_secret);
PUBLIC_API char *BuildScopeStringFromJson(const char *scopes_json, char *buffer, size_t buffer_sz);
PUBLIC_API UFSRVResult *DbStoreOAuthTokens(unsigned long client_id, unsigned long user_id, const char *scopes_json, const char *authorization_code, const char *access_token, const char *refresh_token, time_t access_expiry, time_t refresh_expiry);

#endif //UFSRV_UTILS_OAUTH_H
