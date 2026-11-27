/**
 * @file peer_type.h
 * @brief One WebRTC peer association owned by a shard.
 *
 * Everything in here is touched by the owning shard only: the DTLS context, the
 * SRTP sessions and the retransmission cache never need a lock. libsrtp is
 * forward-declared so that including this header does not make libsrtp a
 * dependency of every ufsrvcorelib consumer; the engine implementation that
 * drives SRTP includes <srtp3/srtp.h> itself.
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

#ifndef UFSRVCORELIB_PEER_TYPE_H
#define UFSRVCORELIB_PEER_TYPE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include <uflib/standard_ssl_includes.h>
#include <ufsrvcorelib/ufsrv_core/sfu_server/model/shard_specs_type.h>

#include "dtls_state_type.h"
#include "track_type.h"
#include "packet_type.h"

typedef struct srtp_ctx_t_ srtp_ctx_t;
typedef srtp_ctx_t *       srtp_t;

#define SFU_NACK_CACHE    512u

/* ICE credentials are provisioning data, not test data: real endpoints use a
 * 4-16 character ufrag and a 22-32 character password. 64 leaves headroom for
 * every endpoint seen in the wild without bloating the command pool. */
#define SFU_UFRAG_MAX     64u
#define SFU_PASSWORD_MAX  64u

/* Replay guard and rebind policy constants, see sfu_shard_ice.c. */
#define SFU_PEER_SUBS_MAX          8u   /* publisher SSRCs one subscriber may pull */

/** Exact negotiated header used when a retained packet was protected. */
typedef struct SfuRtpSentMapping {
  uint64_t serial;
  uint8_t payload_type;
  uint8_t mid_extension_id;
} SfuRtpSentMapping;

/** One inbound publisher track mapped to one outbound SSRC on this peer. */
typedef struct SfuSubscription {
  struct SfuPeer *publisher;   ///< Negotiated source; NULL for legacy bindings
  size_t publisher_media_index;
  size_t subscriber_media_index;
  SfuRtpSentMapping *sent_mapping; ///< Indexed by publisher sequence, at video retention capacity.
  uint32_t publisher_ssrc;
  uint32_t out_ssrc;
  uint16_t seq_offset;   ///< applied to every forwarded sequence number
  uint32_t ts_offset;    ///< added to every forwarded timestamp and its SR
  bool     active;
  uint32_t sent_packets;    ///< RTP packets sent under out_ssrc, modulo 2^32
  uint32_t sent_octets;     ///< RTP payload octets sent under out_ssrc, modulo 2^32
  uint32_t nack_window_ms;    ///< start of the current retransmit window
  uint32_t nack_window_count; ///< retransmits answered inside it
  uint16_t last_nacked_seq;   ///< the last outbound sequence answered
  uint64_t last_nack_ns;      ///< when it was answered
} SfuSubscription;

#define SFU_PEER_TID_RING          16u
#define SFU_ICE_TRANSACTION_ID_SIZE 12u

#define SFU_ICE_RESTART_UFRAG_MAX  32u

typedef struct SfuIceTransaction {
  uint8_t id[SFU_ICE_TRANSACTION_ID_SIZE];
  char local_ufrag[SFU_ICE_RESTART_UFRAG_MAX + 1u];
  struct sockaddr_storage source;
} SfuIceTransaction;


typedef struct SfuIceGeneration {
  char local_ufrag[SFU_ICE_RESTART_UFRAG_MAX + 1u];
  char local_password[SFU_ICE_PWD_LENGTH + 1u];
  char remote_ufrag[SFU_UFRAG_MAX + 1u];
  char remote_password[SFU_PASSWORD_MAX + 1u];
} SfuIceGeneration;

/** ICE state of one peer association. */
typedef enum SfuPeerState {
  SFU_PEER_STATE_NEW = 0,   ///< registered with credentials, no nomination yet
  SFU_PEER_STATE_CONNECTED, ///< a nominated pair exists; the tuple is bound
  SFU_PEER_STATE_FREE       ///< slot is on the free list, not a peer
} SfuPeerState;

