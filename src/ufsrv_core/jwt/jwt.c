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

#include <ufsrv_core/jwt/jwt.h>

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

#include <uflib/utils_base64.h>
#include <uflib/utils_base64url.h>

#define JWT_ALGORITHM "RS256"
#define JWT_HEADER_TYPE "JWT"
#define JWT_EDDSA "EdDSA"
#define JWT_ED25519_KEY_BYTES 32u
#define JWT_ED25519_TEXT_BYTES 43u

typedef struct JwtKey {
  char     *kid;
  EVP_PKEY *pkey;
  bool      has_private;
} JwtKey;

struct JwtKeyset {
  JwtKey *keys;
  size_t  count;
  size_t  capacity;
};

static void
sSetError(char *err, size_t err_sz, const char *format, ...)
{
  va_list args;

  if (err == NULL || err_sz == 0) return;

  va_start(args, format);
  vsnprintf(err, err_sz, format, args);
  va_end(args);
}

static void
sOsslError(char *err, size_t err_sz, const char *what)
{
  unsigned long code = ERR_get_error();
  char          text[256] = {0};

  if (code != 0) ERR_error_string_n(code, text, sizeof text);

  sSetError(err, err_sz, "%s%s%s", what, text[0] != '\0' ? ": " : "", text);
  ERR_clear_error();
}

static char *
sBase64UrlEncode(const unsigned char *data, size_t data_sz)
{
  size_t encoded_sz = GetBase64BufferAllocationSize(data_sz);
  char  *encoded    = calloc(1, encoded_sz);

  if (encoded == NULL) return NULL;

  base64url_encode(data, data_sz, (unsigned char *)encoded);

  return encoded;
}

static char *
sBigNumberToBase64Url(const BIGNUM *number)
{
  int            number_sz = BN_num_bytes(number);
  unsigned char *raw       = NULL;
  char          *encoded   = NULL;

  if (number_sz <= 0) return NULL;

  raw = malloc((size_t)number_sz);
  if (raw == NULL) return NULL;

  if (BN_bn2bin(number, raw) != number_sz) {
    free(raw);

    return NULL;
  }

  encoded = sBase64UrlEncode(raw, (size_t)number_sz);
  free(raw);

  return encoded;
}

static bool
sPublicComponents(EVP_PKEY *pkey, char **n_out, char **e_out)
{
  BIGNUM *n = NULL;
  BIGNUM *e = NULL;
  bool    result = false;

  if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N, &n) != 1) goto done;
  if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_E, &e) != 1) goto done;

  *n_out = sBigNumberToBase64Url(n);
  *e_out = sBigNumberToBase64Url(e);

  result = (*n_out != NULL && *e_out != NULL);

done:
  BN_free(n);
  BN_free(e);

  return result;
}

static char *
sDeriveKeyId(EVP_PKEY *pkey)
{
  char         *n        = NULL;
  char         *e        = NULL;
  char         *canonical = NULL;
  unsigned char digest[SHA256_DIGEST_LENGTH];
  char         *key_id   = NULL;

  if (EVP_PKEY_is_a(pkey, "ED25519")) {
    unsigned char raw[JWT_ED25519_KEY_BYTES];
    size_t length = sizeof raw;
    if (EVP_PKEY_get_raw_public_key(pkey, raw, &length) != 1 || length != sizeof raw) return NULL;
    char *x = sBase64UrlEncode(raw, length);
    if (!x) return NULL;
    const int encoded = asprintf(&canonical, "{\"crv\":\"Ed25519\",\"kty\":\"OKP\",\"x\":\"%s\"}", x);
    free(x);
    if (encoded < 0) return NULL;
    SHA256((const unsigned char *)canonical, strlen(canonical), digest);
    free(canonical);
    return sBase64UrlEncode(digest, sizeof digest);
  }

  if (!sPublicComponents(pkey, &n, &e)) return NULL;

  if (asprintf(&canonical, "{\"e\":\"%s\",\"kty\":\"RSA\",\"n\":\"%s\"}", e, n) < 0) {
    free(n);
    free(e);

    return NULL;
  }

  SHA256((const unsigned char *)canonical, strlen(canonical), digest);
  key_id = sBase64UrlEncode(digest, sizeof digest);

  free(n);
  free(e);
  free(canonical);

  return key_id;
}

