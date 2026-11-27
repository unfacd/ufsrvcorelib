#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "ufsrv_core/turn/turn_ephemeral_credential.h"

void
TurnEphemeralCredentialClear(TurnEphemeralCredential *issued)
{
  if (issued) OPENSSL_cleanse(issued, sizeof(*issued));
}

bool
TurnEphemeralCredentialDerivePassword(const char *realm,
                                      const uint8_t secret[TURN_EPHEMERAL_SECRET_LENGTH],
                                      const char *username,
                                      char password[TURN_EPHEMERAL_PASSWORD_SIZE])
{
  if (!realm || !*realm || strlen(realm) > TURN_EPHEMERAL_REALM_MAX_LENGTH ||
      !secret || !username || strlen(username) != TURN_EPHEMERAL_USERNAME_SIZE - 1 || !password) return false;

  char input[sizeof("unfacd-turn-v1::") + TURN_EPHEMERAL_REALM_MAX_LENGTH + TURN_EPHEMERAL_USERNAME_SIZE];
  int input_length = snprintf(input, sizeof(input), "unfacd-turn-v1:%s:%s", realm, username);
  uint8_t mac[32] = {0};
  unsigned int length = 0;
  bool ok = input_length > 0 && (size_t)input_length < sizeof(input) &&
            HMAC(EVP_sha256(), secret, TURN_EPHEMERAL_SECRET_LENGTH,
                 (const uint8_t *)input, (size_t)input_length, mac, &length) &&
            length == sizeof(mac) &&
            EVP_EncodeBlock((unsigned char *)password, mac, (int)length) == TURN_EPHEMERAL_PASSWORD_SIZE - 1;
  OPENSSL_cleanse(mac, sizeof(mac));
  return ok;
}

bool
TurnEphemeralCredentialIssue(const char *realm,
                             const uint8_t secret[TURN_EPHEMERAL_SECRET_LENGTH],
                             uint32_t lifetime_seconds, uint64_t authenticated_user_id,
                             time_t now, TurnEphemeralCredential *issued)
{
  if (!issued) return false;
  TurnEphemeralCredentialClear(issued);
  if (!lifetime_seconds || !authenticated_user_id || now < 0 ||
      (uintmax_t)now > UINT64_MAX - lifetime_seconds) return false;

  uint64_t expiry = (uint64_t)now + lifetime_seconds;
  time_t expires_at = (time_t)expiry;
  if (expires_at <= now || (uintmax_t)expires_at != expiry) return false;
  int length = snprintf(issued->username, sizeof(issued->username),
                        "%016" PRIx64 ":%016" PRIx64, expiry, authenticated_user_id);
  if (length != TURN_EPHEMERAL_USERNAME_SIZE - 1 ||
      !TurnEphemeralCredentialDerivePassword(realm, secret, issued->username, issued->password)) {
    TurnEphemeralCredentialClear(issued);
    return false;
  }

  issued->expires_at = expires_at;
  return true;
}
