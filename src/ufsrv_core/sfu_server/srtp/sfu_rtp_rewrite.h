#ifndef UFSRVCORELIB_SFU_RTP_REWRITE_H
#define UFSRVCORELIB_SFU_RTP_REWRITE_H

#include <arpa/inet.h>
#include <string.h>

#include "model/peer_type.h"

typedef struct SfuRtpMapping {
  const char *mid;
  uint8_t payload_type;
  uint8_t mid_extension_id;
} SfuRtpMapping;

/* V1 negotiates MID only. Other source extensions have no destination mapping.
 * With a legacy binding, the RTP header extensions pass through unchanged. */
static inline size_t
SfuRtpRewrite(uint8_t *out, const uint8_t *in, size_t length, const SfuSubscription *subscription,
              uint16_t out_sequence, const SfuRtpMapping *mapping)
{
  if (length > SFU_PACKET_CAPACITY) return 0;
  const size_t payload_offset = SfuRtpPayloadOffset(in, length);
  if (!payload_offset) return 0;

  size_t rewritten = length;
  if (mapping->mid) {
    const size_t base = 12u + 4u * (in[0] & 0x0fu);
    const size_t mid_length = strlen(mapping->mid);
    const bool two_byte = mapping->mid_extension_id > 14u;
    const size_t element_header = two_byte ? 2u : 1u;
    const size_t extension_bytes = (element_header + mid_length + 3u) & ~(size_t)3u;
    const size_t header = base + 4u + extension_bytes;
    rewritten = header + length - payload_offset;
    if (rewritten > SFU_PACKET_CAPACITY) return 0;

    memcpy(out, in, base);
    out[0] |= 0x10u;
    out[1] = (uint8_t)((in[1] & 0x80u) | mapping->payload_type);
    uint8_t *extension = out + base;
    extension[0] = two_byte ? 0x10u : 0xbeu;
    extension[1] = two_byte ? 0 : 0xdeu;
    extension[2] = 0;
    extension[3] = (uint8_t)(extension_bytes / 4u);
    if (two_byte) {
      extension[4] = mapping->mid_extension_id;
      extension[5] = (uint8_t)mid_length;
    } else {
      extension[4] = (uint8_t)((mapping->mid_extension_id << 4) | (mid_length - 1u));
    }
    memcpy(extension + 4u + element_header, mapping->mid, mid_length);
    memset(extension + 4u + element_header + mid_length, 0, extension_bytes - element_header - mid_length);
    memcpy(out + header, in + payload_offset, length - payload_offset);
  } else {
    memcpy(out, in, length);
  }
  const uint32_t out_ssrc = htonl(subscription->out_ssrc);
  memcpy(out + 8u, &out_ssrc, sizeof(out_ssrc));
  out[2] = (uint8_t)(out_sequence >> 8);
  out[3] = (uint8_t)out_sequence;
  return rewritten;
}

#endif