static EVP_PKEY *
sReadPem(const char *pem, size_t pem_sz, bool *has_private)
{
  BIO      *bio  = NULL;
  EVP_PKEY *pkey = NULL;

  if (pem_sz > (size_t)INT_MAX) return NULL;

  bio = BIO_new_mem_buf(pem, (int)pem_sz);
  if (bio == NULL) return NULL;

  pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
  if (pkey != NULL) {
    *has_private = true;

    BIO_free(bio);

    return pkey;
  }

  ERR_clear_error();
  BIO_free(bio);

  bio = BIO_new_mem_buf(pem, (int)pem_sz);
  if (bio == NULL) return NULL;

  pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
  if (pkey != NULL) *has_private = false;

  BIO_free(bio);

  return pkey;
}

char *
JwtEd25519PublicFromPem(const char *pem, size_t pem_sz, char *err, size_t err_sz)
{
  if (!pem || !*pem) {
    sSetError(err, err_sz, "no Ed25519 PEM supplied");
    return NULL;
  }

  bool has_private = false;
  EVP_PKEY *key = sReadPem(pem, pem_sz ? pem_sz : strlen(pem), &has_private);
  unsigned char raw[JWT_ED25519_KEY_BYTES];
  size_t length = sizeof raw;
  char *public_key = NULL;
  if (key && EVP_PKEY_is_a(key, "ED25519") &&
      EVP_PKEY_get_raw_public_key(key, raw, &length) == 1 && length == sizeof raw) {
    public_key = sBase64UrlEncode(raw, length);
  }
  if (!public_key) sOsslError(err, err_sz, "could not extract the Ed25519 public key from PEM");
  EVP_PKEY_free(key);
  return public_key;
}

JwtKeyset *
JwtKeysetNew(void)
{
  return calloc(1, sizeof(JwtKeyset));
}

void
JwtKeysetFree(JwtKeyset *keyset)
{
  size_t index;

  if (keyset == NULL) return;

  for (index = 0; index < keyset->count; index++) {
    free(keyset->keys[index].kid);
    EVP_PKEY_free(keyset->keys[index].pkey);
  }

  free(keyset->keys);
  free(keyset);
}

size_t
JwtKeysetCount(const JwtKeyset *keyset)
{
  return keyset != NULL ? keyset->count : 0;
}

const char *
JwtKeysetKidAt(const JwtKeyset *keyset, size_t index)
{
  if (keyset == NULL || index >= keyset->count) return NULL;

  return keyset->keys[index].kid;
}

static JwtStatus
sKeysetAppend(JwtKeyset *keyset, EVP_PKEY *pkey, const char *key_id, bool has_private,
              char *err, size_t err_sz)
{
  char   *kid = NULL;
  size_t  index;

  if (key_id != NULL) {
    kid = strdup(key_id);
  } else {
    kid = sDeriveKeyId(pkey);

    if (kid == NULL) {
      sOsslError(err, err_sz, "could not derive the key id");

      EVP_PKEY_free(pkey);

      return JWT_STATUS_KEY_ERROR;
    }
  }

  if (kid == NULL) {
    sSetError(err, err_sz, "could not allocate the key id");

    EVP_PKEY_free(pkey);

    return JWT_STATUS_KEY_ERROR;
  }

  for (index = 0; index < keyset->count; index++) {
    if (strcmp(keyset->keys[index].kid, kid) == 0) {
      sSetError(err, err_sz, "key id '%s' is already in the set", kid);

      free(kid);
      EVP_PKEY_free(pkey);

      return JWT_STATUS_KEY_ERROR;
    }
  }

  if (keyset->count == keyset->capacity) {
    size_t  capacity = keyset->capacity == 0 ? 4 : keyset->capacity * 2;
    JwtKey *keys     = realloc(keyset->keys, capacity * sizeof *keys);

    if (keys == NULL) {
      sSetError(err, err_sz, "could not grow the key set");

      free(kid);
      EVP_PKEY_free(pkey);

      return JWT_STATUS_KEY_ERROR;
    }

    keyset->keys     = keys;
    keyset->capacity = capacity;
  }

  keyset->keys[keyset->count++] = (JwtKey){
      .kid         = kid,
      .pkey        = pkey,
      .has_private = has_private,
  };

  return JWT_STATUS_OK;
}

