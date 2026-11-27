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
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <mariadb/mysql.h>

#include "ufsrv_core/oauth/client_id_metadata.h"
#include "ufsrv_core/oauth/utils_oauth.h"
#include <ufsrv_core/system/utils_json.h>
#include "uflib/utils.h"
#include "ufsrv_core/delegator_sessions/thread_context_type.h"
#include <uflib/db/db_sql.h>

extern __thread ThreadContext ufsrv_thread_context;

#define CIMD_SCHEME_HTTPS            "https://"
#define CIMD_REDIRECTS_MAX_JSON_SZ   OAUTH_CIMD_REDIRECTS_BUF_SZ
#define CIMD_THROTTLE_ENTRIES        64
#define CIMD_DEFAULT_HTTPS_PORT      "443"
#define CIMD_GRANT_AUTHORIZATION_CODE "authorization_code"
#define CIMD_GRANT_CLIENT_CREDENTIALS "client_credentials"
#define CIMD_GRANT_REFRESH_TOKEN      "refresh_token"

typedef struct CimdFailureEntry {
  char   url[CONFIGDEFAULT_OAUTH_CIMD_URL_MAX_SZ + 1];
  time_t failed_at;
} CimdFailureEntry;

typedef struct CimdResponseBuffer {
  char  *body;
  size_t body_sz;
  size_t length;
  bool   overflowed;
  long   cache_ttl;
} CimdResponseBuffer;

static CimdFailureEntry s_failure_throttle[CIMD_THROTTLE_ENTRIES];
static pthread_mutex_t  s_failure_throttle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t   s_curl_once              = PTHREAD_ONCE_INIT;

static void
sCurlGlobalInit(void)
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

static bool
sPortIsValid(const char *port, size_t port_len)
{
  if (port_len == 0 || port_len > 5) return false;

  unsigned long value = 0;

  for (size_t i = 0; i < port_len; i++) {
    if (port[i] < '0' || port[i] > '9') return false;
    value = (value * 10) + (unsigned long)(port[i] - '0');
  }

  return value > 0 && value <= 65535;
}

static bool
sHostIsIpLiteral(const char *host, size_t host_len)
{
  if (host_len == 0 || host_len >= 64) return false;

  char buffer[64];
  memcpy(buffer, host, host_len);
  buffer[host_len] = '\0';

  struct in_addr  ipv4;
  struct in6_addr ipv6;

  return inet_pton(AF_INET, buffer, &ipv4) == 1 || inet_pton(AF_INET6, buffer, &ipv6) == 1;
}

static bool
sAuthorityIsValid(const char *authority, size_t authority_len)
{
  if (authority_len == 0 || authority_len > 255) return false;

  for (size_t i = 0; i < authority_len; i++) {
    unsigned char c = (unsigned char)authority[i];
    if (c <= 0x20 || c == 0x7f || c == '@' || c == '/' || c == '?' || c == '#' || c == '[' || c == ']') return false;
  }

  const char *colon = memchr(authority, ':', authority_len);
  size_t      host_len = colon ? (size_t)(colon - authority) : authority_len;

  if (host_len == 0 || host_len > 253) return false;

  bool previous_dot = false;

  for (size_t i = 0; i < host_len; i++) {
    char c = authority[i];

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
      previous_dot = false;
      continue;
    }

    if (c == '.' && !previous_dot && i != 0 && i != host_len - 1) {
      previous_dot = true;
      continue;
    }

    return false;
  }

  if (sHostIsIpLiteral(authority, host_len)) return false;

  if (colon) {
    size_t port_len = authority_len - host_len - 1;
    if (!sPortIsValid(colon + 1, port_len)) return false;
  }

  return true;
}

bool
OAuthClientMetadataUrlIsValid(const char *url)
{
  if (!IS_STR_LOADED(url)) return false;

  size_t url_len = strlen(url);

  if (url_len <= sizeof(CIMD_SCHEME_HTTPS) - 1 || url_len > CONFIGDEFAULT_OAUTH_CIMD_URL_MAX_SZ) return false;

  for (size_t i = 0; i < url_len; i++) {
    unsigned char c = (unsigned char)url[i];
    if (c <= 0x20 || c == 0x7f || c == '"' || c == '\\' || c == '<' || c == '>' || c == '^' || c == '`' || c == '{' || c == '|' || c == '}') return false;
  }

  if (strncasecmp(url, CIMD_SCHEME_HTTPS, sizeof(CIMD_SCHEME_HTTPS) - 1) != 0) return false;
  if (strchr(url, '#') || strchr(url, '?')) return false;

  const char *authority     = url + (sizeof(CIMD_SCHEME_HTTPS) - 1);
  const char *path          = strchr(authority, '/');

  if (IS_EMPTY(path) || path[1] == '\0') return false;
  if (!sAuthorityIsValid(authority, (size_t)(path - authority))) return false;

  return true;
}