typedef struct SfuPeer {
  struct sockaddr_storage address;
  socklen_t               address_length;
  SfuPeerState            state;
  uint32_t                slot_next;      /* free-list link, slot index + 1 */
  SfuPeerDtls             dtls;      /* lazily created; see sfu_shard_dtls.c */
  srtp_t                  inbound;   /* ssrc_any_inbound */
  srtp_t                  outbound;  /* ssrc_any_outbound, allow_repeat_tx */
  struct SfuPeerConfigSnapshot *config;
  bool                    event_connected; /* DTLS-ready room association */
  struct SfuRoom *        room;            /* NULL until the peer joins a room */
  SfuPublisherTrack       tracks[SFU_PEER_TRACK_MAX];
  size_t                  track_count;
  SfuSubscription         subscriptions[SFU_PEER_SUBS_MAX];
  size_t                  subscription_count;
  SfuPacket *             nack[SFU_NACK_CACHE];  /* plaintext, ring */
  uint32_t                nack_head;
  uint32_t                nack_count;
  uint64_t                last_rx_ms;      /* last authenticated datagram */
  uint64_t                last_consent_ms; /* last authenticated binding request */

  /* Provisioning: set when the signalling command creates the peer. */
  char     ufrag[SFU_UFRAG_MAX + 1u];
  char     local_pwd[SFU_PASSWORD_MAX + 1u];
  uint8_t  ufrag_length;
  uint8_t  password_length;

  /* Recent authenticated transactions. Only the original source can retry. */
  SfuIceTransaction tid_ring[SFU_PEER_TID_RING];
  uint8_t  tid_next;

  SfuIceGeneration ice_current;
  SfuIceGeneration ice_pending;
  uint64_t restart_started_ms;
} SfuPeer;

/**
 * @brief Find the peer's track for @p ssrc.
 *
 * @param peer Peer owned by the shard thread.
 * @param ssrc Publisher SSRC to resolve; zero is never a track.
 * @return The track, or NULL when the peer has not published that SSRC.
 */
static inline SfuPublisherTrack *
SfuPeerTrackFor(SfuPeer *peer, uint32_t ssrc)
{
  if (!peer || !ssrc) return NULL;
  for (size_t index = 0; index < peer->track_count; index++) {
    if (peer->tracks[index].ssrc == ssrc) return &peer->tracks[index];
  }
  return NULL;
}

/**
 * @brief Register @p ssrc on @p peer, or restate its kind if it is already there.
 *
 * The two writers of a peer's track array are ADD_TRACK and the SRTP learn
 * path, so both go through here: one place decides the clock rate and the
 * ceiling, and a re-publish cannot reset the reception state.
 *
 * @param peer   Peer owned by the shard thread.
 * @param ssrc   Publisher SSRC; zero is rejected.
 * @param kind   Track kind the signalling path declared.
 * @return The track, or NULL when the peer is at SFU_PEER_TRACK_MAX.
 */
static inline SfuPublisherTrack *
SfuPeerTrackAppend(SfuPeer *peer, uint32_t ssrc, SfuTrackKind kind)
{
  SfuPublisherTrack *track = SfuPeerTrackFor(peer, ssrc);
  if (track) {
    track->kind = (uint8_t)kind;
    track->clock_rate = SfuTrackClockRate(kind);
    return track;
  }
  if (!peer || !ssrc || peer->track_count >= SFU_PEER_TRACK_MAX) return NULL;
  track = &peer->tracks[peer->track_count++];
  *track = (SfuPublisherTrack){.ssrc = ssrc,
                               .clock_rate = SfuTrackClockRate(kind),
                               .kind = (uint8_t)kind};
  return track;
}

#endif /* UFSRVCORELIB_PEER_TYPE_H */