JwtStatus
JwtKeysetAddPem(JwtKeyset *keyset, const char *pem, size_t pem_sz, const char *key_id,
                char *err, size_t err_sz)
{
  EVP_PKEY *pkey           = NULL;
  bool      has_private    = false;

  if (keyset == NULL || pem == NULL) {
    sSetError(err, err_sz, "no key set or no PEM supplied");

    return JWT_STATUS_INVALID_ARGUMENT;
  }

  if (pem_sz == 0) pem_sz = strlen(pem);

  pkey = sReadPem(pem, pem_sz, &has_private);
  if (pkey == NULL) {
    sOsslError(err, err_sz, "could not read the PEM key");

    return JWT_STATUS_KEY_ERROR;
  }

  return sKeysetAppend(keyset, pkey, key_id, has_private, err, err_sz);
}

JwtStatus
JwtKeysetAddPemFile(JwtKeyset *keyset, const char *path, const char *key_id,
                    char *err, size_t err_sz)
{
  FILE   *file     = NULL;
  char   *contents = NULL;
  long    file_sz  = 0;
  JwtStatus status = JWT_STATUS_OK;

  if (keyset == NULL || path == NULL) {
    sSetError(err, err_sz, "no key set or no path supplied");

    return JWT_STATUS_INVALID_ARGUMENT;
  }

  file = fopen(path, "rb");
  if (file == NULL) {
    sSetError(err, err_sz, "could not open '%s'", path);

    return JWT_STATUS_KEY_ERROR;
  }

  if (fseek(file, 0, SEEK_END) != 0 || (file_sz = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    sSetError(err, err_sz, "'%s' is empty or not seekable", path);

    fclose(file);

    return JWT_STATUS_KEY_ERROR;
  }

  contents = malloc((size_t)file_sz);
  if (contents == NULL) {
    sSetError(err, err_sz, "could not read '%s'", path);

    fclose(file);

    return JWT_STATUS_KEY_ERROR;
  }

  if (fread(contents, 1, (size_t)file_sz, file) != (size_t)file_sz) {
    sSetError(err, err_sz, "could not read '%s'", path);

    free(contents);
    fclose(file);

    return JWT_STATUS_KEY_ERROR;
  }

  fclose(file);

  status = JwtKeysetAddPem(keyset, contents, (size_t)file_sz, key_id, err, err_sz);

  free(contents);

  return status;
}

static JwtStatus
sAddEd25519Public(JwtKeyset *keyset, const unsigned char *text, size_t length, const char *key_id,
                  char *err, size_t err_sz)
{
  if (length && text[length - 1] == '\n') {
    --length;
    if (length && text[length - 1] == '\r') --length;
  }
  unsigned char raw[JWT_ED25519_KEY_BYTES + 1];
  if (length != JWT_ED25519_TEXT_BYTES ||
      base64url_decode(text, length, raw, sizeof raw) != JWT_ED25519_KEY_BYTES) {
    sSetError(err, err_sz, "expected an unpadded base64url Ed25519 public key");
    return JWT_STATUS_KEY_ERROR;
  }
  char *canonical = sBase64UrlEncode(raw, JWT_ED25519_KEY_BYTES);
  const bool valid = canonical && memcmp(text, canonical, length) == 0;
  free(canonical);
  if (!valid) {
    sSetError(err, err_sz, "invalid Ed25519 public key encoding");
    return JWT_STATUS_KEY_ERROR;
  }

  EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, raw, JWT_ED25519_KEY_BYTES);
  if (!pkey) {
    sOsslError(err, err_sz, "could not import the Ed25519 public key");
    return JWT_STATUS_KEY_ERROR;
  }
  return sKeysetAppend(keyset, pkey, key_id, false, err, err_sz);
}

