/**
 * @file sfu_shard_track.h
 * @brief Reception statistics for one publisher track.
 *
 * The RTP path updates a track after a packet is authenticated, and the RTCP
 * timer later reads those numbers to build a receiver report. Keeping the
 * arithmetic here means the sequence extension and the jitter estimator can be
 * tested without a socket, a session or a shard.
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

#ifndef UFSRVCORELIB_SFU_SHARD_TRACK_H
#define UFSRVCORELIB_SFU_SHARD_TRACK_H

#include <stdint.h>

#include "model/track_type.h"

/**
 * @brief Apply one accepted RTP packet to a track's reception statistics.
 *
 * Extends the 16-bit sequence number (RFC 3550 A.1, including a resync on an
 * implausible jump) and updates the interarrival jitter (A.8) in RTP timestamp
 * units, using the track's clock rate.
 *
 * @param track      Track the packet belongs to; NULL is ignored.
 * @param sequence   RTP sequence number, host order.
 * @param timestamp  RTP timestamp, host order.
 * @param arrival_ns Monotonic arrival time in nanoseconds.
 */
void SfuTrackOnRtp(SfuPublisherTrack *track, uint16_t sequence, uint32_t timestamp, uint64_t arrival_ns);

#endif /* UFSRVCORELIB_SFU_SHARD_TRACK_H */
