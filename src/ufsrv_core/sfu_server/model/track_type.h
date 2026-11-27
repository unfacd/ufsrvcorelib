/**
 * @file track_type.h
 * @brief One publisher track: the per-SSRC record RTCP observes and reports on.
 *
 * The peer owns the connection (ICE, DTLS, SRTP); a track owns one SSRC and the
 * reception state the shard accumulates for it. Everything RTCP needs about an
 * inbound stream lives here, so the RR timer reads a record rather than
 * recomputing from packet history, and the NACK retention and keyframe work can
 * hang their own fields off the same record instead of adding parallel arrays.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRVCORELIB_TRACK_TYPE_H
#define UFSRVCORELIB_TRACK_TYPE_H

#include <stdbool.h>
#include <stdint.h>

#include <ufsrvcorelib/ufsrv_core/sfu_server/model/command_type.h>

#include "nack_type.h"

/** SSRCs one peer may publish (simulcast included). */
#define SFU_PEER_TRACK_MAX 32u

/**
 * @brief One publisher track: identity, timing and the reception state RFC 3550 reports.
 */
typedef struct SfuPublisherTrack {
  uint32_t ssrc;              ///< the published synchronization source
  uint32_t clock_rate;        ///< RTP timestamp units per second; jitter is in these

  uint32_t max_seq_ext;       ///< highest extended sequence number seen
  uint64_t packets_received;  ///< accepted RTP packets since the track was learned
  int32_t  expected_prior;    ///< packets expected at the previous report
  uint32_t received_prior;    ///< packets received at the previous report
  uint32_t jitter_q4;         ///< interarrival jitter, Q4, in RTP timestamp units
  uint64_t first_rtp_ns;      ///< monotonic arrival of the first packet; 0 until then

  uint16_t base_seq;          ///< first sequence of the stream (RFC 3550 A.1)
  uint16_t max_seq;           ///< highest sequence within the current cycle
  uint16_t bad_seq;           ///< resync marker for a huge jump
  uint32_t cycles;            ///< 65536 per completed sequence cycle
  bool     have_seq;          ///< false until the first accepted packet

  uint32_t last_timestamp;    ///< previous RTP timestamp, for A.8 jitter
  uint32_t last_arrival_rtp;  ///< previous arrival in RTP timestamp units
  bool     have_arrival;      ///< false until the first packet is timed

  uint32_t last_sr_ntp_mid32; ///< middle 32 bits of the publisher's last SR NTP timestamp
  uint64_t last_sr_rx_ns;     ///< monotonic arrival of that SR, for the RR's DLSR
  bool     sr_seen;           ///< true once any SR has been accepted for this track

  uint64_t last_pli_ms;       ///< per-publisher PLI throttle, so N subscribers coalesce

  struct {
    uint64_t generation;
    uint64_t last_sample_ns;
    uint64_t last_voice_ns;
    double average;
    uint8_t extension_id;
    uint8_t form; ///< 0 before the first valid sample, 1 or 2 thereafter
    bool allow_mixed;
  } audio_level;

  uint8_t  kind;              ///< SfuTrackKind
  bool     sr_missing_counted;///< the no-SR-within-N-seconds counter fired once
  bool     ring_denied;       ///< no ring was available; counted once

  SfuNackRing *ring;          ///< retained packets, NULL when this track keeps none
} SfuPublisherTrack;

/**
 * @brief RTP clock for a track kind.
 *
 * The default signalling gets until an endpoint's SDP rtpmap is parsed: 90 kHz
 * video, 48 kHz audio. Jitter and SR timestamps are meaningless without it.
 *
 * @param kind Track kind the peer declared, or the learn path's audio default.
 * @return Timestamp units per second.
 */
static inline uint32_t
SfuTrackClockRate(SfuTrackKind kind)
{
  return kind == SFU_TRACK_AUDIO ? 48000u : 90000u;
}

#endif /* UFSRVCORELIB_TRACK_TYPE_H */