JwtStatus
JwtKeysetAddEd25519Public(JwtKeyset *keyset, const char *public_key, const char *key_id,
                         char *err, size_t err_sz)
{
  if (!keyset || !public_key) return JWT_STATUS_INVALID_ARGUMENT;
  const size_t length = strnlen(public_key, JWT_ED25519_TEXT_BYTES + 3);
  return sAddEd25519Public(keyset, (const unsigned char *)public_key, length, key_id, err, err_sz);
}

JwtStatus
JwtKeysetAddEd25519PublicFile(JwtKeyset *keyset, const char *path, const char *key_id,
                             char *err, size_t err_sz)
{
  if (!keyset || !path) return JWT_STATUS_INVALID_ARGUMENT;
  FILE *file = fopen(path, "rb");
  if (!file) {
    sSetError(err, err_sz, "could not open '%s'", path);
    return JWT_STATUS_KEY_ERROR;
  }

  unsigned char text[JWT_ED25519_TEXT_BYTES + 3];
  size_t length = fread(text, 1, sizeof text, file);
  const bool read_ok = !ferror(file) && feof(file);
  fclose(file);

  if (!read_ok) {
    sSetError(err, err_sz, "could not read a complete Ed25519 public key from '%s'", path);
    return JWT_STATUS_KEY_ERROR;
  }
  return sAddEd25519Public(keyset, text, length, key_id, err, err_sz);
}

static JwtKey *
sFindKey(const JwtKeyset *keyset, const char *key_id)
{
  size_t index;

  if (keyset == NULL || keyset->count == 0) return NULL;

  if (key_id == NULL) return keyset->count == 1 ? &keyset->keys[0] : NULL;

  for (index = 0; index < keyset->count; index++) {
    if (strcmp(keyset->keys[index].kid, key_id) == 0) return &keyset->keys[index];
  }

  return NULL;
}

