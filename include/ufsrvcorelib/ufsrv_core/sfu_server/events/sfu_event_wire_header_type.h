//
// Created by ayman on 27/11/2026.
//

/**
 * @file sfu_event_wire_header_type.h
 * @brief sfu_event_wire_header_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 27/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_SFU_EVENT_WIRE_HEADER_TYPE_H
#define UFSRVCORELIB_SFU_EVENT_WIRE_HEADER_TYPE_H

#include <stdint.h>
#include <string.h>
#include <netinet/in.h>

#include "sfu_event_type.h"
#include "sfu_disconnect_reason_enum.h"
/* V1 frames have a 24-byte header, room and peer IDs, then type-specific data.
 * Multi-byte fields are big-endian. Lengths count bytes without terminators.
 */
#define SFU_EVT_MAGIC 0x5346u
#define SFU_EVT_VER   1u
#define SFU_EVT_HEADER_SIZE 24u
#define SFU_EVT_FRAME_MAX (SFU_EVT_HEADER_SIZE + 3u * SFU_MAX_ID_LEN + 1u)

typedef struct __attribute__((packed)) {
  uint16_t magic;
  uint8_t  version;
  uint8_t  event_type;
  uint16_t payload_len;
  uint8_t  room_id_len;
  uint8_t  peer_id_len;
  uint32_t seq;
  uint64_t timestamp_ms; ///< Unix epoch milliseconds.
  uint8_t  detail;
  uint8_t  reserved[3];
} SfuEventWireHeader;

#ifdef __cplusplus
static_assert(sizeof(SfuEventWireHeader) == SFU_EVT_HEADER_SIZE, "SFU event V1 header size");
#else
_Static_assert(sizeof(SfuEventWireHeader) == SFU_EVT_HEADER_SIZE, "SFU event V1 header size");
#endif

static inline uint64_t SfuEventWireHostToNetwork64(uint64_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(value);
#else
  return value;
#endif
}

/** Encode one V1 record. Returns zero without writing on invalid input. */
static inline size_t
SfuEventWireEncodeRecord(uint8_t *out_buf, size_t out_cap, const SfuEvent *event)
{
  if (!out_buf || !event || event->room_id_len > SFU_MAX_ID_LEN ||
      event->peer_id_len > SFU_MAX_ID_LEN) return 0;
  if (event->type != SFU_EVT_EVENT_DELIVERY_GAP && !event->room_id_len) return 0;

  size_t extra_len = 0;
  uint64_t extra_number = 0;
  switch (event->type) {
    case SFU_EVT_PEER_CONNECTED:
      if (!event->peer_id_len || event->detail != 0) return 0;
      break;
    case SFU_EVT_PEER_DISCONNECTED:
      if (!event->peer_id_len || event->detail > SFU_DISC_ADMIN_KICK) return 0;
      break;
    case SFU_EVT_DOMINANT_SPEAKER:
      if (event->detail > 127 || (!event->peer_id_len && event->detail != 0)) return 0;
      break;
    case SFU_EVT_PUBLICATION_ADDED:
    case SFU_EVT_PUBLICATION_REMOVED:
      if (!event->peer_id_len || !event->data.publication.id_len ||
          event->data.publication.id_len > SFU_MAX_ID_LEN ||
          (event->detail != SFU_EVT_MEDIA_AUDIO && event->detail != SFU_EVT_MEDIA_VIDEO)) return 0;
      extra_len = 1u + event->data.publication.id_len;
      break;
    case SFU_EVT_ICE_RESTART_SUCCEEDED:
    case SFU_EVT_ICE_RESTART_FAILED:
      if (!event->peer_id_len || !event->data.ice_generation) return 0;
      if (event->type == SFU_EVT_ICE_RESTART_SUCCEEDED) {
        if (event->detail != 0) return 0;
      } else if (event->detail != SFU_EVT_ICE_RESTART_TIMEOUT &&
                 event->detail != SFU_EVT_ICE_RESTART_TRANSPORT_FAILED) return 0;
      extra_number = event->data.ice_generation;
      extra_len = sizeof(extra_number);
      break;
    case SFU_EVT_ROOM_IDLE:
      if (event->peer_id_len || event->detail != 0) return 0;
      break;
    case SFU_EVT_EVENT_DELIVERY_GAP:
      if (event->room_id_len || event->peer_id_len || !event->data.dropped_count ||
          event->detail < SFU_EVT_GAP_QUEUE_FULL || event->detail > SFU_EVT_GAP_SHUTDOWN_ABANDONED) return 0;
      extra_number = event->data.dropped_count;
      extra_len = sizeof(extra_number);
      break;
    default:
      return 0;
  }

  const size_t payload_len = (size_t)event->room_id_len + event->peer_id_len + extra_len;
  const size_t total = SFU_EVT_HEADER_SIZE + payload_len;
  if (out_cap < total) return 0;

  const SfuEventWireHeader header = {
    .magic        = htons(SFU_EVT_MAGIC),
    .version      = SFU_EVT_VER,
    .event_type   = (uint8_t)event->type,
    .payload_len  = htons((uint16_t)payload_len),
    .room_id_len  = event->room_id_len,
    .peer_id_len  = event->peer_id_len,
    .seq          = htonl(event->seq),
    .timestamp_ms = SfuEventWireHostToNetwork64(event->timestamp_ms),
    .detail       = event->detail,
    .reserved     = {0, 0, 0},
  };

  memcpy(out_buf, &header, sizeof(header));
  uint8_t *payload = out_buf + sizeof(header);
  memcpy(payload, event->room_id, event->room_id_len);
  payload += event->room_id_len;
  memcpy(payload, event->peer_id, event->peer_id_len);
  payload += event->peer_id_len;

  if (event->type == SFU_EVT_PUBLICATION_ADDED || event->type == SFU_EVT_PUBLICATION_REMOVED) {
    payload[0] = event->data.publication.id_len;
    memcpy(payload + 1, event->data.publication.id, event->data.publication.id_len);
  } else if (extra_len) {
    const uint64_t network = SfuEventWireHostToNetwork64(extra_number);
    memcpy(payload, &network, sizeof(network));
  }
  return total;
}

/** Compatibility entry point for records with no type-specific payload. */
static inline size_t
SfuEventWireEncode(uint8_t *out_buf, size_t out_cap, uint8_t event_type, uint8_t detail,
                   uint32_t seq, uint64_t ts_ms, const char *room_id, uint8_t room_id_len,
                   const char *peer_id, uint8_t peer_id_len)
{
  if (room_id_len > SFU_MAX_ID_LEN || peer_id_len > SFU_MAX_ID_LEN ||
      (room_id_len && !room_id) || (peer_id_len && !peer_id) ||
      (event_type != SFU_EVT_ROOM_IDLE &&
       (event_type < SFU_EVT_PEER_CONNECTED || event_type > SFU_EVT_DOMINANT_SPEAKER))) return 0;

  SfuEvent event = {
    .seq = seq, .timestamp_ms = ts_ms, .type = (SfuEventTypeEnum)event_type, .detail = detail,
    .room_id_len = room_id_len, .peer_id_len = peer_id_len, .room_id = {0}, .peer_id = {0},
    .data = {.publication = {0, {0}}},
  };
  if (room_id_len) memcpy(event.room_id, room_id, room_id_len);
  if (peer_id_len) memcpy(event.peer_id, peer_id, peer_id_len);
  return SfuEventWireEncodeRecord(out_buf, out_cap, &event);
}

#endif //UFSRVCORELIB_SFU_EVENT_WIRE_HEADER_TYPE_H
