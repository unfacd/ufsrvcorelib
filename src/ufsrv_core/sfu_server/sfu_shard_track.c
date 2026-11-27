/**
 * @file sfu_shard_track.c
 * @brief The RFC 3550 reception statistics the receiver report is built from.
 *
 * Two calculations live here. Sequence extension follows A.1: a small forward
 * delta advances the highest sequence, a backward delta starts a new cycle, and
 * a jump too large to be real is treated as a resync rather than as 65 000 lost
 * packets. Jitter follows A.8: the arrival time is converted to RTP timestamp
 * units with the track's clock rate, and the smoothed estimate is kept in Q4 so
 * the arithmetic stays integral on the packet path.
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

#include <stddef.h>

#include "sfu_shard_track.h"

#define SFU_TRACK_MAX_DROPOUT 3000u   /* A.1: larger forward jumps resync */
#define SFU_TRACK_RESYNC_GAP  1000u   /* A.1: smaller backward gaps are reorder */

/** Arrival in RTP timestamp units. Clock rates are kilohertz multiples (48k, 90k). */
static uint32_t
sArrivalInRtp(uint64_t arrival_ns, uint32_t clock_rate)
{
  const uint64_t milliseconds = arrival_ns / 1000000u;
  const uint32_t units_per_ms = clock_rate ? clock_rate / 1000u : 1u;
  return (uint32_t)(milliseconds * units_per_ms);
}

void
SfuTrackOnRtp(SfuPublisherTrack *track, uint16_t sequence, uint32_t timestamp, uint64_t arrival_ns)
{
  if (!track) return;

  if (!track->have_seq) {
    track->have_seq = true;
    track->base_seq = sequence;
    track->max_seq = sequence;
    track->max_seq_ext = sequence;
    track->packets_received = 1;
    if (!track->first_rtp_ns) track->first_rtp_ns = arrival_ns;
  } else {
    const uint16_t delta = (uint16_t)(sequence - track->max_seq);
    if (delta < SFU_TRACK_MAX_DROPOUT) {
      if (sequence < track->max_seq) track->cycles += 65536u; /* wrapped forward */
      track->max_seq = sequence;
      track->max_seq_ext = track->cycles + sequence;
      track->packets_received++;
    } else if (delta > 65536u - SFU_TRACK_RESYNC_GAP) {
      /* Late or reordered packet: it counts as received but cannot advance the
       * highest sequence, so the report's expected count is unaffected. */
      track->packets_received++;
    } else {
      /* Implausible forward jump: treat it as a new stream numbering. */
      track->bad_seq = (uint16_t)(sequence + 1u);
      track->base_seq = sequence;
      track->max_seq = sequence;
      track->cycles = 0;
      track->max_seq_ext = sequence;
      track->packets_received++;
    }
  }

  if (track->have_arrival) {
    const uint32_t arrival_rtp = sArrivalInRtp(arrival_ns, track->clock_rate);
    /* D = (arrival_j - arrival_i) - (timestamp_j - timestamp_i), both in RTP units. */
    int64_t difference = (int64_t)arrival_rtp - (int64_t)track->last_arrival_rtp;
    difference -= (int64_t)(int32_t)(timestamp - track->last_timestamp);
    const int64_t magnitude = difference < 0 ? -difference : difference;
    const int32_t sample_q4 = (int32_t)(magnitude << 4);
    track->jitter_q4 += (uint32_t)((sample_q4 - (int32_t)track->jitter_q4) / 16);
    track->last_arrival_rtp = arrival_rtp;
  } else {
    track->have_arrival = true;
    track->last_arrival_rtp = sArrivalInRtp(arrival_ns, track->clock_rate);
  }
  track->last_timestamp = timestamp;
}