static char *
sMint(const JwtKeyset *keyset, const char *key_id, const json_object *claims,
        char *err, size_t err_sz, bool ed25519)
{
  JwtKey         *key            = NULL;
  json_object    *header         = NULL;
  const char     *header_json    = NULL;
  const char     *claims_json    = NULL;
  char           *header_encoded = NULL;
  char           *claims_encoded = NULL;
  char           *signing_input  = NULL;
  unsigned char  *signature      = NULL;
  char           *signature_encoded = NULL;
  char           *token          = NULL;
  size_t          signature_sz   = 0;
  EVP_MD_CTX     *digest         = NULL;

  if (keyset == NULL || claims == NULL) {
    sSetError(err, err_sz, "no key set or no claims supplied");

    return NULL;
  }

  key = sFindKey(keyset, key_id);
  if (key == NULL) {
    sSetError(err, err_sz, "%s", key_id == NULL ? "the key set holds more than one key; name the signing key"
                                                : "the signing key is not in the set");

    return NULL;
  }

  const char *key_type = ed25519 ? "ED25519" : "RSA";
  if (!key->has_private || !EVP_PKEY_is_a(key->pkey, key_type)) {
    sSetError(err, err_sz, "key '%s' is not a %s private signing key", key->kid, key_type);

    return NULL;
  }

  header = json_object_new_object();
  if (header == NULL) {
    sSetError(err, err_sz, "could not build the JWT header");

    return NULL;
  }

  json_object_object_add(header, "alg", json_object_new_string(ed25519 ? JWT_EDDSA : JWT_ALGORITHM));
  json_object_object_add(header, "typ", json_object_new_string(JWT_HEADER_TYPE));
  json_object_object_add(header, "kid", json_object_new_string(key->kid));

  header_json = json_object_to_json_string_ext(header, JSON_C_TO_STRING_PLAIN);
  claims_json = json_object_to_json_string_ext((json_object *)claims, JSON_C_TO_STRING_PLAIN);

  header_encoded = sBase64UrlEncode((const unsigned char *)header_json, strlen(header_json));
  claims_encoded = sBase64UrlEncode((const unsigned char *)claims_json, strlen(claims_json));
  json_object_put(header);

  if (header_encoded == NULL || claims_encoded == NULL) {
    sSetError(err, err_sz, "could not encode the JWT segments");

    goto done;
  }

  if (asprintf(&signing_input, "%s.%s", header_encoded, claims_encoded) < 0) {
    sSetError(err, err_sz, "could not assemble the signing input");

    goto done;
  }

  digest = EVP_MD_CTX_new();
  if (digest == NULL) {
    sOsslError(err, err_sz, "could not allocate the signing context");

    goto done;
  }

  if (EVP_DigestSignInit(digest, NULL, ed25519 ? NULL : EVP_sha256(), NULL, key->pkey) != 1 ||
      EVP_DigestSign(digest, NULL, &signature_sz, (const unsigned char *)signing_input, strlen(signing_input)) != 1) {
    sOsslError(err, err_sz, "could not prepare the signature");

    goto done;
  }

  signature = malloc(signature_sz);
  if (signature == NULL) {
    sSetError(err, err_sz, "could not allocate the signature");

    goto done;
  }

  if (EVP_DigestSign(digest, signature, &signature_sz, (const unsigned char *)signing_input, strlen(signing_input)) != 1) {
    sOsslError(err, err_sz, "could not sign the token");

    goto done;
  }

  signature_encoded = sBase64UrlEncode(signature, signature_sz);
  if (signature_encoded == NULL) {
    sSetError(err, err_sz, "could not encode the signature");

    goto done;
  }

  if (asprintf(&token, "%s.%s", signing_input, signature_encoded) < 0) {
    sSetError(err, err_sz, "could not assemble the token");

    token = NULL;
  }

done:
  EVP_MD_CTX_free(digest);
  free(signature);
  free(signature_encoded);
  free(signing_input);
  free(header_encoded);
  free(claims_encoded);

  return token;
}

char *
JwtMint(const JwtKeyset *keyset, const char *key_id, const json_object *claims,
        char *err, size_t err_sz)
{
  return sMint(keyset, key_id, claims, err, err_sz, false);
}

char *
JwtMintEd25519(const JwtKeyset *keyset, const char *key_id, const json_object *claims,
               char *err, size_t err_sz)
{
  return sMint(keyset, key_id, claims, err, err_sz, true);
}

static char *
sMintEd25519FromPem(const char *input, size_t pem_sz, bool from_file, const json_object *claims,
                    char *err, size_t err_sz)
{
  if (!input || !claims) {
    sSetError(err, err_sz, "expected a private PEM source and claims");
    return NULL;
  }
  JwtKeyset *keys = JwtKeysetNew();
  if (!keys) {
    sSetError(err, err_sz, "could not allocate signing key set");
    return NULL;
  }
  const JwtStatus loaded = from_file ? JwtKeysetAddPemFile(keys, input, NULL, err, err_sz)
                                    : JwtKeysetAddPem(keys, input, pem_sz, NULL, err, err_sz);
  char *token = loaded == JWT_STATUS_OK ? JwtMintEd25519(keys, NULL, claims, err, err_sz) : NULL;
  JwtKeysetFree(keys);
  return token;
}

char *
JwtMintEd25519FromPemFile(const char *path, const json_object *claims, char *err, size_t err_sz)
{
  return sMintEd25519FromPem(path, 0, true, claims, err, err_sz);
}

char *
JwtMintEd25519FromPem(const char *pem, size_t pem_sz, const json_object *claims, char *err, size_t err_sz)
{
  return sMintEd25519FromPem(pem, pem_sz, false, claims, err, err_sz);
}

