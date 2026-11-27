#ifndef UFSRVCORELIB_SFU_SIGNALING_GRANT_H
#define UFSRVCORELIB_SFU_SIGNALING_GRANT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <uflib/uflib_defs.h>

#define SFU_GRANT_ID_MAX 128u
#define SFU_GRANT_LIFETIME_SECONDS 600

typedef struct SfuGrantVerifier SfuGrantVerifier;

typedef enum {
  SFU_GRANT_CONFERENCE_START = 1u << 0,
  SFU_GRANT_CONFERENCE_JOIN = 1u << 1,
  SFU_GRANT_MEDIA_PUBLISH = 1u << 2,
  SFU_GRANT_MEDIA_SUBSCRIBE = 1u << 3
} SfuGrantScope;

typedef struct {
  char subject[SFU_GRANT_ID_MAX + 1];
  char conference_id[SFU_GRANT_ID_MAX + 1];
  uint32_t scopes;
} SfuSignalingGrant;

/** Load the chat issuer's public key. The verifier is read-only after creation. */
PUBLIC_API SfuGrantVerifier *SfuGrantVerifierCreate(const char *public_key_path, const char *issuer,
                                                    const char *audience, char *error, size_t error_size);
/** Create the same verifier from the base64url public key text supplied by configuration.
 * The caller may release the input strings after creation. */
PUBLIC_API SfuGrantVerifier *SfuGrantVerifierCreateFromPublicKey(const char *public_key, const char *issuer,
                                                                 const char *audience, char *error, size_t error_size);
PUBLIC_API void SfuGrantVerifierDestroy(SfuGrantVerifier *verifier);

/** Verify the Authorization header for one signaling request and copy its claims into grant. */
PUBLIC_API bool SfuGrantVerifyBearer(const SfuGrantVerifier *verifier, const char *authorization,
                                     SfuSignalingGrant *grant);

/** Check conference binding and all requested scopes. Session ownership is checked by the caller. */
PUBLIC_API bool SfuGrantAllows(const SfuSignalingGrant *grant, const char *conference_id, uint32_t scopes);

#endif