bool
OAuthClientIdentifierIsUrl(const char *identifier)
{
  if (!IS_STR_LOADED(identifier)) return false;

  return strncasecmp(identifier, "https://", sizeof("https://") - 1) == 0 ||
         strncasecmp(identifier, "http://", sizeof("http://") - 1) == 0;
}

bool
OAuthClientMetadataAddressIsPublic(uint32_t host_order_address)
{
  unsigned int first  = (unsigned int)((host_order_address >> 24) & 0xff);
  unsigned int second = (unsigned int)((host_order_address >> 16) & 0xff);
  unsigned int third  = (unsigned int)((host_order_address >> 8) & 0xff);

  if (first == 0 || first == 10 || first == 127) return false;
  if (first == 169 && second == 254) return false;
  if (first == 172 && second >= 16 && second <= 31) return false;
  if (first == 192 && second == 168) return false;
  if (first == 192 && second == 0 && (third == 0 || third == 2)) return false;
  if (first == 192 && second == 88 && third == 99) return false;
  if (first == 198 && (second == 18 || second == 19)) return false;
  if (first == 198 && second == 51 && third == 100) return false;
  if (first == 203 && second == 0 && third == 113) return false;
  if (first == 100 && second >= 64 && second <= 127) return false;
  if (first >= 224) return false;

  return true;
}

static bool
sAddressIsPublic(const struct sockaddr_in *address)
{
  return OAuthClientMetadataAddressIsPublic(ntohl(address->sin_addr.s_addr));
}

static bool
sHostResolvesToPublicAddress(const char *host, char *resolved_out, size_t resolved_sz)
{
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *results = NULL;

  if (getaddrinfo(host, NULL, &hints, &results) != 0 || IS_EMPTY(results)) {
    if (results) freeaddrinfo(results);
    return false;
  }

  bool found = false;

  for (struct addrinfo *entry = results; entry; entry = entry->ai_next) {
    const struct sockaddr_in *address = (const struct sockaddr_in *)entry->ai_addr;

    if (IS_EMPTY(address)) continue;

    if (!sAddressIsPublic(address)) {
      syslog(LOG_WARNING, "%s: '%s' resolves to a non-public address", __func__, host);
      found = false;
      break;
    }

    if (!found) {
      char buffer[INET_ADDRSTRLEN] = {0};

      if (inet_ntop(AF_INET, &address->sin_addr, buffer, sizeof buffer)) {
        snprintf(resolved_out, resolved_sz, "%s", buffer);
        found = true;
      }
    }
  }

  freeaddrinfo(results);

  return found;
}

static bool
sUrlHostAndPort(const char *url, char *host_out, size_t host_sz, char *port_out, size_t port_sz)
{
  const char *authority = url + (sizeof(CIMD_SCHEME_HTTPS) - 1);
  const char *path      = strchr(authority, '/');

  if (IS_EMPTY(path)) return false;

  size_t      authority_len = (size_t)(path - authority);
  const char *colon         = memchr(authority, ':', authority_len);
  size_t      host_len      = colon ? (size_t)(colon - authority) : authority_len;

  if (host_len == 0 || host_len >= host_sz) return false;

  memcpy(host_out, authority, host_len);
  host_out[host_len] = '\0';

  if (colon) {
    size_t port_len = authority_len - host_len - 1;
    if (port_len == 0 || port_len >= port_sz) return false;
    memcpy(port_out, colon + 1, port_len);
    port_out[port_len] = '\0';
  } else {
    snprintf(port_out, port_sz, "%s", CIMD_DEFAULT_HTTPS_PORT);
  }

  return true;
}

static size_t
sMetadataBodyAppend(void *contents, size_t size, size_t nmemb, void *userp)
{
  CimdResponseBuffer *buffer = (CimdResponseBuffer *)userp;
  size_t              chunk  = size * nmemb;

  if (chunk == 0) return 0;

  if (buffer->length + chunk + 1 > buffer->body_sz) {
    buffer->overflowed = true;
    return 0;
  }

  memcpy(buffer->body + buffer->length, contents, chunk);
  buffer->length += chunk;
  buffer->body[buffer->length] = '\0';

  return chunk;
}

static size_t
sMetadataHeaderCapture(char *buffer, size_t size, size_t nitems, void *userp)
{
  CimdResponseBuffer *response = (CimdResponseBuffer *)userp;
  size_t              line_sz  = size * nitems;
  size_t              name_sz  = sizeof("cache-control:") - 1;

  if (line_sz <= name_sz || line_sz >= 512) return line_sz;

  char line[512];
  memcpy(line, buffer, line_sz);
  line[line_sz] = '\0';

  if (strncasecmp(line, "cache-control:", name_sz) != 0) return line_sz;

  const char *max_age = strcasestr(line + name_sz, "max-age=");

  if (IS_EMPTY(max_age)) return line_sz;

  long ttl = strtol(max_age + sizeof("max-age=") - 1, NULL, 10);

  if (ttl < CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL_MIN) ttl = CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL_MIN;
  if (ttl > CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL_MAX) ttl = CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL_MAX;

  response->cache_ttl = ttl;

  return line_sz;
}

