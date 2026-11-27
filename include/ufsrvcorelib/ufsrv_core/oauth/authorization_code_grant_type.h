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

#ifndef UFSRV_AUTHORIZATION_CODE_GRANT_TYPE_H
#define UFSRV_AUTHORIZATION_CODE_GRANT_TYPE_H

/**
 * @brief Everything the token exchange needs from one authorization code row.
 *
 * Produced by DbLookupAuthorizationCodeGrant() in a single statement: the
 * columns DbLookupAuthorizationCode() does not return (scopes, PKCE challenge
 * and method, nonce, user_id, client_id).
 *
 * The buffers are fixed-size copies, not pointers into a statement handle, so
 * the grant stays valid after the statement is closed and the caller owns it
 * outright.  A value longer than its buffer is truncated rather than rejected;
 * that is safe here because the stored values were validated at /authorize
 * time, and the truncation is visible to the caller as a shorter string.
 *
 * OAuth grant lookups return this snapshot to callers.
 */
typedef struct AuthorizationCodeGrant {
    unsigned long client_id;          ///< oauth_clients.client_id, FK for the token rows
    unsigned long user_id;            ///< resource owner, oauth_access_tokens.user_id
    char redirect_uri[2084];          ///< exactly the URI registered on this code (utf8mb4_bin)
    char scopes[4096];                ///< scopes JSON as granted at /authorize
    char code_challenge[256];         ///< PKCE challenge to verify the verifier against
    char code_challenge_method[16];   ///< "S256" or "plain"
    char nonce[256];                  ///< OIDC nonce, for id_token binding (may be empty)
} AuthorizationCodeGrant;

#endif //UFSRV_AUTHORIZATION_CODE_GRANT_TYPE_H
