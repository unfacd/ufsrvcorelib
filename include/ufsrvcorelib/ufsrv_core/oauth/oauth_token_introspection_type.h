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


#ifndef UFSRV_OAUTH_TOKEN_INTROSPECTION_TYPE_H
#define UFSRV_OAUTH_TOKEN_INTROSPECTION_TYPE_H

/**
 * @brief What one stored token row carries, as RFC 7662 §2.2 asks for it.
 *
 * Produced by DbIntrospectOAuthToken(): a single lookup that answers whether the
 * presented token exists, whether it is still usable, and everything an
 * introspection response reports about it.  Active is decided in the database —
 * revoked or expired means inactive — so the caller never has to re-derive it.
 */
typedef struct OAuthTokenIntrospection {
    bool          found;        ///< a row matched the presented token
    bool          is_active;    ///< found and neither revoked nor expired
    bool          is_refresh;   ///< came from oauth_refresh_tokens
    bool          is_client_subject; ///< the token acts for the client itself, not a user
    unsigned long client_id;    ///< oauth_clients.client_id the token was issued to
    unsigned long user_id;      ///< resource owner, or the client's registrant as provenance
    char          client_uuid[UUID4_STR_BUFFER_SIZE]; ///< serialised client identifier
    time_t        issued_at;    ///< epoch seconds
    time_t        expires_at;   ///< epoch seconds
    char          scopes[4096]; ///< scopes JSON as granted
    char          jti[256];     ///< JWT ID when the row carries one, else empty
    char          token_type[20]; ///< "Bearer"
    char          subject[64];  ///< subject as presented: ufsrvuid text, or the client's uuid
} OAuthTokenIntrospection;

#endif //UFSRV_OAUTH_TOKEN_INTROSPECTION_TYPE_H