static bool
sFetchMetadataDocument(const char *url, char *body, size_t body_sz, long *cache_ttl_out)
{
  char host[256] = {0};
  char port[8]   = {0};

  if (!sUrlHostAndPort(url, host, sizeof host, port, sizeof port)) {
    syslog(LOG_WARNING, "%s: cannot split '%s' into host and port", __func__, url);
    return false;
  }

  char resolved[INET_ADDRSTRLEN] = {0};

  if (!sHostResolvesToPublicAddress(host, resolved, sizeof resolved)) {
    syslog(LOG_WARNING, "%s: refusing to fetch '%s': host does not resolve to a public address", __func__, url);
    return false;
  }

  char pin_entry[320];
  int  pin_written = snprintf(pin_entry, sizeof pin_entry, "%s:%s:%s", host, port, resolved);

  if (pin_written < 0 || (size_t)pin_written >= sizeof pin_entry) return false;

  pthread_once(&s_curl_once, sCurlGlobalInit);

  CURL *curl = curl_easy_init();

  if (IS_EMPTY(curl)) {
    syslog(LOG_ERR, "%s: curl_easy_init() failed", __func__);
    return false;
  }

  struct curl_slist *headers    = NULL;
  struct curl_slist *resolve    = NULL;
  bool               fetched    = false;

  headers = curl_slist_append(headers, "Accept: application/json");
  resolve = curl_slist_append(resolve, pin_entry);

  if (IS_EMPTY(headers) || IS_EMPTY(resolve)) goto cleanup;

  CimdResponseBuffer response;
  memset(&response, 0, sizeof response);
  response.body       = body;
  response.body_sz    = body_sz < CONFIGDEFAULT_OAUTH_CIMD_DOCUMENT_MAX_SZ ? body_sz : CONFIGDEFAULT_OAUTH_CIMD_DOCUMENT_MAX_SZ;
  response.cache_ttl  = CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL;
  body[0]             = '\0';

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)CONFIGDEFAULT_OAUTH_CIMD_FETCH_CONNECT_TIMEOUT);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)CONFIGDEFAULT_OAUTH_CIMD_FETCH_TIMEOUT);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ufsrvapi-oauth-client-metadata/1");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sMetadataBodyAppend);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, sMetadataHeaderCapture);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&response);

  CURLcode curl_code = curl_easy_perform(curl);

  if (curl_code != CURLE_OK) {
    syslog(LOG_WARNING, "%s: fetch of '%s' failed: '%s'", __func__, url, curl_easy_strerror(curl_code));
    goto cleanup;
  }

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code != 200) {
    syslog(LOG_WARNING, "%s: '%s' answered %ld", __func__, url, http_code);
    goto cleanup;
  }

  if (response.overflowed) {
    syslog(LOG_WARNING, "%s: document at '%s' exceeds %d bytes", __func__, url, CONFIGDEFAULT_OAUTH_CIMD_DOCUMENT_MAX_SZ);
    goto cleanup;
  }

  if (response.length == 0) {
    syslog(LOG_WARNING, "%s: document at '%s' is empty", __func__, url);
    goto cleanup;
  }

  *cache_ttl_out = response.cache_ttl;
  fetched        = true;

  cleanup:
  if (headers) curl_slist_free_all(headers);
  if (resolve) curl_slist_free_all(resolve);
  curl_easy_cleanup(curl);

  return fetched;
}

static bool
sRedirectUriIsAcceptable(const char *uri)
{
  if (!IS_STR_LOADED(uri)) return false;

  size_t uri_len = strlen(uri);

  if (uri_len < 10 || uri_len > OAUTH_CIMD_REDIRECT_URI_MAX_SZ) return false;

  for (size_t i = 0; i < uri_len; i++) {
    unsigned char c = (unsigned char)uri[i];
    if (c <= 0x20 || c == 0x7f || c == '"' || c == '\\' || c == '<' || c == '>' || c == '`') return false;
  }

  if (strchr(uri, '#')) return false;

  if (strncasecmp(uri, CIMD_SCHEME_HTTPS, sizeof(CIMD_SCHEME_HTTPS) - 1) == 0) {
    const char *host = uri + (sizeof(CIMD_SCHEME_HTTPS) - 1);
    const char *end  = strchr(host, '/');

    if (IS_EMPTY(end) || end == host) return false;
    if (memchr(host, '@', (size_t)(end - host))) return false;

    return true;
  }

  if (strncasecmp(uri, "http://", sizeof("http://") - 1) != 0) return false;

  const char *host = uri + (sizeof("http://") - 1);
  const char *end  = strchr(host, '/');

  if (IS_EMPTY(end)) return false;

  size_t host_len = (size_t)(end - host);

  if (host_len == sizeof("127.0.0.1") - 1 && strncmp(host, "127.0.0.1", host_len) == 0) return true;
  if (host_len == sizeof("[::1]") - 1 && strncmp(host, "[::1]", host_len) == 0) return true;
  if (host_len == sizeof("localhost") - 1 && strncasecmp(host, "localhost", host_len) == 0) return true;

  return false;
}

