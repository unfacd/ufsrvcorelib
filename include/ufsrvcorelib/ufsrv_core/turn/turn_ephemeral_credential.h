#ifndef UFSRVCORELIB_TURN_EPHEMERAL_CREDENTIAL_H
#define UFSRVCORELIB_TURN_EPHEMERAL_CREDENTIAL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "ufsrvcorelib_defs.h"

enum {
  TURN_EPHEMERAL_SECRET_LENGTH = 32,
  TURN_EPHEMERAL_USERNAME_SIZE = 34,
  TURN_EPHEMERAL_PASSWORD_SIZE = 45,
  TURN_EPHEMERAL_REALM_MAX_LENGTH = 763
};

typedef struct TurnEphemeralCredential
{
  char username[TURN_EPHEMERAL_USERNAME_SIZE];
  char password[TURN_EPHEMERAL_PASSWORD_SIZE];
  time_t expires_at;
} TurnEphemeralCredential;

PUBLIC_API bool TurnEphemeralCredentialIssue(const char *realm,
    const uint8_t secret[TURN_EPHEMERAL_SECRET_LENGTH], uint32_t lifetime_seconds,
    uint64_t authenticated_user_id, time_t now, TurnEphemeralCredential *issued);
PUBLIC_API bool TurnEphemeralCredentialDerivePassword(const char *realm,
    const uint8_t secret[TURN_EPHEMERAL_SECRET_LENGTH], const char *username,
    char password[TURN_EPHEMERAL_PASSWORD_SIZE]);
PUBLIC_API void TurnEphemeralCredentialClear(TurnEphemeralCredential *issued);

#endif