static json_object *
sDecodeSegment(const char *segment, size_t segment_sz)
{
  size_t  decoded_sz = base64_decoded_size((const unsigned char *)segment, segment_sz);
  char   *decoded    = NULL;
  json_object *parsed = NULL;

  if (decoded_sz == 0) return NULL;

  decoded = calloc(1, decoded_sz);
  if (decoded == NULL) return NULL;

  if (base64url_decode((const unsigned char *)segment, segment_sz, (unsigned char *)decoded, decoded_sz) < 0) {
    free(decoded);

    return NULL;
  }

  parsed = json_tokener_parse(decoded);
  free(decoded);

  return parsed;
}

static bool
sVerifySignature(const JwtKey *key, const char *signing_input, size_t signing_sz,
                 const char *signature_segment, size_t signature_sz, bool ed25519)
{
  if (!EVP_PKEY_is_a(key->pkey, ed25519 ? "ED25519" : "RSA")) return false;
  size_t          decoded_sz = base64_decoded_size((const unsigned char *)signature_segment, signature_sz);
  unsigned char  *signature  = NULL;
  EVP_MD_CTX     *digest     = NULL;
  ssize_t         signature_len;
  bool            verified   = false;

  if (decoded_sz == 0) return false;

  signature = malloc(decoded_sz);
  if (signature == NULL) return false;

  signature_len = base64url_decode((const unsigned char *)signature_segment, signature_sz, signature, decoded_sz);
  if (signature_len <= 0 || (ed25519 && signature_len != 64)) goto done;

  digest = EVP_MD_CTX_new();
  if (digest == NULL) goto done;

  if (EVP_DigestVerifyInit(digest, NULL, ed25519 ? NULL : EVP_sha256(), NULL, key->pkey) != 1) goto done;

  verified = EVP_DigestVerify(digest, signature, (size_t)signature_len,
                              (const unsigned char *)signing_input, signing_sz) == 1;

done:
  EVP_MD_CTX_free(digest);
  free(signature);
  ERR_clear_error();

  return verified;
}

static bool
sAudienceMatches(json_object *claims, const char *audience)
{
  json_object *value = NULL;

  if (!json_object_object_get_ex(claims, "aud", &value)) return false;

  if (json_object_is_type(value, json_type_string)) {
    return (size_t)json_object_get_string_len(value) == strlen(audience) &&
           strcmp(json_object_get_string(value), audience) == 0;
  }

  if (json_object_is_type(value, json_type_array)) {
    size_t index;
    size_t count = json_object_array_length(value);

    for (index = 0; index < count; index++) {
      json_object *entry = json_object_array_get_idx(value, index);

      if (json_object_is_type(entry, json_type_string) &&
          (size_t)json_object_get_string_len(entry) == strlen(audience) &&
          strcmp(json_object_get_string(entry), audience) == 0) {
        return true;
      }
    }
  }

  return false;
}

static JwtStatus
sCheckClaims(json_object *claims, const JwtVerifyOptions *options)
{
  json_object *value = NULL;
  time_t       now   = time(NULL);

  if (!json_object_object_get_ex(claims, "exp", &value)) {
    if (options->require_expiry) return JWT_STATUS_MISSING_EXPIRY;
  } else {
    if (!json_object_is_type(value, json_type_int)) return JWT_STATUS_MALFORMED;

    if (difftime(now, (time_t)json_object_get_int64(value)) > (double)options->leeway_seconds) {
      return JWT_STATUS_EXPIRED;
    }
  }

  if (json_object_object_get_ex(claims, "nbf", &value)) {
    if (!json_object_is_type(value, json_type_int)) return JWT_STATUS_MALFORMED;

    if (difftime((time_t)json_object_get_int64(value), now) > (double)options->leeway_seconds) {
      return JWT_STATUS_NOT_YET_VALID;
    }
  }

  if (options->issuer != NULL) {
    if (!json_object_object_get_ex(claims, "iss", &value) ||
        !json_object_is_type(value, json_type_string) ||
        json_object_get_string_len(value) != (int)strlen(options->issuer) ||
        strcmp(json_object_get_string(value), options->issuer) != 0) {
      return JWT_STATUS_BAD_ISSUER;
    }
  }

  if (options->audience != NULL && !sAudienceMatches(claims, options->audience)) {
    return JWT_STATUS_BAD_AUDIENCE;
  }

  return JWT_STATUS_OK;
}