static bool
sGrantTypeIsSupported(const char *grant_type)
{
  return strcmp(grant_type, CIMD_GRANT_AUTHORIZATION_CODE) == 0 ||
         strcmp(grant_type, CIMD_GRANT_CLIENT_CREDENTIALS) == 0 ||
         strcmp(grant_type, CIMD_GRANT_REFRESH_TOKEN) == 0;
}

bool
OAuthClientMetadataDocumentValidate(const char *url, const char *document, OAuthClientMetadata *metadata_out)
{
  if (!IS_STR_LOADED(url) || !IS_STR_LOADED(document) || IS_EMPTY(metadata_out)) return false;

  if (!OAuthClientMetadataUrlIsValid(url)) {
    syslog(LOG_WARNING, "%s: '%s' is not a valid client identifier URL", __func__, url);
    return false;
  }

  if (strlen(document) > CONFIGDEFAULT_OAUTH_CIMD_DOCUMENT_MAX_SZ) return false;

  memset(metadata_out, 0, sizeof *metadata_out);
  metadata_out->cache_ttl = CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL;

  json_tokener *tokener = json_tokener_new();

  if (IS_EMPTY(tokener)) return false;

  json_object *root = json_tokener_parse_ex(tokener, document, (int)strlen(document));
  enum json_tokener_error parse_error = json_tokener_get_error(tokener);

  json_tokener_free(tokener);

  if (parse_error != json_tokener_success || IS_EMPTY(root) || !json_object_is_type(root, json_type_object)) {
    syslog(LOG_WARNING, "%s: document at '%s' is not a JSON object", __func__, url);
    if (root) json_object_put(root);
    return false;
  }

  bool valid = false;

  json_object *declared_id = json__get(root, "client_id");

  if (IS_EMPTY(declared_id) || !json_object_is_type(declared_id, json_type_string)) {
    syslog(LOG_WARNING, "%s: document at '%s' does not declare client_id", __func__, url);
    goto cleanup;
  }

  if (strcmp(json_object_get_string(declared_id), url) != 0) {
    syslog(LOG_WARNING, "%s: document at '%s' declares a different client_id", __func__, url);
    goto cleanup;
  }

  if (IS_PRESENT(json__get(root, "client_secret")) || IS_PRESENT(json__get(root, "private_key")) ||
      IS_PRESENT(json__get(root, "client_assertion"))) {
    syslog(LOG_WARNING, "%s: document at '%s' carries secret or key material", __func__, url);
    goto cleanup;
  }

  json_object *auth_method = json__get(root, "token_endpoint_auth_method");

  if (IS_PRESENT(auth_method)) {
    if (!json_object_is_type(auth_method, json_type_string) ||
        strcmp(json_object_get_string(auth_method), "none") != 0) {
      syslog(LOG_WARNING, "%s: document at '%s' declares a shared-secret authentication method", __func__, url);
      goto cleanup;
    }
  }

  json_object *declared_name = json__get(root, "client_name");

  if (IS_EMPTY(declared_name) || !json_object_is_type(declared_name, json_type_string)) {
    syslog(LOG_WARNING, "%s: document at '%s' does not declare client_name", __func__, url);
    goto cleanup;
  }

  const char *name = json_object_get_string(declared_name);
  size_t      name_len = IS_STR_LOADED(name) ? strlen(name) : 0;

  if (name_len < CONFIGDEFAULT_OAUTH_CLIENT_NAME_MIN_SZ || name_len > 100) {
    syslog(LOG_WARNING, "%s: document at '%s' declares an out-of-range client_name", __func__, url);
    goto cleanup;
  }

  json_object *declared_redirects = json__get(root, "redirect_uris");

  if (IS_EMPTY(declared_redirects) || !json_object_is_type(declared_redirects, json_type_array)) {
    syslog(LOG_WARNING, "%s: document at '%s' does not declare redirect_uris", __func__, url);
    goto cleanup;
  }

  size_t redirects_count = json_object_array_length(declared_redirects);

  if (redirects_count == 0 || redirects_count > CONFIGDEFAULT_OAUTH_AUTH_REDIRECTS_MAX) {
    syslog(LOG_WARNING, "%s: document at '%s' declares %zu redirect_uris", __func__, url, redirects_count);
    goto cleanup;
  }

  json_object *redirects_out = json_object_new_array();

  if (IS_EMPTY(redirects_out)) goto cleanup;

  for (size_t i = 0; i < redirects_count; i++) {
    json_object *entry = json_object_array_get_idx(declared_redirects, i);

    if (IS_EMPTY(entry) || !json_object_is_type(entry, json_type_string)) {
      json_object_put(redirects_out);
      goto cleanup;
    }

    const char *uri = json_object_get_string(entry);

    if (!sRedirectUriIsAcceptable(uri)) {
      syslog(LOG_WARNING, "%s: document at '%s' declares an unacceptable redirect_uri", __func__, url);
      json_object_put(redirects_out);
      goto cleanup;
    }

    json_object_array_add(redirects_out, json_object_new_string(uri));
  }

  json_object *declared_grants = json__get(root, "grant_types");
  json_object *grants_out      = json_object_new_array();

  if (IS_EMPTY(grants_out)) {
    json_object_put(redirects_out);
    goto cleanup;
  }

  if (IS_PRESENT(declared_grants)) {
    if (!json_object_is_type(declared_grants, json_type_array) || json_object_array_length(declared_grants) == 0) {
      syslog(LOG_WARNING, "%s: document at '%s' declares malformed grant_types", __func__, url);
      json_object_put(grants_out);
      json_object_put(redirects_out);
      goto cleanup;
    }

    for (size_t i = 0; i < json_object_array_length(declared_grants); i++) {
      json_object *entry = json_object_array_get_idx(declared_grants, i);

      if (IS_EMPTY(entry) || !json_object_is_type(entry, json_type_string)) {
        json_object_put(grants_out);
        json_object_put(redirects_out);
        goto cleanup;
      }

      const char *grant_type = json_object_get_string(entry);

      if (!sGrantTypeIsSupported(grant_type)) {
        syslog(LOG_WARNING, "%s: document at '%s' declares unsupported grant '%s'", __func__, url, grant_type);
        json_object_put(grants_out);
        json_object_put(redirects_out);
        goto cleanup;
      }

      json_object_array_add(grants_out, json_object_new_string(grant_type));
    }
  } else {
    json_object_array_add(grants_out, json_object_new_string(CIMD_GRANT_AUTHORIZATION_CODE));
  }

  const char *redirects_json = json_object_to_json_string_ext(redirects_out, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
  const char *grants_json    = json_object_to_json_string_ext(grants_out, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);

  if (strlen(redirects_json) >= sizeof(metadata_out->redirect_uris) || strlen(grants_json) > OAUTH_CIMD_GRANT_TYPES_MAX_SZ) {
    json_object_put(grants_out);
    json_object_put(redirects_out);
    goto cleanup;
  }

  snprintf(metadata_out->url, sizeof metadata_out->url, "%s", url);
  snprintf(metadata_out->name, sizeof metadata_out->name, "%s", name);
  snprintf(metadata_out->redirect_uris, sizeof metadata_out->redirect_uris, "%s", redirects_json);
  snprintf(metadata_out->grant_types, sizeof metadata_out->grant_types, "%s", grants_json);

  valid = true;

  json_object_put(grants_out);
  json_object_put(redirects_out);

  cleanup:
  json_object_put(root);

  return valid;
}

static json_object *
sParseJsonArray(const char *json)
{
  if (!IS_STR_LOADED(json)) return NULL;

  json_object *root = json_tokener_parse(json);

  if (IS_EMPTY(root) || !json_object_is_type(root, json_type_array)) {
    if (root) json_object_put(root);
    return NULL;
  }

  return root;
}

bool
OAuthClientRedirectUriIsDeclared(const char *redirect_uris_json, const char *redirect_uri)
{
  if (!IS_STR_LOADED(redirect_uri)) return false;

  json_object *redirects = sParseJsonArray(redirect_uris_json);

  if (IS_EMPTY(redirects)) return false;

  bool declared = false;

  for (size_t i = 0; i < json_object_array_length(redirects); i++) {
    json_object *entry = json_object_array_get_idx(redirects, i);

    if (IS_EMPTY(entry) || !json_object_is_type(entry, json_type_string)) continue;

    if (strcmp(json_object_get_string(entry), redirect_uri) == 0) {
      declared = true;
      break;
    }
  }

  json_object_put(redirects);

  return declared;
}

bool
OAuthClientGrantTypeIsAllowed(const char *grant_types_json, const char *grant_type)
{
  if (!IS_STR_LOADED(grant_type)) return false;

  json_object *grants = sParseJsonArray(grant_types_json);

  if (IS_EMPTY(grants)) return false;

  bool allowed = false;

  for (size_t i = 0; i < json_object_array_length(grants); i++) {
    json_object *entry = json_object_array_get_idx(grants, i);

    if (IS_EMPTY(entry) || !json_object_is_type(entry, json_type_string)) continue;

    if (strcmp(json_object_get_string(entry), grant_type) == 0) {
      allowed = true;
      break;
    }
  }

  json_object_put(grants);

  return allowed;
}

static bool
sFetchAttemptIsAllowed(const char *url)
{
  time_t now   = time(NULL);
  bool   allow = true;

  pthread_mutex_lock(&s_failure_throttle_mutex);

  for (size_t i = 0; i < CIMD_THROTTLE_ENTRIES; i++) {
    if (s_failure_throttle[i].url[0] != '\0' && strcmp(s_failure_throttle[i].url, url) == 0) {
      if (now - s_failure_throttle[i].failed_at < CONFIGDEFAULT_OAUTH_CIMD_FAILURE_THROTTLE) allow = false;
      break;
    }
  }

  pthread_mutex_unlock(&s_failure_throttle_mutex);

  return allow;
}

static void
sRecordFetchOutcome(const char *url, bool succeeded)
{
  time_t now = time(NULL);

  pthread_mutex_lock(&s_failure_throttle_mutex);

  size_t slot = CIMD_THROTTLE_ENTRIES;

  for (size_t i = 0; i < CIMD_THROTTLE_ENTRIES; i++) {
    if (s_failure_throttle[i].url[0] == '\0') { slot = i; break; }
  }

  if (slot == CIMD_THROTTLE_ENTRIES) {
    time_t oldest = 0;
    slot = 0;

    for (size_t i = 0; i < CIMD_THROTTLE_ENTRIES; i++) {
      if (oldest == 0 || s_failure_throttle[i].failed_at < oldest) {
        oldest = s_failure_throttle[i].failed_at;
        slot   = i;
      }
    }
  }

  snprintf(s_failure_throttle[slot].url, sizeof s_failure_throttle[slot].url, "%s", url);
  s_failure_throttle[slot].failed_at = succeeded ? 0 : now;

  if (succeeded) s_failure_throttle[slot].url[0] = '\0';

  pthread_mutex_unlock(&s_failure_throttle_mutex);
}

static UFSRVResult *
sLookupClientByUrl(const char *url, bool *is_fresh_out, OAuthClient *client_out, unsigned long *client_db_id_out)
{
#define SQL_SELECT_CLIENT_BY_URL \
  "SELECT client_id, uuid_serialised, user_id, name, redirect_uris, grant_types, client_type, " \
  "(metadata_expires_at IS NOT NULL AND metadata_expires_at > UTC_TIMESTAMP()) " \
  "FROM oauth_clients WHERE client_id_url = ? AND is_active = 1"

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_SELECT_CLIENT_BY_URL, sizeof(SQL_SELECT_CLIENT_BY_URL) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long url_len = strlen(url);
  MYSQL_BIND    bind_param[1];
  memset(bind_param, 0, sizeof(bind_param));
  bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[0].buffer        = (char *)url;
  bind_param[0].buffer_length = url_len;
  bind_param[0].length        = &url_len;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long db_client_id = 0;
  unsigned long db_user_id   = 0;

  char          db_uuid[37]                                                                    = {0};
  char          db_name[CONFIGDEFAULT_OAUTH_CLIENT_NAME_MAX_SZ + 1]                            = {0};
  char          db_redirects[CIMD_REDIRECTS_MAX_JSON_SZ]                                       = {0};
  char          db_grants[OAUTH_CIMD_GRANT_TYPES_MAX_SZ + 1]                                   = {0};
  char          db_type[16]                                                                    = {0};

  unsigned long uuid_len      = 0;
  unsigned long name_len      = 0;
  unsigned long redirects_len = 0;
  unsigned long grants_len    = 0;
  unsigned long type_len      = 0;

  my_bool is_null_uuid      = 0;
  my_bool is_null_name      = 0;
  my_bool is_null_redirects = 0;
  my_bool is_null_grants    = 0;
  my_bool is_null_type      = 0;
  my_bool is_fresh          = 0;

  MYSQL_BIND bind_result[8];
  memset(bind_result, 0, sizeof(bind_result));

  bind_result[0].buffer_type = MYSQL_TYPE_LONG;
  bind_result[0].buffer      = &db_client_id;

  bind_result[1].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[1].buffer        = db_uuid;
  bind_result[1].buffer_length = sizeof(db_uuid) - 1;
  bind_result[1].length        = &uuid_len;
  bind_result[1].is_null       = &is_null_uuid;

  bind_result[2].buffer_type = MYSQL_TYPE_LONG;
  bind_result[2].buffer      = &db_user_id;

  bind_result[3].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[3].buffer        = db_name;
  bind_result[3].buffer_length = sizeof(db_name) - 1;
  bind_result[3].length        = &name_len;
  bind_result[3].is_null       = &is_null_name;

  bind_result[4].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[4].buffer        = db_redirects;
  bind_result[4].buffer_length = sizeof(db_redirects) - 1;
  bind_result[4].length        = &redirects_len;
  bind_result[4].is_null       = &is_null_redirects;

  bind_result[5].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[5].buffer        = db_grants;
  bind_result[5].buffer_length = sizeof(db_grants) - 1;
  bind_result[5].length        = &grants_len;
  bind_result[5].is_null       = &is_null_grants;

  bind_result[6].buffer_type   = MYSQL_TYPE_STRING;
  bind_result[6].buffer        = db_type;
  bind_result[6].buffer_length = sizeof(db_type) - 1;
  bind_result[6].length        = &type_len;
  bind_result[6].is_null       = &is_null_type;

  bind_result[7].buffer_type = MYSQL_TYPE_TINY;
  bind_result[7].buffer      = &is_fresh;

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

  *client_db_id_out = db_client_id;
  *is_fresh_out     = is_fresh != 0;

  if (IS_PRESENT(client_out)) {
    client_out->user_id                = db_user_id;
    client_out->client_type            = (!is_null_type && strcmp(db_type, "confidential") == 0) ? CONFIDENTIAL : PUBLIC;
    client_out->client_name.by_ref     = strdup(is_null_name ? "" : db_name);
    client_out->redirect_uris          = strdup(is_null_redirects ? "[]" : db_redirects);
    client_out->grant_types_json       = strdup(is_null_grants ? "[]" : db_grants);

    if (!is_null_uuid) {
      snprintf(client_out->client_uuid.serialised.by_value, sizeof client_out->client_uuid.serialised.by_value, "%s", db_uuid);
    }
  }

  THREAD_CONTEXT_RETURN_RESULT_SUCCESS(EMPTY_USERDATA, RESCODE_BACKEND_DATA)

#undef SQL_SELECT_CLIENT_BY_URL
}

static UFSRVResult *
sStoreClientMetadata(const OAuthClientMetadata *metadata, long cache_ttl, OAuthClient *client_out, unsigned long *client_db_id_out)
{
#define SQL_UPSERT_CLIENT_METADATA \
  "INSERT INTO oauth_clients (user_id, client_uuid, secret, name, grant_types, allowed_scopes, redirect_uris, client_type, client_id_url, metadata_fetched_at, metadata_expires_at) " \
  "VALUES (0, unhex(replace(?, '-', '')), '', ?, ?, ?, ?, 'public', ?, UTC_TIMESTAMP(), DATE_ADD(UTC_TIMESTAMP(), INTERVAL ? SECOND)) " \
  "ON DUPLICATE KEY UPDATE name = VALUES(name), grant_types = VALUES(grant_types), " \
  "redirect_uris = VALUES(redirect_uris), metadata_fetched_at = VALUES(metadata_fetched_at), " \
  "metadata_expires_at = VALUES(metadata_expires_at), is_active = 1"

  UUID4_STATE_T state;
  UUID4_T       uuid;
  char          uuid_serialised[37] = {0};

  uuid4_seed(&state);
  uuid4_gen(&state, &uuid);

  if (!uuid4_to_s(uuid, uuid_serialised, (int)sizeof uuid_serialised)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  MYSQL_STMT *stmt = AS_MARIADB_STATEMENT(h_statement_prepare(THREAD_CONTEXT_DB_BACKEND, SQL_UPSERT_CLIENT_METADATA, sizeof(SQL_UPSERT_CLIENT_METADATA) - 1));

  if (IS_EMPTY(stmt)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  unsigned long uuid_len        = strlen(uuid_serialised);
  unsigned long name_len        = strlen(metadata->name);
  unsigned long grants_len      = strlen(metadata->grant_types);
  unsigned long scopes_len      = sizeof(CONFIGDEFAULT_OAUTH_PUBLIC_CLIENT_ALLOWED_SCOPES) - 1;
  unsigned long redirects_len   = strlen(metadata->redirect_uris);
  unsigned long url_len         = strlen(metadata->url);
  long          ttl             = cache_ttl;

  MYSQL_BIND bind_param[7];
  memset(bind_param, 0, sizeof(bind_param));

  bind_param[0].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[0].buffer        = uuid_serialised;
  bind_param[0].buffer_length = uuid_len;
  bind_param[0].length        = &uuid_len;

  bind_param[1].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[1].buffer        = (char *)metadata->name;
  bind_param[1].buffer_length = name_len;
  bind_param[1].length        = &name_len;

  bind_param[2].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[2].buffer        = (char *)metadata->grant_types;
  bind_param[2].buffer_length = grants_len;
  bind_param[2].length        = &grants_len;

  bind_param[3].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[3].buffer        = (char *)CONFIGDEFAULT_OAUTH_PUBLIC_CLIENT_ALLOWED_SCOPES;
  bind_param[3].buffer_length = scopes_len;
  bind_param[3].length        = &scopes_len;

  bind_param[4].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[4].buffer        = (char *)metadata->redirect_uris;
  bind_param[4].buffer_length = redirects_len;
  bind_param[4].length        = &redirects_len;

  bind_param[5].buffer_type   = MYSQL_TYPE_STRING;
  bind_param[5].buffer        = (char *)metadata->url;
  bind_param[5].buffer_length = url_len;
  bind_param[5].length        = &url_len;

  bind_param[6].buffer_type = MYSQL_TYPE_LONG;
  bind_param[6].buffer      = &ttl;

  if (mysql_stmt_bind_param(stmt, bind_param) || mysql_stmt_execute(stmt)) {
    syslog(LOG_ERR, "%s: %s", __func__, mysql_stmt_error(stmt));
    mysql_stmt_close(stmt);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_CONNECTION)
  }

  mysql_stmt_close(stmt);

  bool is_fresh = false;

  return sLookupClientByUrl(metadata->url, &is_fresh, client_out, client_db_id_out);

#undef SQL_UPSERT_CLIENT_METADATA
}

UFSRVResult *
OAuthClientMetadataRegister(const char *url, const char *document, long cache_ttl, OAuthClient *client_out, unsigned long *client_db_id_out)
{
  if (IS_EMPTY(client_out) || IS_EMPTY(client_db_id_out)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  OAuthClientMetadata metadata;

  if (!OAuthClientMetadataDocumentValidate(url, document, &metadata)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  if (IS_PRESENT(client_out->client_name.by_ref)) free(client_out->client_name.by_ref);
  if (IS_PRESENT(client_out->redirect_uris))      free(client_out->redirect_uris);
  if (IS_PRESENT(client_out->grant_types_json))   free(client_out->grant_types_json);

  memset(client_out, 0, sizeof *client_out);

  return sStoreClientMetadata(&metadata, cache_ttl, client_out, client_db_id_out);
}

UFSRVResult *
OAuthClientResolveIdentifier(const char *identifier, OAuthClient *client_out, unsigned long *client_db_id_out)
{
  if (!IS_STR_LOADED(identifier) || IS_EMPTY(client_out) || IS_EMPTY(client_db_id_out)) {
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  if (!OAuthClientIdentifierIsUrl(identifier)) {
    UFSRVResult *lookup_res = DbOAuthClientLookupByUuid(identifier, client_out, client_db_id_out);

    if (!_RESULT_TYPE_SUCCESS(lookup_res)) return lookup_res;

    snprintf(client_out->client_uuid.serialised.by_value, sizeof client_out->client_uuid.serialised.by_value, "%s", identifier);

    return lookup_res;
  }

  if (!OAuthClientMetadataUrlIsValid(identifier)) {
    syslog(LOG_WARNING, "%s: '%s' is not a valid client identifier URL", __func__, identifier);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  bool         is_fresh    = false;
  UFSRVResult *cached_res  = sLookupClientByUrl(identifier, &is_fresh, client_out, client_db_id_out);

  if (_RESULT_CODE_EQUAL(cached_res, RESCODE_BACKEND_CONNECTION)) return cached_res;

  if (_RESULT_TYPE_SUCCESS(cached_res) && is_fresh) return cached_res;

  if (!sFetchAttemptIsAllowed(identifier)) {
    syslog(LOG_WARNING, "%s: '%s' failed to fetch recently; not retrying yet", __func__, identifier);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  char document[CONFIGDEFAULT_OAUTH_CIMD_DOCUMENT_MAX_SZ + 1] = {0};
  long cache_ttl = CONFIGDEFAULT_OAUTH_CIMD_CACHE_TTL;

  if (!sFetchMetadataDocument(identifier, document, sizeof document, &cache_ttl)) {
    sRecordFetchOutcome(identifier, false);
    THREAD_CONTEXT_RETURN_RESULT_ERROR(EMPTY_USERDATA, RESCODE_BACKEND_DATA_EMPTYSET)
  }

  UFSRVResult *store_res = OAuthClientMetadataRegister(identifier, document, cache_ttl, client_out, client_db_id_out);

  if (!_RESULT_TYPE_SUCCESS(store_res)) {
    sRecordFetchOutcome(identifier, false);

    return store_res;
  }

  sRecordFetchOutcome(identifier, true);

  return store_res;
}
