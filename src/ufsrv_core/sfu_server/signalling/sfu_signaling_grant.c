#include <ufsrv_core/sfu_server/signalling/sfu_signaling_grant.h>

#include <ufsrv_core/jwt/jwt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SFU_GRANT_TOKEN_MAX 8192u

struct SfuGrantVerifier {
  JwtKeyset *keys;
  JwtVerifyOptions options;
};

static SfuGrantVerifier *
sCreateVerifier(const char *key, const char *issuer, const char *audience, char *error, size_t error_size,
                JwtStatus (*load_key)(JwtKeyset *, const char *, const char *, char *, size_t))
{
  if (!key || !*key || !issuer || !*issuer || !audience || !*audience) {
    if (error && error_size) snprintf(error, error_size, "expected a public key source, issuer and audience");
    return NULL;
  }

  SfuGrantVerifier *verifier = calloc(1, sizeof *verifier);
  if (!verifier) {
    if (error && error_size) snprintf(error, error_size, "could not allocate the grant verifier");
    return NULL;
  }
  verifier->keys = JwtKeysetNew();
  verifier->options = (JwtVerifyOptions){
    .issuer = strdup(issuer),
    .audience = strdup(audience),
    .require_expiry = true
  };
  if (!verifier->keys || !verifier->options.issuer || !verifier->options.audience) {
    if (error && error_size) snprintf(error, error_size, "could not allocate verifier configuration");
    SfuGrantVerifierDestroy(verifier);
    return NULL;
  }
  if (load_key(verifier->keys, key, NULL, error, error_size) != JWT_STATUS_OK) {
    SfuGrantVerifierDestroy(verifier);
    return NULL;
  }
  return verifier;
}

SfuGrantVerifier *
SfuGrantVerifierCreate(const char *public_key_path, const char *issuer, const char *audience,
                       char *error, size_t error_size)
{
  return sCreateVerifier(public_key_path, issuer, audience, error, error_size, JwtKeysetAddEd25519PublicFile);
}

SfuGrantVerifier *
SfuGrantVerifierCreateFromPublicKey(const char *public_key, const char *issuer, const char *audience,
                                    char *error, size_t error_size)
{
  return sCreateVerifier(public_key, issuer, audience, error, error_size, JwtKeysetAddEd25519Public);
}

void
SfuGrantVerifierDestroy(SfuGrantVerifier *verifier)
{
  if (!verifier) return;
  JwtKeysetFree(verifier->keys);
  free((void *)verifier->options.issuer);
  free((void *)verifier->options.audience);
  free(verifier);
}

static const char *
sStringClaim(json_object *claims, const char *name)
{
  json_object *value;
  if (!json_object_object_get_ex(claims, name, &value) || !json_object_is_type(value, json_type_string)) return NULL;
  const char *text = json_object_get_string(value);
  return strlen(text) == (size_t)json_object_get_string_len(value) ? text : NULL;
}

static uint32_t
sScopes(const char *scope)
{
  static const struct {
    const char *text;
    SfuGrantScope bit;
  } values[] = {
    {"conference:start", SFU_GRANT_CONFERENCE_START},
    {"conference:join", SFU_GRANT_CONFERENCE_JOIN},
    {"media:publish", SFU_GRANT_MEDIA_PUBLISH},
    {"media:subscribe", SFU_GRANT_MEDIA_SUBSCRIBE}
  };
  uint32_t bits = 0;
  while (*(scope += strspn(scope, " \t\n\r\v\f"))) {
    const size_t length = strcspn(scope, " \t\n\r\v\f");
    for (size_t i = 0; i < sizeof values / sizeof values[0]; ++i) {
      if (strlen(values[i].text) == length && memcmp(scope, values[i].text, length) == 0) bits |= values[i].bit;
    }
    scope += length;
  }
  return bits;
}

bool
SfuGrantVerifyBearer(const SfuGrantVerifier *verifier, const char *authorization, SfuSignalingGrant *grant)
{
  static const char scheme[] = "Bearer ";
  if (!grant) return false;
  *grant = (SfuSignalingGrant){0};
  if (!verifier || !authorization || strncasecmp(authorization, scheme, sizeof scheme - 1) != 0) return false;
  const char *token = authorization + sizeof scheme - 1;
  if (strnlen(token, SFU_GRANT_TOKEN_MAX + 1) > SFU_GRANT_TOKEN_MAX) return false;

  json_object *claims = NULL;
  if (JwtVerifyEd25519(token, verifier->keys, &verifier->options, &claims) != JWT_STATUS_OK) return false;

  const char *subject = sStringClaim(claims, "sub");
  const char *conference = sStringClaim(claims, "conference_id");
  const char *scope = sStringClaim(claims, "scope");
  const char *audience = sStringClaim(claims, "aud");
  const int64_t expiry = json_object_get_int64(json_object_object_get(claims, "exp"));
  const int64_t now = time(NULL);
  const bool valid = now >= 0 && expiry > now && expiry - now <= SFU_GRANT_LIFETIME_SECONDS &&
                     audience && strcmp(audience, verifier->options.audience) == 0 &&
                     subject && *subject && strlen(subject) <= SFU_GRANT_ID_MAX &&
                     conference && *conference && strlen(conference) <= SFU_GRANT_ID_MAX &&
                     strspn(conference, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == strlen(conference) &&
                     scope;
  if (valid) {
    strcpy(grant->subject, subject);
    strcpy(grant->conference_id, conference);
    grant->scopes = sScopes(scope);
  }
  json_object_put(claims);
  return valid;
}

bool
SfuGrantAllows(const SfuSignalingGrant *grant, const char *conference_id, uint32_t scopes)
{
  return grant && conference_id && *conference_id && strcmp(grant->conference_id, conference_id) == 0 &&
         (grant->scopes & scopes) == scopes;
}
