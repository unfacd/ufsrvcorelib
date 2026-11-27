#ifndef UFSRVCORELIB_SFU_PEER_CONFIG_H
#define UFSRVCORELIB_SFU_PEER_CONFIG_H

#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_server.h>

typedef enum {
  SFU_PEER_CONFIG_INACTIVE,
  SFU_PEER_CONFIG_PUBLISH,
  SFU_PEER_CONFIG_SUBSCRIBE
} SfuPeerConfigDirection;

typedef struct {
  const char *mid;
  SfuPeerConfigDirection direction;
  SfuTrackKind kind;
  uint32_t clock_rate;
  uint8_t payload_type;
  uint8_t mid_extension_id;
  uint8_t audio_level_extension_id; ///< RFC 6464 publisher mapping, zero when absent
  bool audio_level_allow_mixed; ///< RFC 8285 mixed extension forms were negotiated
  const char *publisher_ufrag;
  const char *publisher_mid;
  uint32_t publisher_ssrc; /* Optional ingress hint. Zero means learn by MID. */
  uint32_t outbound_ssrc;
} SfuPeerConfigMediaV1;

typedef struct {
  uint64_t revision;
  uint64_t room_id;
  const char *ufrag; /* Stable local peer key, including across ICE restarts. */
  const char *remote_ufrag;
  const char *remote_password;
  uint8_t fingerprint[32];
  size_t media_count;
  const SfuPeerConfigMediaV1 *media;
} SfuPeerConfigV1;

typedef enum {
  SFU_PEER_CONFIG_APPLIED,
  SFU_PEER_CONFIG_REJECTED,
  SFU_PEER_CONFIG_CONFLICT,
  SFU_PEER_CONFIG_UNAVAILABLE
} SfuPeerConfigResult;

/* Atomically apply a complete negotiated peer revision on the owning shard.
 * The room and any referenced publisher MIDs must already exist on this shard.
 * Existing media entries retain their order and identity. Mark removed entries
 * inactive. Use SfuServerRestartIce to prepare new remote credentials first.
 * Input is copied before dispatch and may be released when this returns.
 * APPLIED means the revision is active, or an identical retry is already active.
 * UNAVAILABLE also covers an unknown outcome, which is safe to retry verbatim. */
PUBLIC_API SfuPeerConfigResult SfuServerApplyPeerConfigV1(SfuServer *server, size_t shard_index,
                                                         const SfuPeerConfigV1 *config);

/* Atomically apply a revision and return its local ICE credentials. Create the
 * room on first admission and prepare a restart only after validating media.
 * The caller supplies an initialized buffer for the ICE JSON document. */
PUBLIC_API SfuPeerConfigResult SfuServerApplyNegotiatedPeerConfigV1(SfuServer *server, size_t shard_index,
                                                                   const SfuPeerConfigV1 *config,
                                                                   BufferDescriptor *ice);

#endif
