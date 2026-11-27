/**
 * @file sfu_shard_rtcp.h
 * @brief The shard's RTCP receive path.
 *
 * Stage 2 so far: authenticate (SRTCP), walk the compound, cache sender reports
 * and count everything. Replies - receiver reports, relayed SRs, forwarded PLI -
 * arrive in later steps; this one exists so RTCP stops being a counter and starts
 * being parsed, with every drop path visible in the shard's counters.
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

#ifndef UFSRVCORELIB_SFU_SHARD_RTCP_H
#define UFSRVCORELIB_SFU_SHARD_RTCP_H

#include "model/shard_type.h"
#include "model/packet_type.h"
#include "srtp/sfu_shard_srtp.h"

/**
 * @brief Authenticate and parse one RTCP datagram from @p peer.
 *
 * @param shard    Shard receiving it; shard-thread only.
 * @param peer     Peer the flow table resolved; its SRTP context carries SRTCP.
 * @param packet   Receive slot whose payload is the SRTCP packet.
 * @return SFU_MEDIA_DROPPED always for now: nothing is sent in stage 2.4.
 */
SfuMediaOutcome SfuShardHandleRtcp(SfuShard *shard, SfuPeer *peer, SfuPacket *packet);

/** A publisher that has sent no SR this long after its first RTP is counted. */
#define SFU_RTCP_SR_MISSING_AFTER_MS 5000u

/**
 * @brief Emit receiver reports and age the no-SR counter.
 *
 * Runs once per turn from the shard's own tick and does nothing until the
 * deadline the previous run scheduled has passed. One report per peer per
 * published track, on the jittered cadence: the SFU reporting on what it
 * received, independent of any subscription.
 *
 * @param shard  Shard whose tracks are reported.
 * @param now_ms Shard monotonic milliseconds.
 */
void SfuShardRtcpSweep(SfuShard *shard, uint64_t now_ms);

/**
 * @brief Ask the publisher of a freshly bound subscription for a keyframe.
 *
 * Called from the subscribe path, where the publisher and its track are already
 * resolved for the binding being created. The request obeys the same
 * per-publisher throttle as a subscriber-driven PLI: inside the window it is
 * suppressed (`rtcp_pli_suppressed`) and the window is not extended; outside it
 * one PLI is sent and stamps the window. Refusals — publisher not connected, no
 * outbound SRTP session, no tuple, not in a room, no track — are counted as
 * `rtcp_pli_dropped`; every attempt counts `pli_on_join`. An audio track is
 * skipped outright: its frames are independent and no keyframe exists.
 *
 * @param shard           Shard applying the subscription.
 * @param publisher       Peer whose track is being subscribed to.
 * @param publisher_ssrc  The SSRC the subscriber named.
 */
void SfuShardPliOnJoin(SfuShard *shard, SfuPeer *publisher, uint32_t publisher_ssrc);

#endif /* UFSRVCORELIB_SFU_SHARD_RTCP_H */
