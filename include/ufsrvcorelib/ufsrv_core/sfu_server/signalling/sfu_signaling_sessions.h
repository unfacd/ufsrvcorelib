#ifndef UFSRVCORELIB_SFU_SIGNALING_SESSIONS_H
#define UFSRVCORELIB_SFU_SIGNALING_SESSIONS_H

#include <stddef.h>
#include <stdint.h>

#include <uflib/uflib_defs.h>

#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_server.h>
#include <ufsrvcorelib/ufsrv_core/sfu_server/signalling/sfu_signaling_grant.h>

#define SFU_SIGNALING_SESSION_ID_LENGTH 32u
#define SFU_SIGNALING_SDP_LIMIT (128u * 1024u)

typedef struct SfuSignalingSessions SfuSignalingSessions;

typedef enum {
  SFU_SIGNALING_SESSION_CREATED,
  SFU_SIGNALING_SESSION_INVALID,
  SFU_SIGNALING_SESSION_UNAUTHORIZED,
  SFU_SIGNALING_SESSION_FORBIDDEN,
  SFU_SIGNALING_SESSION_NOT_FOUND,
  SFU_SIGNALING_SESSION_UNAVAILABLE,
  SFU_SIGNALING_SESSION_OFFER_APPLIED,
  SFU_SIGNALING_SESSION_CONFLICT,
  SFU_SIGNALING_PUBLICATIONS_LISTED,
  SFU_SIGNALING_SESSION_CLOSED
} SfuSignalingSessionStatus;

typedef struct {
  char session_id[SFU_SIGNALING_SESSION_ID_LENGTH + 1];
  uint64_t room_id;
} SfuSignalingSession;

typedef enum {
  SFU_SIGNALING_PUBLISHER,
  SFU_SIGNALING_SUBSCRIBER
} SfuSignalingRole;

typedef struct {
  const char *mid;
  const char *publication_id;
} SfuSignalingSubscription;

typedef struct {
  char publication_id[SFU_SIGNALING_SESSION_ID_LENGTH + 1];
  char publisher_id[SFU_GRANT_ID_MAX + 1];
  SfuTrackKind kind;
} SfuSignalingPublication;

PUBLIC_API SfuSignalingSessionStatus SfuSignalingOpenSession(SfuSignalingSessions *sessions,
                                                               const char *authorization, const char *conference_id,
                                                               SfuSignalingRole role, const char *idempotency_key,
                                                               SfuSignalingSession *result);

/** Return the authorized conference's acknowledged publications on live shard peers.
 * The caller frees the returned array, which may be NULL for an empty result. */
PUBLIC_API SfuSignalingSessionStatus SfuSignalingListPublications(SfuSignalingSessions *sessions,
                                                                    SfuServer *server, const char *authorization,
                                                                    const char *conference_id,
                                                                    SfuSignalingPublication **publications, size_t *count);

/** Negotiate an offer revision using the session's role. Subscriber bindings are keyed by receiving MID.
 * The caller frees the returned SDP answer. Retry an unknown shard outcome with the same offer and bindings. */
PUBLIC_API SfuSignalingSessionStatus SfuSignalingApplyOffer(SfuSignalingSessions *sessions,
                                                              SfuServer *server, const char *authorization,
                                                              const char *session_id, uint64_t revision,
                                                              const char *sdp,
                                                              const SfuSignalingSubscription *subscriptions,
                                                              size_t count, char **answer);

PUBLIC_API SfuSignalingSessions *SfuSignalingSessionsCreate(const char *public_key, const char *issuer,
                                                            const char *audience, char *error, size_t error_size);
PUBLIC_API void SfuSignalingSessionsDestroy(SfuSignalingSessions *sessions);

/** Authorize and record a publisher session. The room ID is reserved in signaling;
 * the shard receives the media configuration when an offer is accepted. */
PUBLIC_API SfuSignalingSessionStatus SfuSignalingOpenPublisher(SfuSignalingSessions *sessions,
                                                               const char *authorization, const char *conference_id,
                                                               const char *idempotency_key, SfuSignalingSession *result);

/** Negotiate a publisher offer revision. On success the caller owns *answer and frees it.
 * Identical retries return the committed answer. After UNAVAILABLE retry the same offer. */
PUBLIC_API SfuSignalingSessionStatus SfuSignalingApplyPublisherOffer(SfuSignalingSessions *sessions,
                                                                     SfuServer *server, const char *authorization,
                                                                     const char *session_id, uint64_t revision,
                                                                     const char *sdp, char **answer);

/** Release the owner's session on the shard. Authorized retries return CLOSED for ten minutes. */
PUBLIC_API SfuSignalingSessionStatus SfuSignalingCloseSession(SfuSignalingSessions *sessions,
                                                               SfuServer *server, const char *authorization,
                                                               const char *session_id);

#endif
