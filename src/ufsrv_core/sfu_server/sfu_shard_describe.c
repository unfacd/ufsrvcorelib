/**
 * @file sfu_shard_describe.c
 * @brief Render one shard's live state as a JSON document.
 *
 * Runs on the shard's own thread, from the SFU_COMMAND_DESCRIBE_SHARD command,
 * so every field is read without a lock: the shard thread is the only writer of
 * the state it reports.
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

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <stddef.h>
#include <unistd.h>

#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <uflib/adt/flow_table/flow_table.h>
#include <uflib/slab/inlined_slab.h>

#include "sfu_server_priv.h"

BufferDescriptor *
DescribeShard(SfuShard *shard, BufferDescriptor *reply)
{
  if (!shard || !reply || !reply->data) return reply;

  const SfuShardSpec *spec = shard->spec;
  const SfuCounters * counters = spec->counters;
  size_t              queue_depth = 0;
  for (size_t other = 0; other < spec->shard_count; other++) {
    if (spec->inbound[other]) {
      queue_depth = spec->inbound[other]->queue_sz;
      break;
    }
  }

  reply->size = 0;
  reply->data[0] = '\0';

  BufferDescriptorAppendFormatted(reply, "{\"event\":\"sfu_shard_state\"");
  BufferDescriptorAppendFormatted(reply, ",\"pid\":%ld", (long)getpid());
  BufferDescriptorAppendFormatted(reply, ",\"thread\":%lu", (unsigned long)pthread_self());
  BufferDescriptorAppendFormatted(reply, ",\"index\":%zu", spec->index);
  BufferDescriptorAppendFormatted(reply, ",\"shards\":%zu", spec->shard_count);
  BufferDescriptorAppendFormatted(reply, ",\"peer_shards\":%zu", spec->shard_count ? spec->shard_count - 1 : 0);
  BufferDescriptorAppendFormatted(reply, ",\"port\":%u", spec->port);
  BufferDescriptorAppendFormatted(reply, ",\"family\":%d", spec->family);
  BufferDescriptorAppendFormatted(reply, ",\"fd\":%d", spec->fd);
  BufferDescriptorAppendFormatted(reply, ",\"wake_fd\":%d", spec->wake_fd);
  BufferDescriptorAppendFormatted(reply, ",\"slab_packets\":%u", spec->slab_packets);
  BufferDescriptorAppendFormatted(reply, ",\"free_packets\":%zu", InlinedSlabAvailable(&shard->packets.slab));
  BufferDescriptorAppendFormatted(reply, ",\"slab_high_water\":%zu", InlinedSlabHighWater(&shard->packets.slab));
  BufferDescriptorAppendFormatted(reply, ",\"peer_capacity\":%u", spec->peer_capacity);
  BufferDescriptorAppendFormatted(reply, ",\"rtcp_interval_ms\":%u", shard->rtcp.interval_ms);
  BufferDescriptorAppendFormatted(reply, ",\"rtcp_pli_min_ms\":%u", shard->rtcp.pli_min_ms);
  BufferDescriptorAppendFormatted(reply, ",\"peer_count\":%zu", shard->peers.count);
  BufferDescriptorAppendFormatted(reply, ",\"flow_size\":%zu",
                                   FlowTableSize(shard->peers.v4) + FlowTableSize(shard->peers.v6));
  BufferDescriptorAppendFormatted(reply, ",\"flow_capacity\":%zu",
                                   FlowTableCapacity(shard->peers.v4) + FlowTableCapacity(shard->peers.v6));
  BufferDescriptorAppendFormatted(reply, ",\"rooms\":%zu", shard->rooms.count);
  BufferDescriptorAppendFormatted(reply, ",\"room_limit\":%zu", shard->rooms.limit);
  BufferDescriptorAppendFormatted(reply, ",\"audio_level_updates\":%llu,\"dominant_speaker_changes\":%llu",
                                  (unsigned long long)shard->spec->counters->audio_level_updates,
                                  (unsigned long long)shard->spec->counters->dominant_speaker_changes);
  BufferDescriptorAppendFormatted(reply, ",\"nack_retransmit_per_second\":%u", shard->nack.retransmit_per_second);
  BufferDescriptorAppendFormatted(reply, ",\"room_members\":%zu", shard->rooms.members);
  BufferDescriptorAppendFormatted(reply, ",\"ssrc_count\":%zu", shard->ssrc_count);
  BufferDescriptorAppendFormatted(reply, ",\"recv_batch\":%u", spec->recv_batch);
  BufferDescriptorAppendFormatted(reply, ",\"send_batch\":%u", spec->send_batch);
  BufferDescriptorAppendFormatted(reply, ",\"command_capacity\":%u", shard->commands.capacity);
  BufferDescriptorAppendFormatted(reply, ",\"queue_depth\":%zu", queue_depth);
  BufferDescriptorAppendFormatted(reply, ",\"poll_ms\":%u", spec->poll_ms);
  BufferDescriptorAppendFormatted(reply, ",\"peers_idle_timeout_seconds\":%u", spec->peers_idle_timeout_seconds);
  BufferDescriptorAppendFormatted(reply, ",\"ice_consent_timeout_seconds\":%u", spec->ice_consent_timeout_seconds);
  BufferDescriptorAppendFormatted(reply, ",\"ufrag_count\":%zu", shard->ice.count);
  BufferDescriptorAppendFormatted(reply, ",\"commands_applied\":%llu",
                                   (unsigned long long)counters->commands_applied);
  BufferDescriptorAppendFormatted(reply, ",\"commands_dropped\":%llu",
                                   (unsigned long long)counters->commands_dropped);
  BufferDescriptorAppendFormatted(reply, ",\"rx_datagrams\":%llu", (unsigned long long)counters->rx_datagrams);
  BufferDescriptorAppendFormatted(reply, ",\"rx_batches\":%llu", (unsigned long long)counters->rx_batches);
  BufferDescriptorAppendFormatted(reply, ",\"rx_stun\":%llu", (unsigned long long)counters->rx_stun);
  BufferDescriptorAppendFormatted(reply, ",\"rx_dtls\":%llu", (unsigned long long)counters->rx_dtls);
  BufferDescriptorAppendFormatted(reply, ",\"rx_rtp\":%llu", (unsigned long long)counters->rx_rtp);
  BufferDescriptorAppendFormatted(reply, ",\"rx_rtcp\":%llu", (unsigned long long)counters->rx_rtcp);
  BufferDescriptorAppendFormatted(reply, ",\"rtcp_packets_parsed\":%llu",
                                   (unsigned long long)counters->rtcp_packets_parsed);
  BufferDescriptorAppendFormatted(reply, ",\"rtcp_before_srtp\":%llu",
                                   (unsigned long long)counters->rtcp_before_srtp);
  BufferDescriptorAppendFormatted(reply, ",\"rtcp_auth_fail\":%llu",
                                   (unsigned long long)counters->rtcp_auth_fail);
  BufferDescriptorAppendFormatted(reply, ",\"rtcp_replay\":%llu",
                                   (unsigned long long)counters->rtcp_replay);
  BufferDescriptorAppendFormatted(reply, ",\"rtcp_malformed\":%llu",
                                   (unsigned long long)counters->rtcp_malformed);
  BufferDescriptorAppendFormatted(reply, ",\"rtcp_unknown_type\":%llu",
                                   (unsigned long long)counters->rtcp_unknown_type);
  BufferDescriptorAppendFormatted(reply, ",\"tx_rtcp\":%llu", (unsigned long long)counters->tx_rtcp);
  BufferDescriptorAppendFormatted(reply, ",\"rr_sent\":%llu", (unsigned long long)counters->rr_sent);
  BufferDescriptorAppendFormatted(reply, ",\"sr_missing\":%llu", (unsigned long long)counters->sr_missing);
  BufferDescriptorAppendFormatted(reply, ",\"sr_received\":%llu", (unsigned long long)counters->sr_received);
  BufferDescriptorAppendFormatted(reply, ",\"sr_relayed\":%llu", (unsigned long long)counters->sr_relayed);
  BufferDescriptorAppendFormatted(reply, ",\"sr_without_sdes\":%llu",
                                   (unsigned long long)counters->sr_without_sdes);
  BufferDescriptorAppendFormatted(reply, ",\"rr_received\":%llu", (unsigned long long)counters->rr_received);
  BufferDescriptorAppendFormatted(reply, ",\"pli_received\":%llu",
                                   (unsigned long long)counters->rtcp_pli_received);
  BufferDescriptorAppendFormatted(reply, ",\"pli_forwarded\":%llu",
                                   (unsigned long long)counters->rtcp_pli_forwarded);
  BufferDescriptorAppendFormatted(reply, ",\"pli_suppressed\":%llu",
                                   (unsigned long long)counters->rtcp_pli_suppressed);
  BufferDescriptorAppendFormatted(reply, ",\"pli_dropped\":%llu",
                                   (unsigned long long)counters->rtcp_pli_dropped);
  BufferDescriptorAppendFormatted(reply, ",\"pli_on_join\":%llu",
                                   (unsigned long long)counters->pli_on_join);
  BufferDescriptorAppendFormatted(reply, ",\"pli_on_loss\":%llu",
                                   (unsigned long long)counters->pli_on_loss);
  BufferDescriptorAppendFormatted(reply, ",\"fir_received\":%llu",
                                   (unsigned long long)counters->rtcp_fir_received);
  BufferDescriptorAppendFormatted(reply, ",\"nack_received\":%llu",
                                   (unsigned long long)counters->rtcp_nack_received);
  BufferDescriptorAppendFormatted(reply, ",\"nack_requests\":%llu",
                                   (unsigned long long)counters->nack_requests);
  BufferDescriptorAppendFormatted(reply, ",\"nack_retransmits\":%llu",
                                   (unsigned long long)counters->nack_retransmits);
  BufferDescriptorAppendFormatted(reply, ",\"nack_misses\":%llu", (unsigned long long)counters->nack_misses);
  BufferDescriptorAppendFormatted(reply, ",\"nack_dropped\":%llu", (unsigned long long)counters->nack_dropped);
  BufferDescriptorAppendFormatted(reply, ",\"nack_rate_limited\":%llu",
                                   (unsigned long long)counters->nack_rate_limited);
  BufferDescriptorAppendFormatted(reply, ",\"nack_retention_refused\":%llu",
                                   (unsigned long long)counters->nack_retention_refused);
  BufferDescriptorAppendFormatted(reply, ",\"nack_bytes_retransmitted\":%llu",
                                   (unsigned long long)counters->nack_bytes_retransmitted);
  BufferDescriptorAppendFormatted(reply, ",\"bye_received\":%llu",
                                   (unsigned long long)counters->rtcp_bye_received);
  BufferDescriptorAppendFormatted(reply, ",\"rx_other\":%llu", (unsigned long long)counters->rx_other);
  BufferDescriptorAppendFormatted(reply, ",\"rx_resolved\":%llu", (unsigned long long)counters->rx_resolved);
  BufferDescriptorAppendFormatted(reply, ",\"tx_datagrams\":%llu", (unsigned long long)counters->tx_datagrams);
  BufferDescriptorAppendFormatted(reply, ",\"drops_slab\":%llu", (unsigned long long)counters->drops_slab);
  BufferDescriptorAppendFormatted(reply, ",\"drops_queue\":%llu", (unsigned long long)counters->drops_queue);
  BufferDescriptorAppendFormatted(reply, ",\"drops_truncated\":%llu",
                                   (unsigned long long)counters->drops_truncated);
  BufferDescriptorAppendFormatted(reply, ",\"drops_send\":%llu", (unsigned long long)counters->drops_send);
  BufferDescriptorAppendFormatted(reply, ",\"drops_no_peer\":%llu", (unsigned long long)counters->drops_no_peer);
  BufferDescriptorAppendFormatted(reply, ",\"peers_admitted\":%llu", (unsigned long long)counters->peers_admitted);
  BufferDescriptorAppendFormatted(reply, ",\"peer_misses\":%llu", (unsigned long long)counters->peer_misses);
  BufferDescriptorAppendFormatted(reply, ",\"peers_dropped\":%llu", (unsigned long long)counters->peers_dropped);
  BufferDescriptorAppendFormatted(reply, ",\"peers_expired\":%llu", (unsigned long long)counters->peers_expired);
  BufferDescriptorAppendFormatted(reply, ",\"ice_requests\":%llu", (unsigned long long)counters->ice_requests);
  BufferDescriptorAppendFormatted(reply, ",\"ice_authenticated\":%llu",
                                   (unsigned long long)counters->ice_authenticated);
  BufferDescriptorAppendFormatted(reply, ",\"ice_fingerprint_failures\":%llu",
                                   (unsigned long long)counters->ice_fingerprint_failures);
  BufferDescriptorAppendFormatted(reply, ",\"ice_auth_failures\":%llu",
                                   (unsigned long long)counters->ice_auth_failures);
  BufferDescriptorAppendFormatted(reply, ",\"ice_unknown_ufrag\":%llu",
                                   (unsigned long long)counters->ice_unknown_ufrag);
  BufferDescriptorAppendFormatted(reply, ",\"ice_ufrag_mismatch\":%llu",
                                   (unsigned long long)counters->ice_ufrag_mismatch);
  BufferDescriptorAppendFormatted(reply, ",\"ice_replay_dropped\":%llu",
                                   (unsigned long long)counters->ice_replay_dropped);
  BufferDescriptorAppendFormatted(reply, ",\"ice_nominated\":%llu", (unsigned long long)counters->ice_nominated);
  BufferDescriptorAppendFormatted(reply, ",\"ice_rebind_probation\":%llu",
                                   (unsigned long long)counters->ice_rebind_probation);
  BufferDescriptorAppendFormatted(reply, ",\"ice_rebind_promoted\":%llu",
                                   (unsigned long long)counters->ice_rebind_promoted);
  BufferDescriptorAppendFormatted(reply, ",\"ice_rebind_rejected_rate\":%llu",
                                   (unsigned long long)counters->ice_rebind_rejected_rate);
  BufferDescriptorAppendFormatted(reply, ",\"ice_rebind_rejected_conflict\":%llu",
                                   (unsigned long long)counters->ice_rebind_rejected_conflict);
  BufferDescriptorAppendFormatted(reply, ",\"ice_rebind_expired\":%llu",
                                   (unsigned long long)counters->ice_rebind_expired);
  BufferDescriptorAppendFormatted(reply, ",\"ice_consent_expired\":%llu",
                                   (unsigned long long)counters->ice_consent_expired);
  BufferDescriptorAppendFormatted(reply, ",\"dtls_handshakes_started\":%llu",
                                   (unsigned long long)counters->dtls_handshakes_started);
  BufferDescriptorAppendFormatted(reply, ",\"dtls_handshakes_completed\":%llu",
                                   (unsigned long long)counters->dtls_handshakes_completed);
  BufferDescriptorAppendFormatted(reply, ",\"dtls_fingerprint_mismatches\":%llu",
                                   (unsigned long long)counters->dtls_fingerprint_mismatches);
  BufferDescriptorAppendFormatted(reply, ",\"dtls_failures\":%llu", (unsigned long long)counters->dtls_failures);
  BufferDescriptorAppendFormatted(reply, ",\"dtls_timeouts\":%llu", (unsigned long long)counters->dtls_timeouts);
  BufferDescriptorAppendFormatted(reply, ",\"dtls_records_in\":%llu", (unsigned long long)counters->dtls_records_in);
  BufferDescriptorAppendFormatted(reply, ",\"dtls_records_out\":%llu", (unsigned long long)counters->dtls_records_out);
  BufferDescriptorAppendFormatted(reply, ",\"dtls_sessions\":%lld", (long long)counters->dtls_sessions);
  BufferDescriptorAppendFormatted(reply, ",\"srtp_sessions\":%lld", (long long)counters->srtp_sessions);
  BufferDescriptorAppendFormatted(reply, ",\"srtp_failures\":%llu", (unsigned long long)counters->srtp_failures);
  BufferDescriptorAppendFormatted(reply, ",\"srtp_unprotect_ok\":%llu",
                                   (unsigned long long)counters->srtp_unprotect_ok);
  BufferDescriptorAppendFormatted(reply, ",\"srtp_unprotect_failures\":%llu",
                                   (unsigned long long)counters->srtp_unprotect_failures);
  BufferDescriptorAppendFormatted(reply, ",\"srtp_protect_ok\":%llu",
                                   (unsigned long long)counters->srtp_protect_ok);
  BufferDescriptorAppendFormatted(reply, ",\"srtp_protect_failures\":%llu",
                                   (unsigned long long)counters->srtp_protect_failures);
  BufferDescriptorAppendFormatted(reply, ",\"srtp_bad_packets\":%llu",
                                   (unsigned long long)counters->srtp_bad_packets);
  BufferDescriptorAppendFormatted(reply, ",\"media_echoed\":%llu", (unsigned long long)counters->media_echoed);
  BufferDescriptorAppendFormatted(reply, ",\"rooms_created\":%llu", (unsigned long long)counters->rooms_created);
  BufferDescriptorAppendFormatted(reply, ",\"rooms_released\":%llu",
                                   (unsigned long long)counters->rooms_released);
  BufferDescriptorAppendFormatted(reply, ",\"room_failures\":%llu", (unsigned long long)counters->room_failures);
  BufferDescriptorAppendFormatted(reply, ",\"room_joins\":%llu", (unsigned long long)counters->room_joins);
  BufferDescriptorAppendFormatted(reply, ",\"room_join_failures\":%llu",
                                   (unsigned long long)counters->room_join_failures);
  BufferDescriptorAppendFormatted(reply, ",\"tracks_added\":%llu", (unsigned long long)counters->tracks_added);
  BufferDescriptorAppendFormatted(reply, ",\"track_failures\":%llu",
                                   (unsigned long long)counters->track_failures);
  BufferDescriptorAppendFormatted(reply, ",\"peers_released\":%llu",
                                   (unsigned long long)counters->peers_released);
  BufferDescriptorAppendFormatted(reply, ",\"subscriptions_added\":%llu",
                                   (unsigned long long)counters->subscriptions_added);
  BufferDescriptorAppendFormatted(reply, ",\"subscription_failures\":%llu",
                                   (unsigned long long)counters->subscription_failures);
  BufferDescriptorAppendFormatted(reply, ",\"media_fanned_out\":%llu",
                                   (unsigned long long)counters->media_fanned_out);
  BufferDescriptorAppendFormatted(reply, ",\"fanout_skipped\":%llu",
                                   (unsigned long long)counters->fanout_skipped);
  BufferDescriptorAppendFormatted(reply, ",\"fanout_slab_drops\":%llu",
                                   (unsigned long long)counters->fanout_slab_drops);
  BufferDescriptorAppendFormatted(reply, "}");
  return reply;
}