static JwtStatus
sVerify(const char *token, const JwtKeyset *keyset, const JwtVerifyOptions *options,
        json_object **claims_out, bool ed25519)
{
  static const JwtVerifyOptions default_options = {
      .issuer         = NULL,
      .audience       = NULL,
      .require_expiry = true,
      .leeway_seconds = 0,
  };

  const JwtVerifyOptions *effective = options != NULL ? options : &default_options;
  const char             *payload   = NULL;
  const char             *signature = NULL;
  const char             *key_id    = NULL;
  size_t                  header_sz;
  size_t                  payload_sz;
  size_t                  signature_sz;
  size_t                  signing_sz;
  json_object            *header    = NULL;
  json_object            *claims    = NULL;
  json_object            *alg       = NULL;
  json_object            *kid       = NULL;
  bool                    verified  = false;
  size_t                  index;
  JwtStatus               status    = JWT_STATUS_OK;

  if (claims_out != NULL) *claims_out = NULL;

  if (token == NULL || keyset == NULL || keyset->count == 0) {
    return JWT_STATUS_INVALID_ARGUMENT;
  }

  payload = strchr(token, '.');
  if (payload == NULL) return JWT_STATUS_MALFORMED;

  signature = strchr(payload + 1, '.');
  if (signature == NULL || strchr(signature + 1, '.') != NULL) return JWT_STATUS_MALFORMED;

  header_sz    = (size_t)(payload - token);
  payload_sz   = (size_t)(signature - payload - 1);
  signature_sz = strlen(signature + 1);
  signing_sz   = (size_t)(signature - token);

  if (header_sz == 0 || payload_sz == 0 || signature_sz == 0) return JWT_STATUS_MALFORMED;

  header = sDecodeSegment(token, header_sz);
  if (header == NULL || !json_object_is_type(header, json_type_object)) {
    status = JWT_STATUS_MALFORMED;
    goto done;
  }

  if (!json_object_object_get_ex(header, "alg", &alg) ||
      !json_object_is_type(alg, json_type_string) ||
      json_object_get_string_len(alg) != (int)strlen(ed25519 ? JWT_EDDSA : JWT_ALGORITHM) ||
      strcmp(json_object_get_string(alg), ed25519 ? JWT_EDDSA : JWT_ALGORITHM) != 0 ||
      json_object_object_get_ex(header, "crit", NULL) ||
      json_object_object_get_ex(header, "b64", NULL)) {
    status = JWT_STATUS_UNSUPPORTED_ALGORITHM;
    goto done;
  }

  if (json_object_object_get_ex(header, "kid", &kid) && json_object_is_type(kid, json_type_string)) {
    key_id = json_object_get_string(kid);
  }

  if (key_id != NULL) {
    JwtKey *key = sFindKey(keyset, key_id);

    if (key == NULL) {
      status = JWT_STATUS_UNKNOWN_KEY;
      goto done;
    }

    verified = sVerifySignature(key, token, signing_sz, signature + 1, signature_sz, ed25519);
  } else {
    for (index = 0; index < keyset->count && !verified; index++) {
      verified = sVerifySignature(&keyset->keys[index], token, signing_sz, signature + 1, signature_sz, ed25519);
    }
  }

  if (!verified) {
    status = key_id != NULL ? JWT_STATUS_BAD_SIGNATURE : JWT_STATUS_UNKNOWN_KEY;
    goto done;
  }

  claims = sDecodeSegment(payload + 1, payload_sz);
  if (claims == NULL || !json_object_is_type(claims, json_type_object)) {
    status = JWT_STATUS_MALFORMED;
    goto done;
  }

  status = sCheckClaims(claims, effective);

done:
  if (status == JWT_STATUS_OK && claims_out != NULL) {
    *claims_out = claims;
    claims      = NULL;
  }

  json_object_put(header);
  json_object_put(claims);

  return status;
}

JwtStatus
JwtVerify(const char *token, const JwtKeyset *keyset, const JwtVerifyOptions *options, json_object **claims_out)
{
  return sVerify(token, keyset, options, claims_out, false);
}

