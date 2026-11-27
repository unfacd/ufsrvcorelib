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

#ifndef UFSRV_OAUTH_TOKEN_PAIR_TYPE_H
#define UFSRV_OAUTH_TOKEN_PAIR_TYPE_H

#include <time.h>

#include <ufsrv_core/oauth/oauth_limits.h>

/**
 * @brief Outcome of an authorization-code exchange.
 *
 * An exchange has a small, finite set of answers.  The type is returned rather
 * than mapped to a transport error, because the two callers speak differently:
 * /token answers with an RFC 6749 §5.2 document, the browser landing answers
 * with a page state.
 */
typedef enum OAuthExchangeStatus {
  OAUTH_EXCHANGE_STATUS_OK = 0,
  OAUTH_EXCHANGE_STATUS_INVALID_GRANT,   ///< unknown, expired, redirected elsewhere, or PKCE failed
  OAUTH_EXCHANGE_STATUS_REPLAY,          ///< already redeemed by a request that holds the verifier
  OAUTH_EXCHANGE_STATUS_INVALID_CLIENT,  ///< the client did not authenticate
  OAUTH_EXCHANGE_STATUS_INVALID_SCOPE,   ///< a requested scope the client is not allowed
  OAUTH_EXCHANGE_STATUS_BACKEND_ERROR    ///< the token store could not be read or written
} OAuthExchangeStatus;

/**
 * @brief The pair an exchange issued, with the grant it belongs to.
 *
 * Fixed-size copies: the caller owns the value outright and nothing points into
 * a closed statement handle.
 */
typedef struct OAuthTokenPair {
  char          access_token[CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_MAX_SZ + 1];
  char          refresh_token[CONFIGDEFAULT_OAUTH_TOKEN_SZ + 1];
  char          scopes[4096];       ///< scopes JSON exactly as granted on the code
  unsigned long client_id;          ///< oauth_clients.client_id
  unsigned long user_id;            ///< the resource owner the pair was issued to
  time_t        access_expiry;      ///< seconds of life granted to the access token
} OAuthTokenPair;

#endif //UFSRV_OAUTH_TOKEN_PAIR_TYPE_H
