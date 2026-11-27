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


#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <stdio.h>
#include "ufsrv_core/oauth/utils_oauth.h"
#include "uflib/utils.h"
#include "ufsrv_core/http/http_request.h"
#include <ufsrv_core/system/utils_json.h>
#include "uflib/utils_crypto.h"
#include "uflib/utils_urls.h"
#include <openssl/sha.h>
#include <uflib/utils_file_loader.h>
#include <ufsrv_core/oauth/oauth_client_type.h>
#include <ufsrv_core/delegator_sessions/thread_context_type.h>
#include <mariadb/mysql.h>
#include <uflib/db/db_sql.h>
#include <uflib/mjson/mjson.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
#include <ufsrv_core/jwt/jwt.h>
#include <uflib/ufsrvuid.h>
#include <pthread.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_secret.h>

extern __thread ThreadContext ufsrv_thread_context;

void
LoadOAuthAutoDiscoveryFile(FileLoader *file_loader_ptr) {
  enum FileLoaderResult file_loader_result = FileLoaderLoadIfModified(file_loader_ptr, IS_HASHED_FALSE);

  if (file_loader_result == FL_SUCCESS) {

  } else if (file_loader_result == FL_SUCCESS_EMPTY_FILE) {

  } else {
    //error
  }
}

/**
 * A convenient function to construct a fully formed metadata for Protected Resource Metadata Request. Typically used with the header "WWW-Authenticate".
 * Example of a fully header metadata: "Bearer resource_metadata=\"https://resource.example.com/.well-known/oauth-protected-resource\"".
 *
 * @param server_uri[IN] Name of resource server to embed. This can include protocol portion 'http://', port portion ':12345' and no trailing '/'
 * @param allocated_buffer[INOUT] user allocated buffer holdind fully constructed PRM. use ProvideResourceProtectedMetadataBufferSize() to determine necessary buffer size
 * @return fully constricted PRM ready for inclusion as header paramater.
 */
const char  *
ProvideProtectedResourceMetadata(char *allocated_buffer, const char *server_uri) {
  snprintf(allocated_buffer, ProvideResourceProtectedMetadataBufferSize(), "Bearer resource_metadata=\"%s/.well-known/oauth-protected-resource\"", server_uri);

  return allocated_buffer;
}

/**
 * Convenient function that returns max buffer size for constructing allocation buffers holding ResourceProtectedMetadataBuffer uri.
 * @return byte-size of allocation buffer.
 */
__const size_t
ProvideResourceProtectedMetadataBufferSize() {
  return sizeof("Bearer resource_metadata=\"\"://") + sizeof("65535") + sizeof(":") + 253 + 10 + 1; //1 for terminating '\0'
}

/**
 *
 * @param user_id
 * @param redirect_uri
 * @return
 * UPDATE `oauth_clients` SET `redirect_uris` = JSON_ARRAY_APPEND(`redirect_uris`, '$', 'http://localhost') WHERE `user_id` = 0;
 */
UFSRVResult *
DbOAuthClientAddRedirectUri(unsigned long user_id, const char *redirect_uri)
{
#define SQL_UPDATE_APPEND_CLIENT_REDIRECT_URI  "UPDATE `oauth_clients` SET `redirect_uris` = JSON_ARRAY_APPEND(`redirect_uris`, '$', '%s') WHERE `user_id` = %lu"

  char *sql_query_str = mdsprintf(SQL_UPDATE_APPEND_CLIENT_REDIRECT_URI, redirect_uri, user_id);

#ifdef __UF_TESTING
  syslog(LOG_DEBUG, "%s (pid:'%lu'): GENERATED SQL QUERY: '%s'", __func__, pthread_self(), sql_query_str);
#endif

  int sql_result = h_query_update(THREAD_CONTEXT_DB_BACKEND, sql_query_str);

  if (sql_result != H_OK) {
    syslog(LOG_DEBUG, "%s (pid:'%lu', th_ctx:'%p'): ERROR: COULD NOT EXECUTE UPDATE STATEMENT: '%s'", __func__, pthread_self(), THREAD_CONTEXT_PTR, sql_query_str);
    free(sql_query_str);

    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_COMMAND)
  }

  free(sql_query_str);

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESULT_TYPE_NOOP);

#undef SQL_UPDATE_APPEND_CLIENT_REDIRECT_URI
}

/**
 * @brief Generate a cryptographically random hex-encoded authorization code.
 * @param authorization_buffer_ptr_provided[INOUT] optionally, pass a pre-allocated buffer to collect results.
 * Must be of size CONFIGDEFAULT_OAUTH_AUTH_CODE_SZ + 1 and zderoed out.
 * @return string of CONFIGDEFAULT_OAUTH_AUTH_CODE_SZ hex chars.
 * @dynamic_memory OUT TRANSFERS new string which the caller must free
 */
char *
GenerateAuthorizationCode(char *authorization_buffer_ptr_provided)
{
  return GenerateSecureRandomHexed(authorization_buffer_ptr_provided, CONFIGDEFAULT_OAUTH_AUTH_CODE_SZ);
}

/**
 * @brief Build a JSON array string from space-separated scope values.
 *
 * Converts "openid profile email" → "[\"openid\",\"profile\",\"email\"]".
 * Always writes at least "[]".  Truncates gracefully when the buffer is
 * too small — partial scope tokens are never emitted.
 *
 * @param scope_string[IN] space-separated scopes, may be NULL or empty
 * @param buffer[OUT] caller-allocated buffer, always null-terminated
 * @param buffer_sz[IN] size of buffer in bytes
 * @return buffer (for inline use)
 */
char *
BuildScopesJson(const char *scope_string, char *buffer, size_t buffer_sz)
{
  buffer[0] = '[';
  buffer[1] = ']';
  buffer[2] = '\0';

  if (!IS_STR_LOADED(scope_string)) return buffer;

  int pos = 1; /* skip opening '[' */
  const char *s = scope_string;
  const char *start;

  while (*s) {
    while (*s == ' ') s++;
    if (!*s) break;
    start = s;
    while (*s && *s != ' ') s++;
    int len = s - start;
    /* 5 = closing '"' + optional ',' + opening '"' + closing ']' + '\0' */
    if (pos + len + 5 >= (int)buffer_sz) break;
    if (pos > 1) buffer[pos++] = ',';
    buffer[pos++] = '"';
    memcpy(buffer + pos, start, len);
    pos += len;
    buffer[pos++] = '"';
  }
  buffer[pos++] = ']';
  buffer[pos] = '\0';

  return buffer;
}

/**
 * @brief Look up an active OAuth client by its UUID string, populating the
 *        provided OAuthClient struct and returning the database PK via
 *        client_id_out.  The redirect_uris field is read as a raw JSON array
 *        string so the caller can validate URIs in memory with
 *        OAuthClientValidateRedirectUri().
 *
 * @param client_uuid_str[IN] hyphenated UUID v4 string (36 chars)
 * @param auth_client_provided_ptr[OUT] populated OAuthClient on success (caller owns by_ref members)
 * @param client_id_out[OUT] database auto-increment PK for FK references. This is not part of the OAuthClient domain
 * @return UFSRVResult success with RESCODE_BACKEND_DATA on found, error otherwise
 */
