#ifndef UFSRVCORELIB_SFU_PEER_CONFIG_PRIV_H
#define UFSRVCORELIB_SFU_PEER_CONFIG_PRIV_H

#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_peer_config.h>

#define SFU_CONFIG_MEDIA_MAX 32u
#define SFU_CONFIG_MID_MAX 16u

typedef struct {
  char mid[SFU_CONFIG_MID_MAX + 1u];
  SfuPeerConfigDirection direction;
  SfuTrackKind kind;
  uint32_t clock_rate;
  uint8_t payload_type;
  uint8_t mid_extension_id;
  uint8_t audio_level_extension_id; ///< RFC 6464 publisher mapping, zero when absent
  bool audio_level_allow_mixed; ///< RFC 8285 mixed extension forms were negotiated
  char publisher_ufrag[SFU_COMMAND_UFRAG_MAX];
  char publisher_mid[SFU_CONFIG_MID_MAX + 1u];
  uint32_t publisher_ssrc;
  uint32_t outbound_ssrc;
  uint32_t bound_ssrc; /* Runtime publication binding, excluded from retry comparison. */
} SfuPeerConfigSection;

typedef struct SfuPeerConfigSnapshot {
  uint64_t revision;
  uint64_t room_id;
  char ufrag[SFU_COMMAND_UFRAG_MAX];
  char remote_ufrag[SFU_COMMAND_UFRAG_MAX];
  char remote_password[SFU_COMMAND_PASSWORD_MAX];
  uint8_t fingerprint[SFU_COMMAND_FINGERPRINT_SIZE];
  size_t media_count;
  SfuPeerConfigSection media[SFU_CONFIG_MEDIA_MAX];
} SfuPeerConfigSnapshot;

#endif
