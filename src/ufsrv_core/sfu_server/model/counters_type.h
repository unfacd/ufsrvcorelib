/**
 * @file counters_type.h
 * @brief Per-shard counters, read by the console and by operators.
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

#ifndef UFSRVCORELIB_COUNTERS_TYPE_H
#define UFSRVCORELIB_COUNTERS_TYPE_H

#include <stdint.h>

typedef struct SfuCounters {
  uint64_t rx_datagrams;
  uint64_t tx_datagrams;
  uint64_t rx_batches;
  uint64_t tx_batches;
  uint64_t drops_slab;
  uint64_t drops_queue;
  uint64_t drops_truncated;
  uint64_t drops_send;
  uint64_t drops_no_peer;
  uint64_t commands_applied;
  uint64_t commands_dropped;
  uint64_t rx_stun;
  uint64_t rx_dtls;
  uint64_t rx_rtp;
  uint64_t rx_rtcp;
  uint64_t rtcp_packets_parsed;   /* packets inside parsed compounds */
  uint64_t rtcp_before_srtp;      /* RTCP from a peer with no SRTP context yet */
  uint64_t rtcp_auth_fail;        /* SRTCP authentication failed */
  uint64_t rtcp_replay;           /* SRTCP index already seen */
  uint64_t rtcp_malformed;        /* a header lied about its length, or bad padding */
  uint64_t rtcp_unknown_type;     /* parsed, but a type or FMT we do not act on */
  uint64_t tx_rtcp;
  uint64_t rr_sent;
  uint64_t sr_missing;
  uint64_t audio_level_updates;
  uint64_t dominant_speaker_changes;
  uint64_t sr_received;
  uint64_t sr_relayed;
  uint64_t sr_without_sdes;
  uint64_t rr_received;
  uint64_t rtcp_pli_received;
  uint64_t rtcp_pli_forwarded;
  uint64_t rtcp_pli_suppressed; /* inside the per-publisher throttle window */
  uint64_t rtcp_pli_dropped;    /* no subscription, inactive, self, or no track */
  uint64_t pli_on_join;         /* keyframe requests attempted when a subscription is bound */
  uint64_t pli_on_loss;         /* keyframe requests attempted after a NACK ring miss */
  uint64_t rtcp_fir_received;
  uint64_t rtcp_nack_received;
  uint64_t nack_requests; /* sequences named by NACKs, not packets */
  uint64_t nack_retransmits;       /* packets answered from a retention ring */
  uint64_t nack_misses;            /* the requested sequence was not retained */
  uint64_t nack_dropped;           /* unresolved, self, inactive subscription, or no ring */
  uint64_t nack_rate_limited;      /* refused by the subscription budget or the cooldown */
  uint64_t nack_retention_refused; /* a track got no ring: descriptor or pool bound */
  uint64_t nack_bytes_retransmitted;
  uint64_t rtcp_bye_received;
  uint64_t rx_other; /* datagrams no RFC 7983 class claims: the classes now sum to rx_datagrams */
  uint64_t rx_resolved; /* datagrams whose sender resolved to a peer record */
  uint64_t ice_requests;
  uint64_t ice_authenticated;
  uint64_t ice_fingerprint_failures;
  uint64_t ice_auth_failures;
  uint64_t ice_unknown_ufrag;
  uint64_t ice_ufrag_mismatch; /* USERNAME named another transport's ufrag */
  uint64_t ice_replay_dropped;
  uint64_t ice_nominated;
  uint64_t ice_rebind_probation;
  uint64_t ice_rebind_promoted;
  uint64_t ice_rebind_rejected_rate;
  uint64_t ice_rebind_rejected_conflict;
  uint64_t ice_rebind_expired;
  uint64_t ice_consent_expired;
  uint64_t dtls_handshakes_started;
  uint64_t dtls_handshakes_completed;
  uint64_t dtls_fingerprint_mismatches;
  uint64_t dtls_failures;
  uint64_t dtls_timeouts;
  uint64_t dtls_records_in;
  uint64_t dtls_records_out;
  int64_t  dtls_sessions;
  int64_t  srtp_sessions;
  uint64_t srtp_failures;
  uint64_t srtp_unprotect_ok;
  uint64_t srtp_unprotect_failures;
  uint64_t srtp_protect_ok;
  uint64_t srtp_protect_failures;
  uint64_t srtp_bad_packets;
  uint64_t media_echoed;
  uint64_t rooms_created;
  uint64_t rooms_released;
  uint64_t room_failures;
  uint64_t room_joins;
  uint64_t room_join_failures;
  uint64_t tracks_added;
  uint64_t track_failures;
  uint64_t peers_released;
  uint64_t subscriptions_added;
  uint64_t subscription_failures;
  uint64_t media_fanned_out;
  uint64_t fanout_skipped;
  uint64_t fanout_slab_drops;
  uint64_t peers_admitted;
  uint64_t peer_misses;
  uint64_t peers_dropped;
  uint64_t peers_expired;
} SfuCounters;

#endif /* UFSRVCORELIB_COUNTERS_TYPE_H */