UFSRVResult *
DbOAuthClientLookupByUuid(const char *client_uuid_str, OAuthClient *auth_client_provided_ptr, unsigned long *client_id_out)
{
#define SQL_SELECT_CLIENT_BY_UUID \
  "SELECT client_id, user_id, name, redirect_uris, is_active FROM oauth_clients WHERE uuid_serialised = ? AND is_active = 1"
  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_CLIENT_BY_UUID, sizeof(SQL_SELECT_CLIENT_BY_UUID) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  /* --- bind parameter: uuid_serialised (string) --- */
  unsigned long uuid_len = strlen(client_uuid_str);
  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[0].buffer        = (char *)client_uuid_str;
  bind_param[0].buffer_length = uuid_len;
  bind_param[0].length        = &uuid_len;

  if (mysql_stmt_bind_param(stmt, bind_param)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_param() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  if (mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: mysql_stmt_execute() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  /* --- bind result columns --- */
  unsigned long db_client_id  = 0;
  unsigned long db_user_id    = 0;
  char          db_name[CONFIGDEFAULT_OAUTH_CLIENT_NAME_MAX_SZ + 1] = {0};
  char          db_redirects[(CONFIGDEFAULT_OAUTH_AUTH_REDIRECTS_MAX * CONFIGDEFAULT_OAUTH_CIMD_URL_MAX_SZ) + 1] = {0};
  my_bool       db_is_active  = 0;
  unsigned long name_len      = 0;
  unsigned long redirects_len = 0;
  my_bool       is_null_name      = 0;
  my_bool       is_null_redirects = 0;
  my_bool       is_null_active    = 0;

  memset(db_name, 0, sizeof(db_name));
  memset(db_redirects, 0, sizeof(db_redirects));

  MYSQL_BIND bind_result[5];
  memset(bind_result, 0, sizeof(bind_result));

  bind_result[0].buffer_type   = MYSQL_TYPE_LONG;
  bind_result[0].buffer        = &db_client_id;

  bind_result[1].buffer_type   = MYSQL_TYPE_LONG;
  bind_result[1].buffer        = &db_user_id;

  bind_result[2].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[2].buffer        = db_name;
  bind_result[2].buffer_length = sizeof(db_name) - 1;
  bind_result[2].length        = &name_len;
  bind_result[2].is_null       = &is_null_name;

  bind_result[3].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[3].buffer        = db_redirects;
  bind_result[3].buffer_length = sizeof(db_redirects) - 1;
  bind_result[3].length        = &redirects_len;
  bind_result[3].is_null       = &is_null_redirects;

  bind_result[4].buffer_type   = MYSQL_TYPE_TINY;
  bind_result[4].buffer        = &db_is_active;
  bind_result[4].is_null       = &is_null_active;

  if (mysql_stmt_bind_result(stmt, bind_result)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_result() failed: %s", __func__, mysql_stmt_error(stmt));
    goto error_statement;
  }

  if (mysql_stmt_store_result(stmt)) {
    syslog(LOG_ERR, "%s: mysql_stmt_store_result() failed: %s", __func__, mysql_stmt_error(stmt));
    goto error_statement;
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  if (fetch_status == MYSQL_NO_DATA) {
    syslog(LOG_DEBUG, "%s: no active client found for uuid '%s'", __func__, client_uuid_str);
    goto error_statement;
  }
  if (fetch_status != 0) {
    syslog(LOG_ERR, "%s: mysql_stmt_fetch() failed: %s", __func__, mysql_stmt_error(stmt));
    goto error_statement;
  }

  mysql_stmt_close(stmt);

  *client_id_out = db_client_id;

  if (IS_PRESENT(auth_client_provided_ptr)) {
    auth_client_provided_ptr->user_id             = db_user_id;
    auth_client_provided_ptr->client_name.by_ref  = strdup(is_null_name ? "" : db_name);
    auth_client_provided_ptr->redirect_uris       = strdup(is_null_redirects ? "[]" : db_redirects);
  }

#ifdef __UF_TESTING
  syslog(LOG_DEBUG, "%s (pid:'%lu'): found client_id='%lu' for uuid '%s'", __func__, pthread_self(), db_client_id, client_uuid_str);
#endif

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

  error_statement:
  mysql_stmt_close(stmt);
  THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)

#undef SQL_SELECT_CLIENT_BY_UUID
}

UFSRVResult *
DbGetUfsrvuidByUserId(unsigned long user_id, UfsrvUid *uid_out)
{
#define SQL_SELECT_USER_UFSRVUID \
  "SELECT ufsrvuid FROM accounts WHERE id = ?"

  if (IS_EMPTY(uid_out)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_PROG_NULL_POINTER)
  }

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_USER_UFSRVUID, sizeof(SQL_SELECT_USER_UFSRVUID) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type = MYSQL_TYPE_LONG;
  bind_param[0].buffer      = &user_id;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  uint8_t       raw[CONFIG_MAX_UFSRV_ID_SZ] = {0};
  unsigned long raw_len                     = 0;
  my_bool       is_null                     = 0;

  MYSQL_BIND bind_result[1];
  memset(bind_result, 0, sizeof(bind_result));
  bind_result[0].buffer_type   = MYSQL_TYPE_BLOB;
  bind_result[0].buffer        = raw;
  bind_result[0].buffer_length = sizeof raw;
  bind_result[0].length        = &raw_len;
  bind_result[0].is_null       = &is_null;

  if (mysql_stmt_bind_result(stmt, bind_result) || mysql_stmt_store_result(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  mysql_stmt_close(stmt);

  if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  if (is_null || raw_len != CONFIG_MAX_UFSRV_ID_SZ) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  memcpy(uid_out->data, raw, CONFIG_MAX_UFSRV_ID_SZ);

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(uid_out, RESCODE_BACKEND_DATA)

#undef SQL_SELECT_USER_UFSRVUID
}

/**
 * @brief Retrieve the email address for a given user account.
 * @param user_id[IN] account id (accounts.id)
 * @return heap-allocated email string on success, NULL if not found
 * @dynamic_memory EXPORTS returned string which the caller must free
 */
char *
DbGetUserEmailByUserId(unsigned long user_id)
{
#define SQL_SELECT_USER_EMAIL \
  "SELECT number FROM accounts WHERE id = ?"

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_USER_EMAIL, sizeof(SQL_SELECT_USER_EMAIL) - 1));

  if (IS_EMPTY(stmt)) return NULL;

  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type = MYSQL_TYPE_LONG;
  bind_param[0].buffer      = &user_id;

  if (mysql_stmt_bind_param(stmt, bind_param)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_param() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return NULL;
  }

  if (mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: mysql_stmt_execute() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return NULL;
  }

  char  email_buf[320] = {0};
  unsigned long email_len = 0;
  my_bool is_null = 0;

  MYSQL_BIND bind_result[1];
  memset(bind_result, 0, sizeof(bind_result));
  bind_result[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[0].buffer        = email_buf;
  bind_result[0].buffer_length = sizeof(email_buf) - 1;
  bind_result[0].length        = &email_len;
  bind_result[0].is_null       = &is_null;

  if (mysql_stmt_bind_result(stmt, bind_result)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_result() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return NULL;
  }

  if (mysql_stmt_store_result(stmt)) {
    mysql_stmt_close(stmt);
    return NULL;
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  mysql_stmt_close(stmt);

  if (fetch_status != 0 || is_null || email_len == 0) return NULL;

  return strdup(email_buf);

#undef SQL_SELECT_USER_EMAIL
}

/**
 * @brief Resolve the account an e-mail address belongs to.
 *
 * accounts.number is the address the account was registered under, and the
 * mailbox is the only proof of ownership this flow accepts: the answer decides
 * whose account a sign-in link may be minted for, so the address is matched as a
 * bound parameter against the stored value rather than rendered into the SQL.
 *
 * @param email[IN] address as supplied by the client
 * @return accounts.id, or 0 when no account carries that address
 */
unsigned long
DbGetUserIdByEmail(const char *email)
{
#define SQL_SELECT_USER_ID_BY_EMAIL "SELECT id FROM accounts WHERE number = ?"

  if (!IS_STR_LOADED(email)) return 0;

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_USER_ID_BY_EMAIL, sizeof(SQL_SELECT_USER_ID_BY_EMAIL) - 1));

  if (IS_EMPTY(stmt)) return 0;

  unsigned long email_len = strlen(email);

  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[0].buffer        = (char *)email;
  bind_param[0].buffer_length = email_len;
  bind_param[0].length        = &email_len;

  if (mysql_stmt_bind_param(stmt, bind_param)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_param() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return 0;
  }

  if (mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: mysql_stmt_execute() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return 0;
  }

  unsigned long user_id = 0;
  my_bool       is_null = 0;

  MYSQL_BIND bind_result[1];
  memset(bind_result, 0, sizeof(bind_result));
  bind_result[0].buffer_type   = MYSQL_TYPE_LONG;
  bind_result[0].buffer        = &user_id;
  bind_result[0].is_null       = &is_null;

  if (mysql_stmt_bind_result(stmt, bind_result)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_result() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    return 0;
  }

  if (mysql_stmt_store_result(stmt)) {
    mysql_stmt_close(stmt);
    return 0;
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  mysql_stmt_close(stmt);

  if (fetch_status != 0 || is_null) return 0;

  return user_id;

#undef SQL_SELECT_USER_ID_BY_EMAIL
}

/**
 * @brief Look up a stored authorization code and return its associated data.
 * @param code[IN] the authorization code
 * @param client_uuid[IN] database PK of the client
 * @param redirect_uri_out[OUT] buffer for redirect_uri (caller-allocated)
 * @param redirect_uri_sz[IN] size of redirect_uri_out buffer
 * @param state_out[OUT] buffer for state (caller-allocated, may be empty)
 * @param state_sz[IN] size of state_out buffer
 * @return UFSRVResult
 */
UFSRVResult *
DbLookupAuthorizationCode(const char *code, const char *client_uuid, char *redirect_uri_out, size_t redirect_uri_sz, char *state_out, size_t state_sz)
{
#define SQL_SELECT_AUTH_CODE \
  "SELECT ac.redirect_uri, ac.state " \
  "FROM oauth_authorization_codes ac " \
  "INNER JOIN oauth_clients oc ON ac.client_id = oc.client_id " \
  "WHERE ac.code = ? AND oc.uuid_serialised = ? AND ac.expires_at > NOW() " \
  "LIMIT 1"

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_AUTH_CODE, sizeof(SQL_SELECT_AUTH_CODE) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long code_len       = strlen(code);
  unsigned long uuid_len       = strlen(client_uuid);

  MYSQL_BIND bind_params[2];
  memset(bind_params, 0, sizeof(bind_params));
  bind_params[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[0].buffer        = (char *)code;
  bind_params[0].buffer_length = code_len;
  bind_params[0].length        = &code_len;

  bind_params[1].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[1].buffer        = (char *)client_uuid;
  bind_params[1].buffer_length = uuid_len;
  bind_params[1].length        = &uuid_len;

  if (mysql_stmt_bind_param(stmt, bind_params)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_param() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  if (mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: mysql_stmt_execute() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long db_redirect_len = 0;
  unsigned long db_state_len    = 0;
  my_bool is_null_redirect = 0;
  my_bool is_null_state    = 0;

  MYSQL_BIND bind_result[2];
  memset(bind_result, 0, sizeof(bind_result));

  bind_result[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[0].buffer        = redirect_uri_out;
  bind_result[0].buffer_length = redirect_uri_sz - 1;
  bind_result[0].length        = &db_redirect_len;
  bind_result[0].is_null       = &is_null_redirect;

  bind_result[1].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[1].buffer        = state_out;
  bind_result[1].buffer_length = state_sz - 1;
  bind_result[1].length        = &db_state_len;
  bind_result[1].is_null       = &is_null_state;

  if (mysql_stmt_bind_result(stmt, bind_result)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_result() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  if (mysql_stmt_store_result(stmt)) {
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  mysql_stmt_close(stmt);

  if (fetch_status == MYSQL_NO_DATA || fetch_status != 0) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  redirect_uri_out[db_redirect_len] = '\0';
  state_out[db_state_len]           = '\0';

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_SELECT_AUTH_CODE
}

static bool
sLoopbackHostIs(const char *host, size_t host_len)
{
  if (host_len == 9 && strncmp(host, "127.0.0.1", 9) == 0) return true;
  if (host_len == 5 && strncmp(host, "[::1]", 5) == 0) return true;

  return false;
}

static size_t
sAuthorityHostLen(const char *authority, size_t authority_len)
{
  if (authority_len == 0) return 0;

  if (authority[0] == '[') {
    size_t len = 1;
    while (len < authority_len && authority[len] != ']') len++;
    return len < authority_len ? len + 1 : authority_len;
  }

  size_t len = 0;
  while (len < authority_len && authority[len] != ':' && authority[len] != '/') len++;

  return len;
}

static void
sSkipAuthorityPort(const char **rest, size_t *rest_len)
{
  if (*rest_len == 0 || **rest != ':') return;

  size_t consumed = 0;
  while (consumed < *rest_len && (*rest)[consumed] != '/' && (*rest)[consumed] != '?') consumed++;

  *rest     += consumed;
  *rest_len -= consumed;
}

static bool
sLoopbackRedirectUrisMatch(const char *registered, size_t registered_len, const char *requested)
{
  static const char scheme[] = "http://";
  static const size_t scheme_len = sizeof(scheme) - 1;

  if (!IS_STR_LOADED(requested)) return false;
  if (registered_len <= scheme_len) return false;
  if (strncasecmp(registered, scheme, scheme_len) != 0) return false;
  if (strncasecmp(requested, scheme, scheme_len) != 0) return false;

  const char *reg_authority = registered + scheme_len;
  size_t      reg_authority_len = registered_len - scheme_len;
  const char *req_authority = requested + scheme_len;
  size_t      req_authority_len = strlen(requested) - scheme_len;

  size_t reg_host_len = sAuthorityHostLen(reg_authority, reg_authority_len);
  size_t req_host_len = sAuthorityHostLen(req_authority, req_authority_len);

  if (!sLoopbackHostIs(reg_authority, reg_host_len)) return false;
  if (reg_host_len != req_host_len || strncasecmp(reg_authority, req_authority, req_host_len) != 0) return false;

  const char *reg_rest = reg_authority + reg_host_len;
  size_t      reg_rest_len = reg_authority_len - reg_host_len;
  const char *req_rest = req_authority + req_host_len;
  size_t      req_rest_len = req_authority_len - req_host_len;

  sSkipAuthorityPort(&reg_rest, &reg_rest_len);
  sSkipAuthorityPort(&req_rest, &req_rest_len);

  return reg_rest_len == req_rest_len && strncmp(reg_rest, req_rest, reg_rest_len) == 0;
}

/**
 * @brief Whether a requested redirect URI is one the client registered.
 *
 * RFC 8252 §7.3: a loopback redirect URI carries whatever ephemeral port the
 * client bound, so for http://127.0.0.1 and http://[::1] the port is ignored on
 * both sides; every other URI is matched literally.
 *
 * @param redirect_uris_json[IN] client's registered URIs, as a JSON array
 * @param requested_uri[IN] the URI to validate
 * @return true if the URI is present in the array, false otherwise
 */
bool
OAuthClientValidateRedirectUri(const char *redirect_uris_json, const char *requested_uri)
{
  if (!IS_STR_LOADED(redirect_uris_json)) return false;

  size_t json_len = strlen(redirect_uris_json);
  const char *buf = redirect_uris_json;
  int offset = 0;
  int key_offset, key_len, val_offset, val_len, val_type;

  while ((offset = mjson_next(buf, json_len, offset, &key_offset, &key_len, &val_offset, &val_len, &val_type)) > 0) {
    if (val_type == MJSON_TOK_STRING && val_len >= 2) {
      /* mjson includes quotes in the offset/length; skip them */
      int content_off = val_offset + 1;
      int content_len = val_len - 2;
      if (content_len == (int)strlen(requested_uri) &&
          strncmp(buf + content_off, requested_uri, content_len) == 0) {
        return true;
      }
      if (sLoopbackRedirectUrisMatch(buf + content_off, (size_t)content_len, requested_uri)) {
        return true;
      }
    }
  }

  return false;
}

/**
 * @brief Redeem an authorization code exactly once — RFC 6749 §4.1.2.
 *
 * Validity and consumption are one statement: the UPDATE carries
 * `used_at IS NULL` and `expires_at > NOW()` in its predicate, so InnoDB's row
 * lock serialises concurrent exchanges and only the first sees one affected
 * row; every later caller sees zero.
 *
 * @param code[IN] authorization code presented by the client
 * @param client_uuid[IN] hyphenated UUID of the client presenting it
 * @param is_replay_out[OUT] optional; true when the code exists but was already
 *                           redeemed.  A replay is not the same as an unknown
 *                           code: RFC 6749 §4.1.2 says the tokens issued from
 *                           that code SHOULD be revoked, which a caller can
 *                           only do if it is told the difference.
 * @return RESCODE_BACKEND_DATA         this call consumed the code
 *         RESCODE_BACKEND_DATA_EMPTYSET unknown, expired or already redeemed
 *         RESCODE_BACKEND_CONNECTION    statement failure
 */
UFSRVResult *
DbRedeemAuthorizationCode(const char *code, const char *client_uuid, bool *is_replay_out)
{
#define SQL_REDEEM_AUTH_CODE \
  "UPDATE oauth_authorization_codes ac " \
  "INNER JOIN oauth_clients oc ON ac.client_id = oc.client_id " \
  "SET ac.used_at = NOW() " \
  "WHERE ac.code = ? AND oc.uuid_serialised = ? " \
  "AND ac.used_at IS NULL AND ac.expires_at > NOW()"

#define SQL_SELECT_AUTH_CODE_REDEEMED \
  "SELECT ac.used_at IS NOT NULL FROM oauth_authorization_codes ac " \
  "INNER JOIN oauth_clients oc ON ac.client_id = oc.client_id " \
  "WHERE ac.code = ? AND oc.uuid_serialised = ? " \
  "LIMIT 1"

  if (IS_PRESENT(is_replay_out)) *is_replay_out = false;

  if (!IS_STR_LOADED(code) || !IS_STR_LOADED(client_uuid)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  unsigned long code_len = strlen(code);
  unsigned long uuid_len = strlen(client_uuid);

  MYSQL_BIND bind_params[2];
  memset(bind_params, 0, sizeof(bind_params));
  bind_params[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[0].buffer        = (char *)code;
  bind_params[0].buffer_length = code_len;
  bind_params[0].length        = &code_len;
  bind_params[1].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[1].buffer        = (char *)client_uuid;
  bind_params[1].buffer_length = uuid_len;
  bind_params[1].length        = &uuid_len;

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_REDEEM_AUTH_CODE, sizeof(SQL_REDEEM_AUTH_CODE) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  if (mysql_stmt_bind_param(stmt, bind_params) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: redemption failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);
  mysql_stmt_close(stmt);

  if (affected_rows == 1) {
    THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)
  }

  /* Zero rows: unknown, expired, another client's, or already redeemed.
     Classify the last case so the caller can revoke what it issued. */
  stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_AUTH_CODE_REDEEMED, sizeof(SQL_SELECT_AUTH_CODE_REDEEMED) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  if (mysql_stmt_bind_param(stmt, bind_params) || mysql_stmt_execute(stmt)) {
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  my_bool is_redeemed = 0;
  my_bool is_null     = 0;

  MYSQL_BIND bind_result[1];
  memset(bind_result, 0, sizeof(bind_result));
  bind_result[0].buffer_type   = MYSQL_TYPE_TINY;
  bind_result[0].buffer        = &is_redeemed;
  bind_result[0].buffer_length = sizeof(is_redeemed);
  bind_result[0].is_null       = &is_null;

  if (mysql_stmt_bind_result(stmt, bind_result) == 0 && mysql_stmt_store_result(stmt) == 0) {
    if (mysql_stmt_fetch(stmt) == 0 && !is_null && is_redeemed && IS_PRESENT(is_replay_out)) {
      *is_replay_out = true;
      syslog(LOG_WARNING, "%s: authorization code replay detected for client '%s'", __func__, client_uuid);
    }
  }

  mysql_stmt_close(stmt);

  THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)

#undef SQL_REDEEM_AUTH_CODE
#undef SQL_SELECT_AUTH_CODE_REDEEMED
}

/**
 * @brief Read every column the token exchange needs from one authorization code.
 *
 * DbLookupAuthorizationCode() returns only redirect_uri and state — all the
 * identity-verification redirect needs.  The exchange additionally needs the
 * PKCE challenge, the granted scopes, the nonce and the owning user and client,
 * so they are read here in one statement instead of re-querying after the code
 * has already been consumed.
 *
 * The read is restricted to unused, unexpired codes, but it does not consume
 * anything: DbRedeemAuthorizationCode() remains the gate.  Do the read first,
 * verify PKCE and scopes against what it returns, then redeem, and only then
 * mint — a crash between the read and the redemption costs nothing, whereas a
 * crash between redemption and minting burns the grant.
 *
 * @param code[IN] authorization code presented by the client
 * @param client_uuid[IN] hyphenated UUID of the client presenting it
 * @param grant_out[OUT] zeroed and populated on success
 * @return RESCODE_BACKEND_DATA on success, EMPTYSET when no unused unexpired
 *         code matches this client, RESCODE_BACKEND_CONNECTION on failure
 */
UFSRVResult *
DbLookupAuthorizationCodeGrant(const char *code, const char *client_uuid, bool include_redeemed, AuthorizationCodeGrant *grant_out)
{
#define SQL_SELECT_AUTH_CODE_GRANT_ANY \
  "SELECT ac.client_id, ac.user_id, ac.redirect_uri, ac.scopes, " \
  "ac.code_challenge, ac.code_challenge_method, ac.nonce " \
  "FROM oauth_authorization_codes ac " \
  "INNER JOIN oauth_clients oc ON ac.client_id = oc.client_id " \
  "WHERE ac.code = ? AND oc.uuid_serialised = ? "

#define SQL_SELECT_AUTH_CODE_GRANT_UNUSED \
  SQL_SELECT_AUTH_CODE_GRANT_ANY "AND ac.used_at IS NULL AND ac.expires_at > NOW() LIMIT 1"

#define SQL_SELECT_AUTH_CODE_GRANT_REDEEMED \
  SQL_SELECT_AUTH_CODE_GRANT_ANY "AND ac.used_at IS NOT NULL LIMIT 1"

  if (IS_EMPTY(grant_out) || !IS_STR_LOADED(code) || !IS_STR_LOADED(client_uuid)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  memset(grant_out, 0, sizeof(*grant_out));

  const char *sql    = include_redeemed ? SQL_SELECT_AUTH_CODE_GRANT_REDEEMED : SQL_SELECT_AUTH_CODE_GRANT_UNUSED;
  size_t      sql_sz = include_redeemed ? sizeof(SQL_SELECT_AUTH_CODE_GRANT_REDEEMED) - 1 : sizeof(SQL_SELECT_AUTH_CODE_GRANT_UNUSED) - 1;

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, sql, sql_sz));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long code_len = strlen(code);
  unsigned long uuid_len = strlen(client_uuid);

  MYSQL_BIND bind_params[2];
  memset(bind_params, 0, sizeof(bind_params));
  bind_params[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[0].buffer        = (char *)code;
  bind_params[0].buffer_length = code_len;
  bind_params[0].length        = &code_len;
  bind_params[1].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[1].buffer        = (char *)client_uuid;
  bind_params[1].buffer_length = uuid_len;
  bind_params[1].length        = &uuid_len;

  if (mysql_stmt_bind_param(stmt, bind_params) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: lookup failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long redirect_len  = 0;
  unsigned long scopes_len    = 0;
  unsigned long challenge_len = 0;
  unsigned long method_len    = 0;
  unsigned long nonce_len     = 0;

  my_bool is_null_redirect  = 0;
  my_bool is_null_scopes    = 0;
  my_bool is_null_challenge = 0;
  my_bool is_null_method    = 0;
  my_bool is_null_nonce     = 0;

  MYSQL_BIND bind_result[7];
  memset(bind_result, 0, sizeof(bind_result));

  bind_result[0].buffer_type = MYSQL_TYPE_LONG;
  bind_result[0].buffer      = &grant_out->client_id;

  bind_result[1].buffer_type = MYSQL_TYPE_LONG;
  bind_result[1].buffer      = &grant_out->user_id;

  bind_result[2].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[2].buffer        = grant_out->redirect_uri;
  bind_result[2].buffer_length = sizeof(grant_out->redirect_uri) - 1;
  bind_result[2].length        = &redirect_len;
  bind_result[2].is_null       = &is_null_redirect;

  bind_result[3].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[3].buffer        = grant_out->scopes;
  bind_result[3].buffer_length = sizeof(grant_out->scopes) - 1;
  bind_result[3].length        = &scopes_len;
  bind_result[3].is_null       = &is_null_scopes;

  bind_result[4].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[4].buffer        = grant_out->code_challenge;
  bind_result[4].buffer_length = sizeof(grant_out->code_challenge) - 1;
  bind_result[4].length        = &challenge_len;
  bind_result[4].is_null       = &is_null_challenge;

  bind_result[5].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[5].buffer        = grant_out->code_challenge_method;
  bind_result[5].buffer_length = sizeof(grant_out->code_challenge_method) - 1;
  bind_result[5].length        = &method_len;
  bind_result[5].is_null       = &is_null_method;

  bind_result[6].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[6].buffer        = grant_out->nonce;
  bind_result[6].buffer_length = sizeof(grant_out->nonce) - 1;
  bind_result[6].length        = &nonce_len;
  bind_result[6].is_null       = &is_null_nonce;

  if (mysql_stmt_bind_result(stmt, bind_result)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_result() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  if (mysql_stmt_store_result(stmt)) {
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  mysql_stmt_close(stmt);

  /* MYSQL_DATA_TRUNCATED still fills the buffers; only a genuine miss aborts. */
  if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  if (is_null_redirect)  grant_out->redirect_uri[0]  = '\0';
  if (is_null_scopes)    grant_out->scopes[0]        = '\0';
  if (is_null_challenge) grant_out->code_challenge[0] = '\0';
  if (is_null_method)    grant_out->code_challenge_method[0] = '\0';
  if (is_null_nonce)     grant_out->nonce[0]         = '\0';

  grant_out->redirect_uri[sizeof(grant_out->redirect_uri) - 1]                   = '\0';
  grant_out->scopes[sizeof(grant_out->scopes) - 1]                               = '\0';
  grant_out->code_challenge[sizeof(grant_out->code_challenge) - 1]               = '\0';
  grant_out->code_challenge_method[sizeof(grant_out->code_challenge_method) - 1] = '\0';
  grant_out->nonce[sizeof(grant_out->nonce) - 1]                                 = '\0';

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_SELECT_AUTH_CODE_GRANT_ANY
#undef SQL_SELECT_AUTH_CODE_GRANT_UNUSED
#undef SQL_SELECT_AUTH_CODE_GRANT_REDEEMED
}

/**
 * @brief Store an authorization code in the oauth_authorization_codes table.
 *
 * Uses MariaDB prepared statements.
 *
 * @param code[IN] the generated authorization code
 * @param client_id[IN] database PK of the client (FK)
 * @param user_id[IN] resource owner's user id
 * @param redirect_uri[IN] the validated redirect URI
 * @param scopes_json[IN] JSON array of scopes (may be "[]")
 * @param code_challenge[IN] PKCE code challenge
 * @param code_challenge_method[IN] "S256" or "plain"
 * @param state[IN] anti-CSRF state parameter (may be NULL)
 * @param nonce[IN] OpenID Connect nonce (may be NULL)
 * @param ttl_seconds[IN] code lifetime in seconds
 * @return UFSRVResult
 */
UFSRVResult *
DbStoreAuthorizationCode(const char *code, unsigned long client_id, unsigned long user_id,
                         const char *redirect_uri, const char *scopes_json,
                         const char *code_challenge, const char *code_challenge_method,
                         const char *state, const char *nonce, time_t ttl_seconds)
{
#define SQL_INSERT_AUTH_CODE \
  "INSERT INTO oauth_authorization_codes " \
  "(code, client_id, user_id, redirect_uri, scopes, code_challenge, " \
  "code_challenge_method, state, nonce, expires_at) " \
  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NOW() + INTERVAL ? SECOND)"

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_INSERT_AUTH_CODE, sizeof(SQL_INSERT_AUTH_CODE) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  /* --- bind parameters --- */
  unsigned long code_len            = strlen(code);
  unsigned long redirect_len        = strlen(redirect_uri);
  unsigned long scopes_len          = strlen(scopes_json);
  unsigned long challenge_len       = strlen(code_challenge);
  unsigned long challenge_meth_len  = strlen(code_challenge_method);
  unsigned long state_len           = state ? strlen(state) : 0;
  unsigned long nonce_len           = nonce ? strlen(nonce) : 0;

  MYSQL_BIND bind_params[10];
  memset(bind_params, 0, sizeof(bind_params));

  bind_params[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[0].buffer        = (char *)code;
  bind_params[0].buffer_length = code_len;
  bind_params[0].length        = &code_len;

  bind_params[1].buffer_type   = MYSQL_TYPE_LONG;
  bind_params[1].buffer        = &client_id;

  bind_params[2].buffer_type   = MYSQL_TYPE_LONG;
  bind_params[2].buffer        = &user_id;

  bind_params[3].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[3].buffer        = (char *)redirect_uri;
  bind_params[3].buffer_length = redirect_len;
  bind_params[3].length        = &redirect_len;

  bind_params[4].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[4].buffer        = (char *)scopes_json;
  bind_params[4].buffer_length = scopes_len;
  bind_params[4].length        = &scopes_len;

  bind_params[5].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[5].buffer        = (char *)code_challenge;
  bind_params[5].buffer_length = challenge_len;
  bind_params[5].length        = &challenge_len;

  bind_params[6].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[6].buffer        = (char *)code_challenge_method;
  bind_params[6].buffer_length = challenge_meth_len;
  bind_params[6].length        = &challenge_meth_len;

  bind_params[7].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[7].buffer        = (char *)(state ? state : "");
  bind_params[7].buffer_length = state_len;
  bind_params[7].length        = &state_len;

  bind_params[8].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[8].buffer        = (char *)(nonce ? nonce : "");
  bind_params[8].buffer_length = nonce_len;
  bind_params[8].length        = &nonce_len;

  bind_params[9].buffer_type   = MYSQL_TYPE_LONG;
  bind_params[9].buffer        = &ttl_seconds;

  if (mysql_stmt_bind_param(stmt, bind_params)) {
    syslog(LOG_ERR, "%s: mysql_stmt_bind_param() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  if (mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: mysql_stmt_execute() failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_COMMAND)
  }

  mysql_stmt_close(stmt);

#ifdef __UF_TESTING
  syslog(LOG_DEBUG, "%s (pid:'%lu'): stored auth code for client_id='%lu' user_id='%lu'",
         __func__, pthread_self(), client_id, user_id);
#endif

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESULT_TYPE_NOOP)

#undef SQL_INSERT_AUTH_CODE
}

/**
 * @brief Verify a PKCE code_verifier against the challenge stored with the code.
 *
 * S256 compares the base64url-encoded SHA-256 of the verifier with the stored
 * challenge; plain compares the two strings.  Comparisons are length-checked and
 * constant-time.
 *
 * @param code_verifier[IN] verifier sent with the token request
 * @param code_challenge[IN] challenge stored at /authorize
 * @param code_challenge_method[IN] "S256" or "plain"
 * @return true when the verifier satisfies the challenge
 */
bool
OAuthVerifyPKCE(const char *code_verifier, const char *code_challenge, const char *code_challenge_method)
{
  if (!IS_STR_LOADED(code_verifier) || !IS_STR_LOADED(code_challenge) || !IS_STR_LOADED(code_challenge_method)) return false;

  size_t challenge_len = strlen(code_challenge);

  if (strcmp(code_challenge_method, "plain") == 0) {
    if (strlen(code_verifier) != challenge_len) return false;
    return memcmp_constant_time(code_verifier, code_challenge, challenge_len) == 0;
  }

  if (strcmp(code_challenge_method, "S256") != 0) return false;

  char expected[64] = {0};

  if (!GeneratePkceCodeChallenge(code_verifier, "S256", expected, sizeof expected)) return false;

  if (strlen(expected) != challenge_len) return false;

  return memcmp_constant_time(expected, code_challenge, challenge_len) == 0;
}

/**
 * @brief Authenticate the client presenting a back-channel request.
 *
 * A public client is identified by client_id alone.  A confidential client must
 * also present the secret recorded at registration.
 *
 * @param client_uuid[IN] hyphenated UUID of the client
 * @param client_secret[IN] optional secret presented with the request
 * @return RESCODE_BACKEND_DATA on success, EMPTYSET when the client is unknown,
 *         inactive, or the secret does not match, CONNECTION on backend failure
 */
UFSRVResult *
DbOAuthClientAuthenticate(const char *client_uuid, const char *client_secret)
{
#define SQL_SELECT_CLIENT_CREDENTIALS \
  "SELECT secret, client_type FROM oauth_clients WHERE uuid_serialised = ? AND is_active = 1"

  if (!IS_STR_LOADED(client_uuid)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_CLIENT_CREDENTIALS, sizeof(SQL_SELECT_CLIENT_CREDENTIALS) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long uuid_len = strlen(client_uuid);
  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[0].buffer        = (char *)client_uuid;
  bind_param[0].buffer_length = uuid_len;
  bind_param[0].length        = &uuid_len;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  char          db_secret[256] = {0};
  char          db_type[16]    = {0};
  unsigned long secret_len     = 0;
  unsigned long type_len       = 0;
  my_bool       is_null_secret = 0;
  my_bool       is_null_type   = 0;

  MYSQL_BIND bind_result[2];
  memset(bind_result, 0, sizeof(bind_result));

  bind_result[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[0].buffer        = db_secret;
  bind_result[0].buffer_length = sizeof(db_secret) - 1;
  bind_result[0].length        = &secret_len;
  bind_result[0].is_null       = &is_null_secret;

  bind_result[1].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[1].buffer        = db_type;
  bind_result[1].buffer_length = sizeof(db_type) - 1;
  bind_result[1].length        = &type_len;
  bind_result[1].is_null       = &is_null_type;

  if (mysql_stmt_bind_result(stmt, bind_result) || mysql_stmt_store_result(stmt)) {
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  mysql_stmt_close(stmt);

  if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  if (is_null_secret || is_null_type || strcmp(db_type, "confidential") != 0) {
    THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)
  }

  size_t provided_len = IS_STR_LOADED(client_secret) ? strlen(client_secret) : 0;

  if (provided_len == 0 || provided_len != strlen(db_secret) || memcmp_constant_time(client_secret, db_secret, provided_len) != 0) {
    syslog(LOG_WARNING, "%s: confidential client '%s' presented no or wrong secret", __func__, client_uuid);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_SELECT_CLIENT_CREDENTIALS
}

/**
 * @brief Render a stored scopes JSON array as the space-separated scope string.
 *
 * Accepts only a flat array of scope tokens, the shape BuildScopesJson() writes;
 * anything else yields an empty string rather than a guess.
 *
 * @param scopes_json[IN] JSON array read from the authorization code
 * @param buffer[OUT] caller-allocated, NUL-terminated on return
 * @param buffer_sz[IN] size of buffer
 * @return buffer
 */
char *
BuildScopeStringFromJson(const char *scopes_json, char *buffer, size_t buffer_sz)
{
  size_t out = 0;

  buffer[0] = '\0';

  if (!IS_STR_LOADED(scopes_json) || buffer_sz < 2) return buffer;

  const char *p = scopes_json;

  while (*p == ' ') p++;
  if (*p++ != '[') return buffer;

  for (;;) {
    while (*p == ' ') p++;

    if (*p == ']') {
      p++;
      while (*p == ' ') p++;
      if (*p != '\0') buffer[0] = '\0';
      else if (out > 0) buffer[out - 1] = '\0'; //drop the trailing space
      return buffer;
    }

    if (*p++ != '"') { buffer[0] = '\0'; return buffer; }

    while (*p != '\0' && *p != '"') {
      if (*p == ',' || *p == '\\') { buffer[0] = '\0'; return buffer; }
      if (out + 2 >= buffer_sz) { buffer[0] = '\0'; return buffer; }
      buffer[out++] = *p++;
    }

    if (*p++ != '"') { buffer[0] = '\0'; return buffer; }
    if (out == 0) { buffer[0] = '\0'; return buffer; }

    while (*p == ' ') p++;
    if (*p != ',' && *p != ']') { buffer[0] = '\0'; return buffer; }
    if (*p == ',') p++;

    if (out + 1 < buffer_sz) buffer[out++] = ' ';
  }
}

/**
 * @brief Persist an issued access token and its refresh token.
 *
 * The refresh token records the access token row it was issued with.  If the
 * second insert fails the first is deleted.
 *
 * @param client_id[IN] oauth_clients.client_id the grant belongs to
 * @param user_id[IN] resource owner
 * @param scopes_json[IN] granted scopes, JSON array
 * @param access_token[IN] token value to store
 * @param refresh_token[IN] refresh token value to store
 * @param access_expiry[IN] access token lifetime in seconds
 * @param refresh_expiry[IN] refresh token lifetime in seconds
 * @return RESCODE_BACKEND_DATA on success, COMMAND or CONNECTION on failure
 */
UFSRVResult *
DbStoreOAuthTokens(unsigned long client_id, unsigned long user_id, const char *scopes_json,
                   const char *authorization_code, const char *access_token, const char *refresh_token,
                   time_t access_expiry, time_t refresh_expiry)
{
#define SQL_INSERT_ACCESS_TOKEN \
  "INSERT INTO oauth_access_tokens (token, client_id, user_id, scopes, authorization_code, token_type, expires_at) " \
  "VALUES (?, ?, ?, ?, ?, 'Bearer', NOW() + INTERVAL ? SECOND)"

#define SQL_INSERT_REFRESH_TOKEN \
  "INSERT INTO oauth_refresh_tokens (token, client_id, user_id, scopes, expires_at, access_token_id, family_id) " \
  "VALUES (?, ?, ?, ?, NOW() + INTERVAL ? SECOND, ?, ?)"

#define SQL_DELETE_ACCESS_TOKEN "DELETE FROM oauth_access_tokens WHERE access_token_id = ?"

  if (!IS_STR_LOADED(access_token) || !IS_STR_LOADED(refresh_token) || !IS_STR_LOADED(scopes_json) || !IS_STR_LOADED(authorization_code)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  unsigned long access_len   = strlen(access_token);
  unsigned long scopes_len   = strlen(scopes_json);
  unsigned long refresh_len  = strlen(refresh_token);
  unsigned long code_len     = strlen(authorization_code);
  long long     access_ttl   = (long long)access_expiry;
  long long     refresh_ttl  = (long long)refresh_expiry;

  char          family[CONFIGDEFAULT_OAUTH_REFRESH_FAMILY_SZ + 1] = {0};
  unsigned long family_len = CONFIGDEFAULT_OAUTH_REFRESH_FAMILY_SZ;

  GenerateSecureRandomHexed(family, family_len);

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_INSERT_ACCESS_TOKEN, sizeof(SQL_INSERT_ACCESS_TOKEN) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  MYSQL_BIND bind_params[6];
  memset(bind_params, 0, sizeof(bind_params));

  bind_params[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[0].buffer        = (char *)access_token;
  bind_params[0].buffer_length = access_len;
  bind_params[0].length        = &access_len;

  bind_params[1].buffer_type   = MYSQL_TYPE_LONG;
  bind_params[1].buffer        = &client_id;

  bind_params[2].buffer_type   = MYSQL_TYPE_LONG;
  bind_params[2].buffer        = &user_id;

  bind_params[3].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[3].buffer        = (char *)scopes_json;
  bind_params[3].buffer_length = scopes_len;
  bind_params[3].length        = &scopes_len;

  bind_params[4].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[4].buffer        = (char *)authorization_code;
  bind_params[4].buffer_length = code_len;
  bind_params[4].length        = &code_len;

  bind_params[5].buffer_type   = MYSQL_TYPE_LONGLONG;
  bind_params[5].buffer        = &access_ttl;

  if (mysql_stmt_bind_param(stmt, bind_params) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: access token insert failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_COMMAND)
  }

  mysql_stmt_close(stmt);

  unsigned long access_token_id = (unsigned long)h_last_insert_id_mariadb(THREAD_CONTEXT_DB_BACKEND);

  stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_INSERT_REFRESH_TOKEN, sizeof(SQL_INSERT_REFRESH_TOKEN) - 1));

  if (IS_EMPTY(stmt)) goto error_delete_access_token;

  MYSQL_BIND refresh_params[7];
  memset(refresh_params, 0, sizeof(refresh_params));

  refresh_params[0].buffer_type   = MYSQL_TYPE_STRING;
  refresh_params[0].buffer        = (char *)refresh_token;
  refresh_params[0].buffer_length = refresh_len;
  refresh_params[0].length        = &refresh_len;

  refresh_params[1].buffer_type   = MYSQL_TYPE_LONG;
  refresh_params[1].buffer        = &client_id;

  refresh_params[2].buffer_type   = MYSQL_TYPE_LONG;
  refresh_params[2].buffer        = &user_id;

  refresh_params[3].buffer_type   = MYSQL_TYPE_STRING;
  refresh_params[3].buffer        = (char *)scopes_json;
  refresh_params[3].buffer_length = scopes_len;
  refresh_params[3].length        = &scopes_len;

  refresh_params[4].buffer_type   = MYSQL_TYPE_LONGLONG;
  refresh_params[4].buffer        = &refresh_ttl;

  refresh_params[5].buffer_type   = MYSQL_TYPE_LONG;
  refresh_params[5].buffer        = &access_token_id;

  refresh_params[6].buffer_type   = MYSQL_TYPE_STRING;
  refresh_params[6].buffer        = family;
  refresh_params[6].buffer_length = family_len;
  refresh_params[6].length        = &family_len;

  if (mysql_stmt_bind_param(stmt, refresh_params) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: refresh token insert failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    goto error_delete_access_token;
  }

  mysql_stmt_close(stmt);

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

  error_delete_access_token:
  stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_DELETE_ACCESS_TOKEN, sizeof(SQL_DELETE_ACCESS_TOKEN) - 1));

  if (!IS_EMPTY(stmt)) {
    MYSQL_BIND delete_param[1];
    memset(delete_param, 0, sizeof(delete_param));
    delete_param[0].buffer_type = MYSQL_TYPE_LONG;
    delete_param[0].buffer      = &access_token_id;

    if (!mysql_stmt_bind_param(stmt, delete_param)) mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
  }

  THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_COMMAND)

#undef SQL_INSERT_ACCESS_TOKEN
#undef SQL_INSERT_REFRESH_TOKEN
#undef SQL_DELETE_ACCESS_TOKEN
}

/**
 * @brief Revoke every token issued from one authorization code.
 *
 * Refresh tokens carry no code of their own; they reach it through the access
 * token they were issued with.
 *
 * @param authorization_code[IN] code whose tokens are to be revoked
 * @return RESCODE_BACKEND_DATA when both updates ran (even if they matched no
 *         rows), CONNECTION or COMMAND on failure
 */
UFSRVResult *
DbRevokeTokensForCode(const char *authorization_code)
{
#define SQL_REVOKE_ACCESS_TOKENS \
  "UPDATE oauth_access_tokens SET is_revoked = 1 WHERE authorization_code = ? AND is_revoked = 0"

#define SQL_REVOKE_REFRESH_TOKENS \
  "UPDATE oauth_refresh_tokens rt INNER JOIN oauth_access_tokens acc ON rt.access_token_id = acc.access_token_id " \
  "SET rt.is_revoked = 1 WHERE acc.authorization_code = ? AND rt.is_revoked = 0"

  if (!IS_STR_LOADED(authorization_code)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  unsigned long code_len = strlen(authorization_code);
  const char   *statements[2] = { SQL_REVOKE_ACCESS_TOKENS, SQL_REVOKE_REFRESH_TOKENS };
  const size_t  sizes[2]      = { sizeof(SQL_REVOKE_ACCESS_TOKENS) - 1, sizeof(SQL_REVOKE_REFRESH_TOKENS) - 1 };

  for (int i = 0; i < 2; i++) {
    MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, statements[i], sizes[i]));

    if (IS_EMPTY(stmt)) {
      THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
    }

    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
    bind_param[0].buffer        = (char *)authorization_code;
    bind_param[0].buffer_length = code_len;
    bind_param[0].length        = &code_len;

    if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
      syslog(LOG_ERR, "%s: revoke failed: %s", __func__, mysql_stmt_error(stmt));
      mysql_stmt_close(stmt);
      THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_COMMAND)
    }

    syslog(LOG_NOTICE, "%s: revoked %llu row(s) for a replayed code", __func__, (unsigned long long)mysql_stmt_affected_rows(stmt));
    mysql_stmt_close(stmt);
  }

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_REVOKE_ACCESS_TOKENS
#undef SQL_REVOKE_REFRESH_TOKENS
}

/**
 * @brief Read one token row for an RFC 7662 introspection response.
 *
 * Looks in oauth_access_tokens first and, when nothing matches, in
 * oauth_refresh_tokens. Whether the token is usable is decided by the query
 * (revoked or expired means inactive), not by the caller.
 *
 * @param token[IN] the token value presented for introspection
 * @param out[OUT] zeroed and populated; found stays false when nothing matched
 * @return RESCODE_BACKEND_DATA when the lookup ran, CONNECTION on backend failure
 */

static pthread_once_t s_jwt_keyset_once = PTHREAD_ONCE_INIT;
static JwtKeyset     *s_jwt_keyset      = NULL;

static bool
sAccessTokenIsJwt(void)
{
  const char *format = UfsrvConfigOauthAccessTokenFormat();

  return IS_STR_LOADED(format) && strcmp(format, "jwt") == 0;
}

static void
sBuildJwtKeyset(void)
{
  const char *path  = UfsrvConfigOauthJwtSigningKey();
  const char *key_id = UfsrvConfigOauthJwtActiveKid();
  JwtKeyset  *keyset = JwtKeysetNew();
  char        err[256] = {0};

  if (keyset == NULL) return;

  if (!IS_STR_LOADED(path)) {
    syslog(LOG_ERR, "%s: oauth.jwt_signing_key is not set; access tokens cannot be signed", __func__);

    JwtKeysetFree(keyset);

    return;
  }

  if (JwtKeysetAddPemFile(keyset, path, IS_STR_LOADED(key_id) ? key_id : NULL, err, sizeof err) != JWT_STATUS_OK) {
    syslog(LOG_ERR, "%s: could not load '%s': %s", __func__, path, err);

    JwtKeysetFree(keyset);

    return;
  }

  s_jwt_keyset = keyset;
}

static bool
sJwtKeyset(JwtKeyset **keyset_out)
{
  pthread_once(&s_jwt_keyset_once, sBuildJwtKeyset);

  *keyset_out = s_jwt_keyset;

  return s_jwt_keyset != NULL;
}

static bool
sMintJwtAccessToken(unsigned long client_id, unsigned long user_id, const char *scopes_json,
                    bool client_subject, const char *client_uuid,
                    char *token_out, size_t token_sz, char *jti_out, size_t jti_sz)
{
  JwtKeyset   *keyset = NULL;
  json_object *claims = NULL;
  UfsrvUid     uid    = {0};
  char         subject[64] = {0};
  char         scope[4096] = {0};
  char         jti[CONFIGDEFAULT_OAUTH_TOKEN_SZ + 1] = {0};
  char         err[256] = {0};
  char        *token    = NULL;
  const char  *issuer;
  time_t       issued_at;

  if (!sJwtKeyset(&keyset)) return false;

  GenerateSecureRandomHexed(jti, CONFIGDEFAULT_OAUTH_TOKEN_SZ);

  if (client_subject) {
    snprintf(subject, sizeof subject, "%s", IS_STR_LOADED(client_uuid) ? client_uuid : "");
  } else {
    if (!_RESULT_TYPE_SUCCESS(DbGetUfsrvuidByUserId(user_id, &uid))) {
      syslog(LOG_ERR, "%s: no ufsrvuid for user '%lu'; refusing to mint", __func__, user_id);

      return false;
    }

    UfsrvUidConvertSerialise(&uid, subject);
  }

  BuildScopeStringFromJson(scopes_json, scope, sizeof scope);

  issued_at = time(NULL);
  claims    = json_object_new_object();

  if (claims == NULL) return false;

  json_object_object_add(claims, "jti", json_object_new_string(jti));
  json_object_object_add(claims, "iat", json_object_new_int64(issued_at));
  json_object_object_add(claims, "exp", json_object_new_int64(issued_at + CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_EXPIRY));
  json_object_object_add(claims, "sub", json_object_new_string(subject));
  json_object_object_add(claims, "sub_type", json_object_new_string(client_subject ? "client" : "user"));
  json_object_object_add(claims, "user_id", json_object_new_int64(user_id));
  json_object_object_add(claims, "client_id", json_object_new_int64(client_id));

  if (IS_STR_LOADED(scope)) json_object_object_add(claims, "scope", json_object_new_string(scope));
  if (client_subject && IS_STR_LOADED(client_uuid)) {
    json_object_object_add(claims, "client_uuid", json_object_new_string(client_uuid));
  }

  issuer = UfsrvConfigOauthJwtIssuer();

  if (IS_STR_LOADED(issuer)) json_object_object_add(claims, "iss", json_object_new_string(issuer));

  token = JwtMint(keyset, NULL, claims, err, sizeof err);
  json_object_put(claims);

  if (token == NULL) {
    syslog(LOG_ERR, "%s: the access token could not be signed: %s", __func__, err);

    return false;
  }

  if (strlen(token) >= token_sz) {
    syslog(LOG_ERR, "%s: the signed access token does not fit in %zu bytes", __func__, token_sz);
    free(token);

    return false;
  }

  snprintf(token_out, token_sz, "%s", token);
  snprintf(jti_out, jti_sz, "%s", jti);
  free(token);

  return true;
}

static bool
sMintAccessToken(unsigned long client_id, unsigned long user_id, const char *scopes_json,
                 bool client_subject, const char *client_uuid,
                 char *presented_out, size_t presented_sz, char *stored_out, size_t stored_sz)
{
  if (sAccessTokenIsJwt()) {
    return sMintJwtAccessToken(client_id, user_id, scopes_json, client_subject, client_uuid,
                               presented_out, presented_sz, stored_out, stored_sz);
  }

  GenerateSecureRandomHexed(presented_out, CONFIGDEFAULT_OAUTH_TOKEN_SZ);
  snprintf(stored_out, stored_sz, "%s", presented_out);

  return true;
}

static long long
sJsonInt64(json_object *object, const char *name, long long fallback)
{
  json_object *value = NULL;

  if (!json_object_object_get_ex(object, name, &value) || !json_object_is_type(value, json_type_int)) {
    return fallback;
  }

  return json_object_get_int64(value);
}

static bool
sJsonString(json_object *object, const char *name, char *buffer, size_t buffer_sz)
{
  json_object *value = NULL;

  buffer[0] = '\0';

  if (!json_object_object_get_ex(object, name, &value) || !json_object_is_type(value, json_type_string)) {
    return false;
  }

  snprintf(buffer, buffer_sz, "%s", json_object_get_string(value));

  return IS_STR_LOADED(buffer);
}

static bool
sIntrospectJwtAccessToken(const char *token, OAuthTokenIntrospection *out)
{
  static const JwtVerifyOptions options = {
      .issuer         = NULL,
      .audience       = NULL,
      .require_expiry = false,
      .leeway_seconds = 0,
  };

  JwtKeyset   *keyset = NULL;
  json_object *claims = NULL;
  const char  *issuer;
  char         scope[4096] = {0};
  long long    expiry;

  if (!sAccessTokenIsJwt() || strchr(token, '.') == NULL) return false;
  if (!sJwtKeyset(&keyset)) return false;

  issuer = UfsrvConfigOauthJwtIssuer();

  JwtVerifyOptions expected = options;
  expected.issuer = IS_STR_LOADED(issuer) ? issuer : NULL;

  if (JwtVerify(token, keyset, &expected, &claims) != JWT_STATUS_OK) return false;

  out->found          = true;
  out->is_refresh     = false;
  out->token_type[0]  = '\0';
  snprintf(out->token_type, sizeof out->token_type, "%s", "Bearer");

  sJsonString(claims, "scope", scope, sizeof scope);
  BuildScopesJson(scope, out->scopes, sizeof out->scopes);

  sJsonString(claims, "sub", out->subject, sizeof out->subject);
  sJsonString(claims, "jti", out->jti, sizeof out->jti);
  sJsonString(claims, "client_uuid", out->client_uuid, sizeof out->client_uuid);

  out->client_id   = (unsigned long)sJsonInt64(claims, "client_id", 0);
  out->user_id     = (unsigned long)sJsonInt64(claims, "user_id", 0);
  out->issued_at   = (time_t)sJsonInt64(claims, "iat", 0);
  out->expires_at  = (time_t)sJsonInt64(claims, "exp", 0);

  char sub_type[16] = {0};
  sJsonString(claims, "sub_type", sub_type, sizeof sub_type);
  out->is_client_subject = strcmp(sub_type, "client") == 0;

  expiry        = sJsonInt64(claims, "exp", 0);
  out->is_active = expiry > (long long)time(NULL);

  json_object_put(claims);

  return true;
}

UFSRVResult *
DbIntrospectOAuthToken(const char *token, OAuthTokenIntrospection *out)
{
#define SQL_INTROSPECT_ACCESS_TOKEN \
  "SELECT t.client_id, t.user_id, t.scopes, t.token_type, UNIX_TIMESTAMP(t.issued_at), UNIX_TIMESTAMP(t.expires_at), " \
  "(t.is_revoked = 0 AND t.expires_at > NOW()), COALESCE(t.jti, ''), t.subject_type, c.uuid_serialised " \
  "FROM oauth_access_tokens t JOIN oauth_clients c ON c.client_id = t.client_id WHERE t.token = ? LIMIT 1"

#define SQL_INTROSPECT_REFRESH_TOKEN \
  "SELECT r.client_id, r.user_id, r.scopes, 'Bearer', UNIX_TIMESTAMP(r.issued_at), UNIX_TIMESTAMP(r.expires_at), " \
  "(r.is_revoked = 0 AND r.expires_at > NOW()), '', 'user', c.uuid_serialised " \
  "FROM oauth_refresh_tokens r JOIN oauth_clients c ON c.client_id = r.client_id WHERE r.token = ? LIMIT 1"

  if (IS_EMPTY(out) || !IS_STR_LOADED(token)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  memset(out, 0, sizeof(*out));

  OAuthTokenIntrospection signed_token = {0};
  bool is_jwt = sIntrospectJwtAccessToken(token, &signed_token);

  if (is_jwt) {
    if (!IS_STR_LOADED(signed_token.jti)) {
      THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)
    }
    /* JWTs are stored by JTI. A valid signature alone does not establish
     * that the grant is still active after revocation or code replay. */
    token = signed_token.jti;
  }

  const char   *tables[2] = { SQL_INTROSPECT_ACCESS_TOKEN, SQL_INTROSPECT_REFRESH_TOKEN };
  const size_t  sizes[2]  = { sizeof(SQL_INTROSPECT_ACCESS_TOKEN) - 1, sizeof(SQL_INTROSPECT_REFRESH_TOKEN) - 1 };
  unsigned long token_len = strlen(token);

  for (int i = 0; i < (is_jwt ? 1 : 2); i++) {
    MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, tables[i], sizes[i]));

    if (IS_EMPTY(stmt)) {
      THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
    }

    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
    bind_param[0].buffer        = (char *)token;
    bind_param[0].buffer_length = token_len;
    bind_param[0].length        = &token_len;

    if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
      syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
      mysql_stmt_close(stmt);
      THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
    }

    unsigned long db_client_id = 0;
    unsigned long db_user_id   = 0;
    long long     issued_at    = 0;
    long long     expires_at   = 0;
    my_bool       is_active    = 0;
    my_bool       null_scopes = 0, null_type = 0, null_active = 0, null_jti = 0, null_subject = 0, null_uuid = 0;
    unsigned long scopes_len = 0, type_len = 0, jti_len = 0, subject_len = 0, uuid_len = 0;
    char          db_subject[16] = {0};

    MYSQL_BIND bind_result[10];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_LONG;
    bind_result[0].buffer      = &db_client_id;
    bind_result[1].buffer_type = MYSQL_TYPE_LONG;
    bind_result[1].buffer      = &db_user_id;
    bind_result[2].buffer_type   = MYSQL_TYPE_STRING;
    bind_result[2].buffer        = out->scopes;
    bind_result[2].buffer_length = sizeof(out->scopes) - 1;
    bind_result[2].length        = &scopes_len;
    bind_result[2].is_null       = &null_scopes;
    bind_result[3].buffer_type   = MYSQL_TYPE_STRING;
    bind_result[3].buffer        = out->token_type;
    bind_result[3].buffer_length = sizeof(out->token_type) - 1;
    bind_result[3].length        = &type_len;
    bind_result[3].is_null       = &null_type;
    bind_result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[4].buffer      = &issued_at;
    bind_result[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[5].buffer      = &expires_at;
    bind_result[6].buffer_type = MYSQL_TYPE_TINY;
    bind_result[6].buffer      = &is_active;
    bind_result[6].is_null     = &null_active;
    bind_result[7].buffer_type   = MYSQL_TYPE_STRING;
    bind_result[7].buffer        = out->jti;
    bind_result[7].buffer_length = sizeof(out->jti) - 1;
    bind_result[7].length        = &jti_len;
    bind_result[7].is_null       = &null_jti;
    bind_result[8].buffer_type   = MYSQL_TYPE_STRING;
    bind_result[8].buffer        = db_subject;
    bind_result[8].buffer_length = sizeof(db_subject) - 1;
    bind_result[8].length        = &subject_len;
    bind_result[8].is_null       = &null_subject;
    bind_result[9].buffer_type   = MYSQL_TYPE_STRING;
    bind_result[9].buffer        = out->client_uuid;
    bind_result[9].buffer_length = sizeof(out->client_uuid) - 1;
    bind_result[9].length        = &uuid_len;
    bind_result[9].is_null       = &null_uuid;

    if (mysql_stmt_bind_result(stmt, bind_result) || mysql_stmt_store_result(stmt)) {
      mysql_stmt_close(stmt);
      THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
    }

    int fetch_status = mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);

    if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) continue;

    if (is_jwt) {
      if (signed_token.client_id == db_client_id && signed_token.user_id == db_user_id) {
        signed_token.is_active = signed_token.is_active && !null_active && is_active;
        *out = signed_token;
      } else {
        memset(out, 0, sizeof(*out));
      }
      break;
    }

    out->found       = true;
    out->is_refresh  = (i == 1);
    out->is_active   = (!null_active && is_active);
    out->client_id   = db_client_id;
    out->user_id     = db_user_id;
    out->issued_at   = (time_t)issued_at;
    out->expires_at  = (time_t)expires_at;
    if (null_scopes) out->scopes[0] = '\0';
    if (null_type)   out->token_type[0] = '\0';
    if (null_jti)    out->jti[0] = '\0';
    if (null_uuid)   out->client_uuid[0] = '\0';
    out->is_client_subject = (!null_subject && strcmp(db_subject, "client") == 0);
    out->scopes[sizeof(out->scopes) - 1]          = '\0';
    out->token_type[sizeof(out->token_type) - 1]  = '\0';
    out->jti[sizeof(out->jti) - 1]                = '\0';
    out->client_uuid[sizeof(out->client_uuid) - 1] = '\0';

    break;
  }

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_INTROSPECT_ACCESS_TOKEN
#undef SQL_INTROSPECT_REFRESH_TOKEN
}

/**
 * @brief Revoke a token presented by the client that owns it (RFC 7009).
 *
 * @param token[IN]          The token string, access or refresh.
 * @param client_db_id[IN]   oauth_clients.client_id of the authenticated client.
 * @param prefer_refresh[IN] True when the caller hinted refresh_token.
 * @return RESCODE_BACKEND_DATA on success, CONNECTION on backend failure.
 */
UFSRVResult *
DbRevokeOAuthToken(const char *token, unsigned long client_db_id, bool prefer_refresh)
{
#define SQL_REVOKE_ACCESS \
  "UPDATE oauth_access_tokens SET is_revoked = 1 WHERE token = ? AND client_id = ? AND is_revoked = 0"

#define SQL_REVOKE_REFRESH_OF_ACCESS \
  "UPDATE oauth_refresh_tokens rt INNER JOIN oauth_access_tokens acc ON rt.access_token_id = acc.access_token_id " \
  "SET rt.is_revoked = 1 WHERE acc.token = ? AND acc.client_id = ? AND rt.is_revoked = 0"

#define SQL_REVOKE_REFRESH \
  "UPDATE oauth_refresh_tokens SET is_revoked = 1 WHERE token = ? AND client_id = ? AND is_revoked = 0"

#define SQL_REVOKE_ACCESS_OF_REFRESH \
  "UPDATE oauth_access_tokens acc INNER JOIN oauth_refresh_tokens rt ON acc.access_token_id = rt.access_token_id " \
  "SET acc.is_revoked = 1 WHERE rt.token = ? AND rt.client_id = ? AND acc.is_revoked = 0"

  if (!IS_STR_LOADED(token) || client_db_id == 0) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  OAuthTokenIntrospection presented = {0};

  if (sIntrospectJwtAccessToken(token, &presented) && IS_STR_LOADED(presented.jti)) token = presented.jti;

  unsigned long token_len = strlen(token);
  const char   *access_pair[2]  = { SQL_REVOKE_ACCESS, SQL_REVOKE_REFRESH_OF_ACCESS };
  const char   *refresh_pair[2] = { SQL_REVOKE_REFRESH, SQL_REVOKE_ACCESS_OF_REFRESH };
  const size_t  access_sizes[2]  = { sizeof(SQL_REVOKE_ACCESS) - 1, sizeof(SQL_REVOKE_REFRESH_OF_ACCESS) - 1 };
  const size_t  refresh_sizes[2] = { sizeof(SQL_REVOKE_REFRESH) - 1, sizeof(SQL_REVOKE_ACCESS_OF_REFRESH) - 1 };

  const char **first_statements  = prefer_refresh ? refresh_pair  : access_pair;
  const char **second_statements = prefer_refresh ? access_pair   : refresh_pair;
  const size_t *first_sizes      = prefer_refresh ? refresh_sizes : access_sizes;
  const size_t *second_sizes     = prefer_refresh ? access_sizes  : refresh_sizes;

  for (int pass = 0; pass < 2; pass++) {
    const char **statements = (pass == 0) ? first_statements : second_statements;
    const size_t *sizes      = (pass == 0) ? first_sizes      : second_sizes;

    for (int i = 0; i < 2; i++) {
      MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, statements[i], sizes[i]));

      if (IS_EMPTY(stmt)) {
        THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
      }

      MYSQL_BIND bind_param[2];
      memset(bind_param, 0, sizeof(bind_param));

      bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
      bind_param[0].buffer        = (char *)token;
      bind_param[0].buffer_length = token_len;
      bind_param[0].length        = &token_len;

      bind_param[1].buffer_type   = MYSQL_TYPE_LONG;
      bind_param[1].buffer        = &client_db_id;

      if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
        syslog(LOG_ERR, "%s: revocation failed: %s", __func__, mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_COMMAND)
      }

      syslog(LOG_DEBUG, "%s {cid:'%lu'}: revoked %llu row(s)", __func__, client_db_id, (unsigned long long)mysql_stmt_affected_rows(stmt));
      mysql_stmt_close(stmt);
    }
  }

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_REVOKE_ACCESS
#undef SQL_REVOKE_REFRESH_OF_ACCESS
#undef SQL_REVOKE_REFRESH
#undef SQL_REVOKE_ACCESS_OF_REFRESH
}

/**
 * @brief Exchange an authorization code for a token pair.
 *
 * This is the one implementation of the grant: it looks the code up, identifies
 * the client, proves the code was issued for the redirect_uri presented, checks
 * PKCE, redeems the code exactly once and stores the pair.  Both callers — the
 * back-channel /token endpoint and the browser landing that the code is
 * delivered to — reach the same answers through it, so the two can never drift
 * on what counts as a valid exchange.
 *
 * A look-up that finds a spent row is not immediately a failure: if the caller
 * can also satisfy that row's PKCE challenge, it is the legitimate holder
 * presenting the code a second time, and the tokens already issued from it are
 * revoked.  Anyone else cannot revoke a live grant by guessing codes.
 *
 * @param client_uuid[IN]   client the code was issued to
 * @param client_secret[IN] optional secret; NULL or empty for a public client
 * @param code[IN]          the authorization code presented for exchange
 * @param redirect_uri[IN]  the URI presented, which must equal the issued one
 * @param code_verifier[IN] PKCE verifier whose challenge the row holds
 * @param tokens_out[OUT]   receives the pair on OAUTH_EXCHANGE_STATUS_OK
 * @return the outcome of the exchange; tokens_out is only meaningful on OK
 */
OAuthExchangeStatus
DbExchangeAuthorizationCode(const char *client_uuid, const char *client_secret,
                            const char *code, const char *redirect_uri,
                            const char *code_verifier, OAuthTokenPair *tokens_out)
{
  if (!IS_STR_LOADED(client_uuid) || !IS_STR_LOADED(code) ||
      !IS_STR_LOADED(redirect_uri) || !IS_STR_LOADED(code_verifier) || tokens_out == NULL) {
    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  memset(tokens_out, 0, sizeof *tokens_out);

  AuthorizationCodeGrant grant = {0};
  UFSRVResult *lookup_res = DbLookupAuthorizationCodeGrant(code, client_uuid, false, &grant);

  if (!lookup_res || _RESULT_CODE_EQUAL(lookup_res, RESCODE_BACKEND_CONNECTION)) {
    syslog(LOG_ERR, "%s: grant lookup failed", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  if (_RESULT_TYPE_ERROR(lookup_res)) {
    AuthorizationCodeGrant redeemed = {0};
    UFSRVResult *replay_res = DbLookupAuthorizationCodeGrant(code, client_uuid, true, &redeemed);

    if (_RESULT_TYPE_SUCCESS(replay_res) &&
        OAuthVerifyPKCE(code_verifier, redeemed.code_challenge, redeemed.code_challenge_method)) {
      syslog(LOG_WARNING, "%s: authorization code replayed; revoking its tokens", __func__);
      DbRevokeTokensForCode(code);

      return OAUTH_EXCHANGE_STATUS_REPLAY;
    }

    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  UFSRVResult *client_auth_res = DbOAuthClientAuthenticate(client_uuid, client_secret);

  if (!client_auth_res || _RESULT_CODE_EQUAL(client_auth_res, RESCODE_BACKEND_CONNECTION)) {
    syslog(LOG_ERR, "%s: client authentication could not be performed", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  if (!_RESULT_TYPE_SUCCESS(client_auth_res)) {
    syslog(LOG_WARNING, "%s: client '%s' did not authenticate", __func__, client_uuid);

    return OAUTH_EXCHANGE_STATUS_INVALID_CLIENT;
  }

  if (strcmp(redirect_uri, grant.redirect_uri) != 0) {
    syslog(LOG_WARNING, "%s: redirect_uri does not match the one the code was issued for", __func__);

    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  if (!OAuthVerifyPKCE(code_verifier, grant.code_challenge, grant.code_challenge_method)) {
    syslog(LOG_WARNING, "%s: PKCE verification failed", __func__);

    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  bool         is_replay  = false;
  UFSRVResult *redeem_res = DbRedeemAuthorizationCode(code, client_uuid, &is_replay);

  if (!redeem_res || _RESULT_CODE_EQUAL(redeem_res, RESCODE_BACKEND_CONNECTION)) {
    syslog(LOG_ERR, "%s: grant redemption failed", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  if (!_RESULT_TYPE_SUCCESS(redeem_res)) {
    syslog(LOG_DEBUG, "%s: %s; no token was issued", __func__, is_replay ? "authorization code replayed" : "authorization code not redeemable");

    return is_replay ? OAUTH_EXCHANGE_STATUS_REPLAY : OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  char access_token[CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_MAX_SZ + 1] = {0};
  char access_store[CONFIGDEFAULT_OAUTH_TOKEN_SZ + 1] = {0};
  char refresh_token[CONFIGDEFAULT_OAUTH_TOKEN_SZ + 1] = {0};

  if (!sMintAccessToken(grant.client_id, grant.user_id, IS_STR_LOADED(grant.scopes) ? grant.scopes : "[]",
                        false, NULL, access_token, sizeof access_token, access_store, sizeof access_store)) {
    syslog(LOG_ERR, "%s: failed to mint the access token", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  GenerateSecureRandomHexed(refresh_token, CONFIGDEFAULT_OAUTH_TOKEN_SZ);

  UFSRVResult *store_res = DbStoreOAuthTokens(grant.client_id, grant.user_id,
                                              IS_STR_LOADED(grant.scopes) ? grant.scopes : "[]",
                                              code, access_store, refresh_token,
                                              CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_EXPIRY,
                                              CONFIGDEFAULT_OAUTH_REFRESH_TOKEN_EXPIRY);

  if (!_RESULT_TYPE_SUCCESS(store_res)) {
    syslog(LOG_ERR, "%s: failed to persist the token pair", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  snprintf(tokens_out->access_token,  sizeof tokens_out->access_token,  "%s", access_token);
  snprintf(tokens_out->refresh_token, sizeof tokens_out->refresh_token, "%s", refresh_token);
  snprintf(tokens_out->scopes,        sizeof tokens_out->scopes,        "%s", grant.scopes);

  tokens_out->client_id     = grant.client_id;
  tokens_out->user_id       = grant.user_id;
  tokens_out->access_expiry = CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_EXPIRY;

  return OAUTH_EXCHANGE_STATUS_OK;
}

static UFSRVResult *
sFetchClientAllowedScopes(unsigned long client_db_id, char *buffer, size_t buffer_sz)
{
#define SQL_SELECT_CLIENT_SCOPES "SELECT allowed_scopes FROM oauth_clients WHERE client_id = ?"

  buffer[0] = '\0';

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_CLIENT_SCOPES, sizeof(SQL_SELECT_CLIENT_SCOPES) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type = MYSQL_TYPE_LONG;
  bind_param[0].buffer      = &client_db_id;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long scopes_len = 0;
  my_bool       null_scopes = 0;

  MYSQL_BIND bind_result[1];
  memset(bind_result, 0, sizeof(bind_result));
  bind_result[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[0].buffer        = buffer;
  bind_result[0].buffer_length = buffer_sz - 1;
  bind_result[0].length        = &scopes_len;
  bind_result[0].is_null       = &null_scopes;

  if (mysql_stmt_bind_result(stmt, bind_result) || mysql_stmt_store_result(stmt)) {
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  mysql_stmt_close(stmt);

  if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  buffer[buffer_sz - 1] = '\0';
  if (null_scopes) buffer[0] = '\0';

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_SELECT_CLIENT_SCOPES
}

static UFSRVResult *
sStoreClientAccessToken(unsigned long client_db_id, unsigned long provenance_user_id, const char *scopes_json,
                        const char *access_token, time_t access_expiry)
{
#define SQL_INSERT_CLIENT_TOKEN \
  "INSERT INTO oauth_access_tokens (token, client_id, user_id, scopes, token_type, subject_type, expires_at) " \
  "VALUES (?, ?, ?, ?, 'Bearer', 'client', NOW() + INTERVAL ? SECOND)"

  if (!IS_STR_LOADED(access_token) || !IS_STR_LOADED(scopes_json)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  unsigned long token_len  = strlen(access_token);
  unsigned long scopes_len = strlen(scopes_json);
  long long     access_ttl = (long long)access_expiry;

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_INSERT_CLIENT_TOKEN, sizeof(SQL_INSERT_CLIENT_TOKEN) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  MYSQL_BIND bind_params[5];
  memset(bind_params, 0, sizeof(bind_params));

  bind_params[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[0].buffer        = (char *)access_token;
  bind_params[0].buffer_length = token_len;
  bind_params[0].length        = &token_len;

  bind_params[1].buffer_type = MYSQL_TYPE_LONG;
  bind_params[1].buffer      = &client_db_id;

  bind_params[2].buffer_type = MYSQL_TYPE_LONG;
  bind_params[2].buffer      = &provenance_user_id;

  bind_params[3].buffer_type   = MYSQL_TYPE_STRING;
  bind_params[3].buffer        = (char *)scopes_json;
  bind_params[3].buffer_length = scopes_len;
  bind_params[3].length        = &scopes_len;

  bind_params[4].buffer_type = MYSQL_TYPE_LONGLONG;
  bind_params[4].buffer      = &access_ttl;

  if (mysql_stmt_bind_param(stmt, bind_params) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_COMMAND)
  }

  mysql_stmt_close(stmt);

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_INSERT_CLIENT_TOKEN
}

static bool
sRequestedScopesAreAllowed(const char *requested_scope, const char *allowed_space_list)
{
  const char *cursor = requested_scope;

  while (*cursor) {
    while (*cursor == ' ') cursor++;
    if (!*cursor) break;

    const char *scope = cursor;
    while (*cursor && *cursor != ' ') cursor++;

    size_t      scope_len = (size_t)(cursor - scope);
    const char *candidate_cursor = allowed_space_list;

    for (;;) {
      while (*candidate_cursor == ' ') candidate_cursor++;
      if (!*candidate_cursor) return false;

      const char *candidate = candidate_cursor;
      while (*candidate_cursor && *candidate_cursor != ' ') candidate_cursor++;

      if ((size_t)(candidate_cursor - candidate) == scope_len && strncmp(candidate, scope, scope_len) == 0) break;
    }
  }

  return true;
}

OAuthExchangeStatus
OAuthIssueClientCredentialsToken(const char *client_uuid, const char *client_secret,
                                 const char *requested_scope, OAuthTokenPair *tokens_out)
{
  if (!IS_STR_LOADED(client_uuid) || IS_EMPTY(tokens_out)) {
    return OAUTH_EXCHANGE_STATUS_INVALID_CLIENT;
  }

  memset(tokens_out, 0, sizeof *tokens_out);

  UFSRVResult *auth_res = DbOAuthClientAuthenticate(client_uuid, client_secret);

  if (!auth_res || _RESULT_CODE_EQUAL(auth_res, RESCODE_BACKEND_CONNECTION)) {
    syslog(LOG_ERR, "%s: client authentication could not be performed", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  if (!_RESULT_TYPE_SUCCESS(auth_res)) {
    syslog(LOG_WARNING, "%s: client '%s' did not authenticate", __func__, client_uuid);

    return OAUTH_EXCHANGE_STATUS_INVALID_CLIENT;
  }

  OAuthClient   client       = {0};
  unsigned long client_db_id = 0;
  UFSRVResult  *lookup_res   = DbOAuthClientLookupByUuid(client_uuid, &client, &client_db_id);

  if (!lookup_res || _RESULT_CODE_EQUAL(lookup_res, RESCODE_BACKEND_CONNECTION)) {
    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  if (_RESULT_TYPE_ERROR(lookup_res)) {
    return OAUTH_EXCHANGE_STATUS_INVALID_CLIENT;
  }

  char allowed_json[4096]       = {0};
  char allowed_space_list[4096] = {0};

  UFSRVResult *scopes_res = sFetchClientAllowedScopes(client_db_id, allowed_json, sizeof allowed_json);

  if (!_RESULT_TYPE_SUCCESS(scopes_res)) {
    free(client.client_name.by_ref);
    free(client.redirect_uris);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  BuildScopeStringFromJson(allowed_json, allowed_space_list, sizeof allowed_space_list);

  char granted_json[4096] = {0};

  if (IS_STR_LOADED(requested_scope)) {
    if (!sRequestedScopesAreAllowed(requested_scope, allowed_space_list)) {
      syslog(LOG_WARNING, "%s: client '%s' requested a scope it is not allowed", __func__, client_uuid);
      free(client.client_name.by_ref);
      free(client.redirect_uris);

      return OAUTH_EXCHANGE_STATUS_INVALID_SCOPE;
    }

    BuildScopesJson(requested_scope, granted_json, sizeof granted_json);
  } else {
    snprintf(granted_json, sizeof granted_json, "%s", IS_STR_LOADED(allowed_json) ? allowed_json : "[]");
  }

  char access_token[CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_MAX_SZ + 1] = {0};
  char access_store[CONFIGDEFAULT_OAUTH_TOKEN_SZ + 1] = {0};

  unsigned long provenance_user_id = client.user_id;

  free(client.client_name.by_ref);
  free(client.redirect_uris);

  if (!sMintAccessToken(client_db_id, provenance_user_id, granted_json, true, client_uuid,
                        access_token, sizeof access_token, access_store, sizeof access_store)) {
    syslog(LOG_ERR, "%s: failed to mint the access token", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  UFSRVResult *store_res = sStoreClientAccessToken(client_db_id, provenance_user_id, granted_json, access_store,
                                                   CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_EXPIRY);

  if (!_RESULT_TYPE_SUCCESS(store_res)) {
    syslog(LOG_ERR, "%s: failed to persist the client access token", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  snprintf(tokens_out->access_token, sizeof tokens_out->access_token, "%s", access_token);
  snprintf(tokens_out->scopes,       sizeof tokens_out->scopes,       "%s", granted_json);

  tokens_out->client_id     = client_db_id;
  tokens_out->user_id       = provenance_user_id;
  tokens_out->access_expiry = CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_EXPIRY;

  return OAUTH_EXCHANGE_STATUS_OK;
}

typedef struct {
  bool          found;
  unsigned long row_id;
  unsigned long client_id;
  unsigned long user_id;
  bool          is_revoked;
  bool          is_active;
  char          family_id[CONFIGDEFAULT_OAUTH_REFRESH_FAMILY_SZ + 1];
  char          client_uuid[UUID4_STR_BUFFER_SIZE];
  char          scopes[4096];
} RefreshTokenRow;

static UFSRVResult *
sLookupRefreshTokenForExchange(const char *refresh_token, RefreshTokenRow *out)
{
#define SQL_SELECT_REFRESH_TOKEN \
  "SELECT r.refresh_tokens_id, r.client_id, r.user_id, r.scopes, r.is_revoked, (r.expires_at > NOW()), " \
  "COALESCE(r.family_id, ''), c.uuid_serialised " \
  "FROM oauth_refresh_tokens r JOIN oauth_clients c ON c.client_id = r.client_id WHERE r.token = ? LIMIT 1"

  if (IS_EMPTY(out) || !IS_STR_LOADED(refresh_token)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  memset(out, 0, sizeof(*out));

  unsigned long token_len = strlen(refresh_token);

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_REFRESH_TOKEN, sizeof(SQL_SELECT_REFRESH_TOKEN) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[0].buffer        = (char *)refresh_token;
  bind_param[0].buffer_length = token_len;
  bind_param[0].length        = &token_len;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long db_row_id    = 0;
  unsigned long db_client_id = 0;
  unsigned long db_user_id   = 0;
  unsigned long scopes_len   = 0;
  unsigned long family_len   = 0;
  unsigned long uuid_len     = 0;
  my_bool       is_revoked   = 0;
  my_bool       is_active    = 0;
  my_bool       null_scopes = 0, null_revoked = 0, null_active = 0, null_family = 0, null_uuid = 0;

  MYSQL_BIND bind_result[8];
  memset(bind_result, 0, sizeof(bind_result));

  bind_result[0].buffer_type = MYSQL_TYPE_LONG;
  bind_result[0].buffer      = &db_row_id;
  bind_result[1].buffer_type = MYSQL_TYPE_LONG;
  bind_result[1].buffer      = &db_client_id;
  bind_result[2].buffer_type = MYSQL_TYPE_LONG;
  bind_result[2].buffer      = &db_user_id;

  bind_result[3].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[3].buffer        = out->scopes;
  bind_result[3].buffer_length = sizeof(out->scopes) - 1;
  bind_result[3].length        = &scopes_len;
  bind_result[3].is_null       = &null_scopes;

  bind_result[4].buffer_type = MYSQL_TYPE_TINY;
  bind_result[4].buffer      = &is_revoked;
  bind_result[4].is_null     = &null_revoked;

  bind_result[5].buffer_type = MYSQL_TYPE_TINY;
  bind_result[5].buffer      = &is_active;
  bind_result[5].is_null     = &null_active;

  bind_result[6].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[6].buffer        = out->family_id;
  bind_result[6].buffer_length = sizeof(out->family_id) - 1;
  bind_result[6].length        = &family_len;
  bind_result[6].is_null       = &null_family;

  bind_result[7].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[7].buffer        = out->client_uuid;
  bind_result[7].buffer_length = sizeof(out->client_uuid) - 1;
  bind_result[7].length        = &uuid_len;
  bind_result[7].is_null       = &null_uuid;

  if (mysql_stmt_bind_result(stmt, bind_result) || mysql_stmt_store_result(stmt)) {
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  int fetch_status = mysql_stmt_fetch(stmt);
  mysql_stmt_close(stmt);

  if (fetch_status == 0 || fetch_status == MYSQL_DATA_TRUNCATED) {
    out->found      = true;
    out->row_id     = db_row_id;
    out->client_id  = db_client_id;
    out->user_id    = db_user_id;
    out->is_revoked = (!null_revoked && is_revoked);
    out->is_active  = (!null_active && is_active);
    if (null_scopes) out->scopes[0]    = '\0';
    if (null_family) out->family_id[0] = '\0';
    if (null_uuid)   out->client_uuid[0] = '\0';
    out->scopes[sizeof(out->scopes) - 1]           = '\0';
    out->family_id[sizeof(out->family_id) - 1]     = '\0';
    out->client_uuid[sizeof(out->client_uuid) - 1] = '\0';
  }

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_SELECT_REFRESH_TOKEN
}

static UFSRVResult *
sRevokeRefreshTokenFamily(const char *family_id)
{
#define SQL_REVOKE_FAMILY_REFRESH \
  "UPDATE oauth_refresh_tokens SET is_revoked = 1 WHERE family_id = ? AND is_revoked = 0"

#define SQL_REVOKE_FAMILY_ACCESS \
  "UPDATE oauth_access_tokens acc INNER JOIN oauth_refresh_tokens rt ON rt.access_token_id = acc.access_token_id " \
  "SET acc.is_revoked = 1 WHERE rt.family_id = ? AND acc.is_revoked = 0"

  if (!IS_STR_LOADED(family_id)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  unsigned long family_len = strlen(family_id);
  const char   *statements[2] = { SQL_REVOKE_FAMILY_REFRESH, SQL_REVOKE_FAMILY_ACCESS };
  const size_t  sizes[2]      = { sizeof(SQL_REVOKE_FAMILY_REFRESH) - 1, sizeof(SQL_REVOKE_FAMILY_ACCESS) - 1 };

  for (int i = 0; i < 2; i++) {
    MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, statements[i], sizes[i]));

    if (IS_EMPTY(stmt)) {
      THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
    }

    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
    bind_param[0].buffer        = (char *)family_id;
    bind_param[0].buffer_length = family_len;
    bind_param[0].length        = &family_len;

    if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
      syslog(LOG_ERR, "%s: revoke failed: %s", __func__, mysql_stmt_error(stmt));
      mysql_stmt_close(stmt);
      THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_COMMAND)
    }

    syslog(LOG_WARNING, "%s: revoked %llu row(s) of a reused refresh token family", __func__,
           (unsigned long long)mysql_stmt_affected_rows(stmt));
    mysql_stmt_close(stmt);
  }

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_REVOKE_FAMILY_REFRESH
#undef SQL_REVOKE_FAMILY_ACCESS
}

static OAuthExchangeStatus
sClaimRefreshToken(unsigned long row_id)
{
#define SQL_CLAIM_REFRESH_TOKEN "UPDATE oauth_refresh_tokens SET is_revoked = 1 WHERE refresh_tokens_id = ? AND is_revoked = 0"

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_CLAIM_REFRESH_TOKEN, sizeof(SQL_CLAIM_REFRESH_TOKEN) - 1));

  if (IS_EMPTY(stmt)) return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;

  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type = MYSQL_TYPE_LONG;
  bind_param[0].buffer      = &row_id;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  my_ulonglong affected = mysql_stmt_affected_rows(stmt);
  mysql_stmt_close(stmt);

  if (affected != 1) {
    syslog(LOG_WARNING, "%s: refresh token %lu was rotated by a concurrent request", __func__, row_id);

    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  return OAUTH_EXCHANGE_STATUS_OK;

#undef SQL_CLAIM_REFRESH_TOKEN
}

static OAuthExchangeStatus
sRestoreRefreshToken(unsigned long row_id)
{
#define SQL_RESTORE_REFRESH_TOKEN "UPDATE oauth_refresh_tokens SET is_revoked = 0 WHERE refresh_tokens_id = ? AND is_revoked = 1"

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_RESTORE_REFRESH_TOKEN, sizeof(SQL_RESTORE_REFRESH_TOKEN) - 1));

  if (IS_EMPTY(stmt)) return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;

  MYSQL_BIND bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type = MYSQL_TYPE_LONG;
  bind_param[0].buffer      = &row_id;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  mysql_stmt_close(stmt);

  return OAUTH_EXCHANGE_STATUS_OK;

#undef SQL_RESTORE_REFRESH_TOKEN
}

static OAuthExchangeStatus
sPersistRefreshFamily(unsigned long row_id, const char *family_id)
{
#define SQL_SET_REFRESH_FAMILY "UPDATE oauth_refresh_tokens SET family_id = ? WHERE refresh_tokens_id = ?"

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SET_REFRESH_FAMILY, sizeof(SQL_SET_REFRESH_FAMILY) - 1));

  if (IS_EMPTY(stmt)) return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;

  unsigned long family_len = strlen(family_id);

  MYSQL_BIND bind_param[2];
  memset(bind_param, 0, sizeof(bind_param));

  bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[0].buffer        = (char *)family_id;
  bind_param[0].buffer_length = family_len;
  bind_param[0].length        = &family_len;

  bind_param[1].buffer_type = MYSQL_TYPE_LONG;
  bind_param[1].buffer      = &row_id;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  mysql_stmt_close(stmt);

  return OAUTH_EXCHANGE_STATUS_OK;

#undef SQL_SET_REFRESH_FAMILY
}

static OAuthExchangeStatus
sStoreRotatedTokenPair(const RefreshTokenRow *row, const char *granted_json,
                       const char *access_token, const char *refresh_token,
                       const char *family_id, time_t access_expiry, time_t refresh_expiry)
{
#define SQL_INSERT_ROTATED_ACCESS \
  "INSERT INTO oauth_access_tokens (token, client_id, user_id, scopes, token_type, expires_at) " \
  "VALUES (?, ?, ?, ?, 'Bearer', NOW() + INTERVAL ? SECOND)"

#define SQL_INSERT_ROTATED_REFRESH \
  "INSERT INTO oauth_refresh_tokens (token, client_id, user_id, scopes, expires_at, access_token_id, family_id) " \
  "VALUES (?, ?, ?, ?, NOW() + INTERVAL ? SECOND, ?, ?)"

#define SQL_DELETE_ACCESS_TOKEN "DELETE FROM oauth_access_tokens WHERE access_token_id = ?"

  unsigned long access_len  = strlen(access_token);
  unsigned long refresh_len = strlen(refresh_token);
  unsigned long scopes_len  = strlen(granted_json);
  unsigned long family_len  = strlen(family_id);
  long long     access_ttl  = (long long)access_expiry;
  long long     refresh_ttl = (long long)refresh_expiry;

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_INSERT_ROTATED_ACCESS, sizeof(SQL_INSERT_ROTATED_ACCESS) - 1));

  if (IS_EMPTY(stmt)) return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;

  MYSQL_BIND access_params[5];
  memset(access_params, 0, sizeof(access_params));

  access_params[0].buffer_type   = MYSQL_TYPE_STRING;
  access_params[0].buffer        = (char *)access_token;
  access_params[0].buffer_length = access_len;
  access_params[0].length        = &access_len;

  access_params[1].buffer_type = MYSQL_TYPE_LONG;
  access_params[1].buffer      = (void *)&row->client_id;

  access_params[2].buffer_type = MYSQL_TYPE_LONG;
  access_params[2].buffer      = (void *)&row->user_id;

  access_params[3].buffer_type   = MYSQL_TYPE_STRING;
  access_params[3].buffer        = (char *)granted_json;
  access_params[3].buffer_length = scopes_len;
  access_params[3].length        = &scopes_len;

  access_params[4].buffer_type = MYSQL_TYPE_LONGLONG;
  access_params[4].buffer      = &access_ttl;

  if (mysql_stmt_bind_param(stmt, access_params) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: access token insert failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    sRestoreRefreshToken(row->row_id);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  mysql_stmt_close(stmt);

  unsigned long access_token_id = (unsigned long)h_last_insert_id_mariadb(THREAD_CONTEXT_DB_BACKEND);

  stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_INSERT_ROTATED_REFRESH, sizeof(SQL_INSERT_ROTATED_REFRESH) - 1));

  if (IS_EMPTY(stmt)) goto error_delete_access_token;

  MYSQL_BIND refresh_params[7];
  memset(refresh_params, 0, sizeof(refresh_params));

  refresh_params[0].buffer_type   = MYSQL_TYPE_STRING;
  refresh_params[0].buffer        = (char *)refresh_token;
  refresh_params[0].buffer_length = refresh_len;
  refresh_params[0].length        = &refresh_len;

  refresh_params[1].buffer_type = MYSQL_TYPE_LONG;
  refresh_params[1].buffer      = (void *)&row->client_id;

  refresh_params[2].buffer_type = MYSQL_TYPE_LONG;
  refresh_params[2].buffer      = (void *)&row->user_id;

  refresh_params[3].buffer_type   = MYSQL_TYPE_STRING;
  refresh_params[3].buffer        = (char *)granted_json;
  refresh_params[3].buffer_length = scopes_len;
  refresh_params[3].length        = &scopes_len;

  refresh_params[4].buffer_type = MYSQL_TYPE_LONGLONG;
  refresh_params[4].buffer      = &refresh_ttl;

  refresh_params[5].buffer_type = MYSQL_TYPE_LONG;
  refresh_params[5].buffer      = &access_token_id;

  refresh_params[6].buffer_type   = MYSQL_TYPE_STRING;
  refresh_params[6].buffer        = (char *)family_id;
  refresh_params[6].buffer_length = family_len;
  refresh_params[6].length        = &family_len;

  if (mysql_stmt_bind_param(stmt, refresh_params) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: refresh token insert failed: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    goto error_delete_access_token;
  }

  mysql_stmt_close(stmt);

  return OAUTH_EXCHANGE_STATUS_OK;

  error_delete_access_token:
  stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_DELETE_ACCESS_TOKEN, sizeof(SQL_DELETE_ACCESS_TOKEN) - 1));

  if (!IS_EMPTY(stmt)) {
    MYSQL_BIND delete_param[1];
    memset(delete_param, 0, sizeof(delete_param));
    delete_param[0].buffer_type = MYSQL_TYPE_LONG;
    delete_param[0].buffer      = &access_token_id;

    if (!mysql_stmt_bind_param(stmt, delete_param)) mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
  }

  sRestoreRefreshToken(row->row_id);

  return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;

#undef SQL_INSERT_ROTATED_ACCESS
#undef SQL_INSERT_ROTATED_REFRESH
#undef SQL_DELETE_ACCESS_TOKEN
}

/**
 * @brief Redeem a refresh token for a new pair, rotating the presented one.
 *
 * The token is bound to the client that was issued it: the row's client must be
 * the one that authenticates here, and a token presented after it has been
 * rotated revokes every member of its family (RFC 6819 §5.2.2.3).
 *
 * @param client_uuid[IN]     hyphenated UUID of the client redeeming
 * @param client_secret[IN]   optional secret; NULL or empty for a public client
 * @param refresh_token[IN]   the token presented
 * @param requested_scope[IN] optional narrower scope; NULL keeps the granted set
 * @param tokens_out[OUT]     receives the rotated pair on OAUTH_EXCHANGE_STATUS_OK
 * @return the outcome of the exchange; tokens_out is only meaningful on OK
 */
OAuthExchangeStatus
DbExchangeRefreshToken(const char *client_uuid, const char *client_secret,
                       const char *refresh_token, const char *requested_scope,
                       OAuthTokenPair *tokens_out)
{
  if (!IS_STR_LOADED(client_uuid) || !IS_STR_LOADED(refresh_token) || IS_EMPTY(tokens_out)) {
    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  memset(tokens_out, 0, sizeof *tokens_out);

  UFSRVResult *auth_res = DbOAuthClientAuthenticate(client_uuid, client_secret);

  if (!auth_res || _RESULT_CODE_EQUAL(auth_res, RESCODE_BACKEND_CONNECTION)) {
    syslog(LOG_ERR, "%s: client authentication could not be performed", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  if (!_RESULT_TYPE_SUCCESS(auth_res)) {
    syslog(LOG_WARNING, "%s: client '%s' did not authenticate", __func__, client_uuid);

    return OAUTH_EXCHANGE_STATUS_INVALID_CLIENT;
  }

  RefreshTokenRow row = {0};
  UFSRVResult    *row_res = sLookupRefreshTokenForExchange(refresh_token, &row);

  if (!row_res || _RESULT_CODE_EQUAL(row_res, RESCODE_BACKEND_CONNECTION) || _RESULT_CODE_EQUAL(row_res, RESCODE_BACKEND_COMMAND)) {
    syslog(LOG_ERR, "%s: refresh token lookup failed", __func__);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  if (!row.found) {
    syslog(LOG_DEBUG, "%s: presented refresh token is unknown", __func__);

    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  if (row.is_revoked) {
    syslog(LOG_WARNING, "%s: a rotated refresh token of client %lu was presented again", __func__, row.client_id);
    sRevokeRefreshTokenFamily(row.family_id);

    return OAUTH_EXCHANGE_STATUS_REPLAY;
  }

  if (!row.is_active) {
    syslog(LOG_DEBUG, "%s: refresh token expired", __func__);

    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  if (!IS_STR_LOADED(row.client_uuid) || strcmp(row.client_uuid, client_uuid) != 0) {
    syslog(LOG_WARNING, "%s: refresh token belongs to client '%s', not to '%s'", __func__,
           IS_STR_LOADED(row.client_uuid) ? row.client_uuid : "(none)", client_uuid);

    return OAUTH_EXCHANGE_STATUS_INVALID_GRANT;
  }

  char granted_json[4096] = {0};

  if (IS_STR_LOADED(requested_scope)) {
    char granted_space_list[4096] = {0};

    BuildScopeStringFromJson(row.scopes, granted_space_list, sizeof granted_space_list);

    if (!sRequestedScopesAreAllowed(requested_scope, granted_space_list)) {
      syslog(LOG_WARNING, "%s: a scope wider than the granted set was requested", __func__);

      return OAUTH_EXCHANGE_STATUS_INVALID_SCOPE;
    }

    BuildScopesJson(requested_scope, granted_json, sizeof granted_json);
  } else {
    snprintf(granted_json, sizeof granted_json, "%s", IS_STR_LOADED(row.scopes) ? row.scopes : "[]");
  }

  char family[CONFIGDEFAULT_OAUTH_REFRESH_FAMILY_SZ + 1] = {0};

  if (IS_STR_LOADED(row.family_id)) {
    snprintf(family, sizeof family, "%s", row.family_id);
  } else {
    GenerateSecureRandomHexed(family, CONFIGDEFAULT_OAUTH_REFRESH_FAMILY_SZ);

    if (sPersistRefreshFamily(row.row_id, family) != OAUTH_EXCHANGE_STATUS_OK) {
      syslog(LOG_ERR, "%s: could not record the refresh token family", __func__);

      return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
    }
  }

  OAuthExchangeStatus claim = sClaimRefreshToken(row.row_id);

  if (claim != OAUTH_EXCHANGE_STATUS_OK) return claim;

  char access_token[CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_MAX_SZ + 1] = {0};
  char access_store[CONFIGDEFAULT_OAUTH_TOKEN_SZ + 1] = {0};
  char rotated_refresh[CONFIGDEFAULT_OAUTH_TOKEN_SZ + 1] = {0};

  if (!sMintAccessToken(row.client_id, row.user_id, granted_json, false, NULL,
                        access_token, sizeof access_token, access_store, sizeof access_store)) {
    syslog(LOG_ERR, "%s: failed to mint the access token", __func__);
    sRestoreRefreshToken(row.row_id);

    return OAUTH_EXCHANGE_STATUS_BACKEND_ERROR;
  }

  GenerateSecureRandomHexed(rotated_refresh, CONFIGDEFAULT_OAUTH_TOKEN_SZ);

  OAuthExchangeStatus stored = sStoreRotatedTokenPair(&row, granted_json, access_store, rotated_refresh, family,
                                                      CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_EXPIRY,
                                                      CONFIGDEFAULT_OAUTH_REFRESH_TOKEN_EXPIRY);

  if (stored != OAUTH_EXCHANGE_STATUS_OK) {
    syslog(LOG_ERR, "%s: failed to persist the rotated pair", __func__);

    return stored;
  }

  snprintf(tokens_out->access_token,  sizeof tokens_out->access_token,  "%s", access_token);
  snprintf(tokens_out->refresh_token, sizeof tokens_out->refresh_token, "%s", rotated_refresh);
  snprintf(tokens_out->scopes,        sizeof tokens_out->scopes,        "%s", granted_json);

  tokens_out->client_id     = row.client_id;
  tokens_out->user_id       = row.user_id;
  tokens_out->access_expiry = CONFIGDEFAULT_OAUTH_ACCESS_TOKEN_EXPIRY;

  return OAUTH_EXCHANGE_STATUS_OK;
}