JwtStatus
JwtVerifyEd25519(const char *token, const JwtKeyset *keyset, const JwtVerifyOptions *options, json_object **claims_out)
{
  return sVerify(token, keyset, options, claims_out, true);
}

char *
JwtKeysetRenderJwks(const JwtKeyset *keyset)
{
  json_object *document = NULL;
  json_object *keys     = NULL;
  char        *rendered = NULL;
  size_t       index;

  if (keyset == NULL || keyset->count == 0) return NULL;

  document = json_object_new_object();
  keys     = json_object_new_array();

  if (document == NULL || keys == NULL) {
    json_object_put(document);
    json_object_put(keys);

    return NULL;
  }

  for (index = 0; index < keyset->count; index++) {
    json_object *entry = json_object_new_object();
    char        *n     = NULL;
    char        *e     = NULL;

    if (entry && EVP_PKEY_is_a(keyset->keys[index].pkey, "ED25519")) {
      unsigned char raw[JWT_ED25519_KEY_BYTES];
      size_t length = sizeof raw;
      char *x = EVP_PKEY_get_raw_public_key(keyset->keys[index].pkey, raw, &length) == 1 && length == sizeof raw
                  ? sBase64UrlEncode(raw, length) : NULL;
      if (!x) {
        json_object_put(entry);
        json_object_put(document);
        json_object_put(keys);
        return NULL;
      }
      json_object_object_add(entry, "kty", json_object_new_string("OKP"));
      json_object_object_add(entry, "crv", json_object_new_string("Ed25519"));
      json_object_object_add(entry, "x", json_object_new_string(x));
      json_object_object_add(entry, "use", json_object_new_string("sig"));
      json_object_object_add(entry, "alg", json_object_new_string(JWT_EDDSA));
      json_object_object_add(entry, "kid", json_object_new_string(keyset->keys[index].kid));
      json_object_array_add(keys, entry);
      free(x);
      continue;
    }

    if (entry == NULL || !sPublicComponents(keyset->keys[index].pkey, &n, &e)) {
      free(n);
      free(e);
      json_object_put(entry);
      json_object_put(document);
      json_object_put(keys);

      return NULL;
    }

    json_object_object_add(entry, "kty", json_object_new_string("RSA"));
    json_object_object_add(entry, "use", json_object_new_string("sig"));
    json_object_object_add(entry, "alg", json_object_new_string(JWT_ALGORITHM));
    json_object_object_add(entry, "kid", json_object_new_string(keyset->keys[index].kid));
    json_object_object_add(entry, "n", json_object_new_string(n));
    json_object_object_add(entry, "e", json_object_new_string(e));
    json_object_array_add(keys, entry);

    free(n);
    free(e);
  }

  json_object_object_add(document, "keys", keys);

  rendered = strdup(json_object_to_json_string_ext(document, JSON_C_TO_STRING_PRETTY));
  json_object_put(document);

  return rendered;
}

const char *
JwtStatusDescription(JwtStatus status)
{
  switch (status) {
  case JWT_STATUS_OK:                    return "ok";
  case JWT_STATUS_INVALID_ARGUMENT:      return "invalid argument";
  case JWT_STATUS_KEY_ERROR:             return "key error";
  case JWT_STATUS_MALFORMED:             return "malformed token";
  case JWT_STATUS_UNSUPPORTED_ALGORITHM: return "unsupported algorithm";
  case JWT_STATUS_UNKNOWN_KEY:           return "unknown key";
  case JWT_STATUS_BAD_SIGNATURE:         return "bad signature";
  case JWT_STATUS_MISSING_EXPIRY:        return "missing expiry";
  case JWT_STATUS_EXPIRED:               return "expired";
  case JWT_STATUS_NOT_YET_VALID:         return "not yet valid";
  case JWT_STATUS_BAD_ISSUER:            return "bad issuer";
  case JWT_STATUS_BAD_AUDIENCE:          return "bad audience";
  }

  return "unknown";
}
