#ifndef UFSRVCORELIB_SFU_SDP_PRIV_H
#define UFSRVCORELIB_SFU_SDP_PRIV_H

#include "../sfu_peer_config_priv.h"

#define SFU_SDP_MID_BYTES 17u
#define SFU_SDP_TOKEN_BYTES 64u
#define SFU_SDP_FINGERPRINT_BYTES 96u
#define SFU_SDP_ANSWER_BYTES (512u + SFU_CONFIG_MEDIA_MAX * 480u)

typedef struct {
  SfuPeerConfigMediaV1 media[SFU_CONFIG_MEDIA_MAX];
  size_t count;
  size_t bundled[SFU_CONFIG_MEDIA_MAX];
  bool session_extmap_allow_mixed;
  const char *remote_ufrag;
  const char *remote_password;
  uint8_t fingerprint[32];
} SfuSdpOffer;

typedef struct {
  char address[46];
  char fingerprint[SFU_SDP_FINGERPRINT_BYTES];
  char ice_ufrag[SFU_SDP_TOKEN_BYTES];
  char ice_password[SFU_SDP_TOKEN_BYTES];
  uint16_t port;
  bool ipv6;
} SfuSdpLocalTransport;

/* Parsed strings borrow storage from the mutable offer buffer. */
bool SfuSdpParseOffer(char *sdp, SfuPeerConfigDirection direction, SfuSdpOffer *offer);
bool SfuSdpReadLocalTransport(SfuServer *server, size_t shard, SfuSdpLocalTransport *transport);
bool SfuSdpReadAcknowledgedIce(const char *document, SfuSdpLocalTransport *transport);
bool SfuSdpWriteAnswer(const SfuSdpOffer *offer, const SfuSdpLocalTransport *local,
                       const SfuPeerConfigMediaV1 *media, const char *stream_id,
                       uint64_t session_origin, uint64_t revision, char *answer, size_t capacity);

#endif
