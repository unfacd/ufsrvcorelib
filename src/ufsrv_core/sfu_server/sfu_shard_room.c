/**
 * @file sfu_shard_room.c
 * @brief Room membership and the SSRC index, owned by one shard thread.
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

#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "sfu_server_priv.h"
#include "rtcp/sfu_shard_rtcp.h"
#include "sfu_shard_ice.h"
#include "sfu_shard_subscription.h"
#include "sfu_shard_nack.h"
#include "sfu_shard_room.h"
#include "events/sfu_shard_events.h"
#include "srtp/sfu_shard_srtp.h"

/* --- SSRC index: open addressing, backward-shift delete ------------------- */

static uint32_t
sSsrcHash(uint32_t ssrc)
{
  ssrc ^= ssrc >> 16;
  ssrc *= 0x7feb352du;
  ssrc ^= ssrc >> 15;
  ssrc *= 0x846ca68bu;
  ssrc ^= ssrc >> 16;
  return ssrc;
}

bool
SfuShardSsrcInsert(SfuShard *shard, uint32_t ssrc, uint32_t slot)
{
  if (!ssrc) return false; /* zero is the empty marker */
  const size_t mask = shard->ssrc_mask;
  size_t       index = sSsrcHash(ssrc) & mask;
  for (size_t probe = 0; probe <= mask; probe++) {
    SfuSsrcEntry *entry = &shard->ssrc_index[index];
    if (entry->slot_plus_one == 0) {
      if ((shard->ssrc_count + 1u) * 4u > (mask + 1u) * 3u) return false;
      entry->ssrc = ssrc;
      entry->slot_plus_one = slot + 1u;
      shard->ssrc_count++;
      return true;
    }
    if (entry->ssrc == ssrc) {
      return entry->slot_plus_one == slot + 1u;
    }
    index = (index + 1) & mask;
  }
  return false;
}

bool
SfuShardSsrcLookup(const SfuShard *shard, uint32_t ssrc, uint32_t *slot)
{
  if (!ssrc) return false;

  const size_t mask = shard->ssrc_mask;
  size_t       index = sSsrcHash(ssrc) & mask;
  for (size_t probe = 0; probe <= mask; probe++) {
    const SfuSsrcEntry *entry = &shard->ssrc_index[index];
    if (entry->slot_plus_one == 0) return false;
    if (entry->ssrc == ssrc) {
      *slot = entry->slot_plus_one - 1u;
      return true;
    }
    index = (index + 1) & mask;
  }
  return false;
}

void
SfuShardSsrcRemovePeer(SfuShard *shard, uint32_t slot)
{
  const SfuPeer *peer = &shard->peers.slots[slot];

  for (size_t track = 0; track < peer->track_count; track++) {
    const uint32_t ssrc = peer->tracks[track].ssrc;
    if (!ssrc) continue;

    const size_t mask = shard->ssrc_mask;
    size_t       index = sSsrcHash(ssrc) & mask;
    for (size_t probe = 0; probe <= mask; probe++) {
      SfuSsrcEntry *entry = &shard->ssrc_index[index];
      if (entry->slot_plus_one == 0) break;
      if (entry->ssrc == ssrc && entry->slot_plus_one == slot + 1u) {
        for (size_t next = (index + 1) & mask; shard->ssrc_index[next].slot_plus_one; next = (next + 1) & mask) {
          SfuSsrcEntry *following = &shard->ssrc_index[next];
          const size_t home = sSsrcHash(following->ssrc) & mask;
          if (((index - home) & mask) >= ((next - home) & mask)) continue;
          *entry = *following;
          entry = following;
          index = next;
        }
        entry->ssrc = 0;
        entry->slot_plus_one = 0;
        shard->ssrc_count--;
        break;
      }
      index = (index + 1) & mask;
    }
  }
}

/* --- rooms: the room-id map, control plane only ---------------------------- */

/**
 * @brief Resolve a room id on this shard.
 *
 * @param shard    Shard that owns the room index.
 * @param room_id  Identifier the signalling server assigned; zero never resolves.
 * @return The room, or NULL when no live room has that id.
 */
static SfuRoom *
sRoomById(const SfuShard *shard, uint64_t room_id)
{
  return (SfuRoom *)Id64MapGet(shard->rooms.index, room_id);
}

void
SfuShardDescribeRoom(const SfuShard *shard, uint64_t room_id, BufferDescriptor *reply)
{
  const SfuRoom *room = sRoomById(shard, room_id);
  if (!room) {
    BufferDescriptorAppendFormatted(reply, "{\"error\":\"room not found\"}");
    return;
  }
  BufferDescriptorAppendFormatted(reply, "{\"room_id\":%llu,\"dominant_speaker\":", (unsigned long long)room_id);
  if (room->speaker_generation) {
    BufferDescriptorAppendFormatted(reply, "{\"publisher_ssrc\":%u}}", room->speaker_ssrc);
  } else {
    BufferDescriptorAppendFormatted(reply, "null}");
  }
}

static void
sElectSpeaker(SfuShard *shard, SfuRoom *room, uint64_t now_ms)
{
  const uint64_t now_ns = now_ms * 1000000ull;
  const SfuPublisherTrack *best = NULL;
  const SfuPublisherTrack *incumbent = NULL;
  const SfuPeer *best_peer = NULL;
  const SfuPeer *incumbent_peer = NULL;
  for (size_t member = 0; member < room->member_count; ++member) {
    const SfuPeer *peer = room->members[member];
    if (peer->state != SFU_PEER_STATE_CONNECTED) continue;
    for (size_t i = 0; i < peer->track_count; ++i) {
      const SfuPublisherTrack *track = &peer->tracks[i];
      const uint64_t last_voice = track->audio_level.last_voice_ns;
      if (!last_voice || (now_ns >= last_voice && now_ns - last_voice >= 1000000000ull)) continue;
      if (track->audio_level.generation == room->speaker_generation) {
        incumbent = track;
        incumbent_peer = peer;
      }
      if (!best || track->audio_level.average < best->audio_level.average ||
          (track->audio_level.average == best->audio_level.average && track->ssrc < best->ssrc)) {
        best = track;
        best_peer = peer;
      }
    }
  }

  const SfuPublisherTrack *winner = incumbent;
  if (!incumbent) {
    winner = best;
    room->challenger_generation = 0;
  } else if (best == incumbent || best->audio_level.average + 6.0 > incumbent->audio_level.average) {
    room->challenger_generation = 0;
  } else if (room->challenger_generation != best->audio_level.generation) {
    room->challenger_generation = best->audio_level.generation;
    room->challenger_since_ms = now_ms;
  } else if (now_ms - room->challenger_since_ms >= 300) {
    winner = best;
    room->challenger_generation = 0;
  }

  const uint64_t generation = winner ? winner->audio_level.generation : 0;
  if (generation != room->speaker_generation) {
    room->speaker_generation = generation;
    room->speaker_ssrc = winner ? winner->ssrc : 0;
    shard->spec->counters->dominant_speaker_changes++;
    const SfuPeer *speaker = winner ? (winner == best ? best_peer : incumbent_peer) : NULL;
    const uint8_t level = winner ? (uint8_t)(winner->audio_level.average + 0.5) : 0;
    SfuShardEventSpeaker(shard, room, speaker, level);
  }
}

void
SfuShardSpeakerSweep(SfuShard *shard, uint64_t now_ms)
{
  if (!shard->rooms.members || now_ms < shard->next_speaker_ms) return;
  shard->next_speaker_ms = now_ms + SFU_SPEAKER_SWEEP_MS;
  /* A room appears once, at its first member, regardless of slot order. */
  for (size_t i = 0; i < shard->peers.capacity; ++i) {
    SfuPeer *peer = &shard->peers.slots[i];
    SfuRoom *room = peer->room;
    if (room && room->members[0] == peer) sElectSpeaker(shard, room, now_ms);
  }
}

/**
 * @brief Allocate one room: the header and its member array in a single block.
 *
 * A live room costs no more than its members need and an absent room costs
 * nothing. The array holds every peer slot in the shard, which is the largest
 * membership a single shard can host.
 *
 * @param shard   Shard providing the member capacity.
 * @param room_id Identifier to store on the record.
 * @return The room, or NULL when the allocation fails.
 */
static SfuRoom *
sRoomAllocate(const SfuShard *shard, uint64_t room_id)
{
  const size_t capacity = shard->spec->peer_capacity ? shard->spec->peer_capacity : 1u;
  SfuRoom *    room = calloc(1, sizeof(*room) + capacity * sizeof(*room->members));
  if (!room) return NULL;
  room->room_id = room_id;
  room->members = (struct SfuPeer **)(room + 1);
  room->member_capacity = capacity;
  return room;
}

/**
 * @brief Drop a room from the index and free its header and member array.
 *
 * @param shard Shard that owns the room index.
 * @param room  Room whose last member has left; its id is removed from the map.
 */
static void
sRoomRelease(SfuShard *shard, SfuRoom *room)
{
  (void)Id64MapRemove(shard->rooms.index, room->room_id);
  if (room->speaker_generation) {
    shard->spec->counters->dominant_speaker_changes++;
    SfuShardEventSpeaker(shard, room, NULL, 0);
  }
  free(room);
  shard->rooms.count--;
}

void
SfuShardCreateRoom(SfuShard *shard, uint64_t room_id)
{
  if (!room_id || sRoomById(shard, room_id)) {
    shard->spec->counters->room_failures++;
    return;
  }
  SfuRoom *room = sRoomAllocate(shard, room_id);
  if (!room) {
    shard->spec->counters->room_failures++;
    return;
  }
  if (Id64MapPut(shard->rooms.index, room_id, room) != ID64_INSERTED) {
    free(room); /* bounded map at rooms.max, or an allocation failure */
    shard->spec->counters->room_failures++;
    return;
  }
  shard->rooms.count++;
  shard->spec->counters->rooms_created++;
}

static void
sJoinRoom(SfuShard *shard, SfuRoom *room, SfuPeer *peer)
{
  room->members[room->member_count++] = peer;
  peer->room = room;
  shard->rooms.members++;
  shard->spec->counters->room_joins++;
  SfuShardEventPeerConnected(shard, peer);
}

void
SfuShardJoinRoom(SfuShard *shard, uint64_t room_id, const char *ufrag, size_t ufrag_length)
{
  SfuRoom *room = sRoomById(shard, room_id);
  SfuPeer *peer = SfuShardPeerByUfrag(shard, ufrag, ufrag_length);
  if (!room || !peer || peer->room || peer->config) {
    shard->spec->counters->room_join_failures++;
    return;
  }
  for (size_t index = 0; index < room->member_count; index++) {
    if (room->members[index] == peer) return; /* already a member */
  }
  if (room->member_count >= room->member_capacity) {
    shard->spec->counters->room_join_failures++;
    return;
  }
  sJoinRoom(shard, room, peer);
}

void
SfuShardAddTrack(SfuShard *shard, uint64_t room_id, const char *ufrag, size_t ufrag_length, uint32_t ssrc,
                 SfuTrackKind kind, uint32_t clock_rate)
{
  SfuRoom *room = sRoomById(shard, room_id);
  SfuPeer *peer = SfuShardPeerByUfrag(shard, ufrag, ufrag_length);
  if (!room || !peer || peer->room != room || peer->config || !ssrc ||
      SfuShardOutboundLookup(shard, ssrc, NULL, NULL)) {
    shard->spec->counters->track_failures++;
    return;
  }

  const uint32_t slot = (uint32_t)(peer - shard->peers.slots);
  SfuPublisherTrack *track = SfuPeerTrackFor(peer, ssrc);
  if ((!track && peer->track_count >= SFU_PEER_TRACK_MAX) || !SfuShardSsrcInsert(shard, ssrc, slot)) {
    shard->spec->counters->track_failures++;
    return;
  }
  track = SfuPeerTrackAppend(peer, ssrc, kind);
  track->clock_rate = clock_rate ? clock_rate : SfuTrackClockRate(kind);
  SfuShardNackAttach(shard, track);
  shard->spec->counters->tracks_added++;
}

void
SfuShardSubscribe(SfuShard *shard, uint64_t room_id, const char *subscriber_ufrag, size_t ufrag_length,
                  uint32_t publisher_ssrc, uint32_t out_ssrc, uint16_t seq_offset)
{
  SfuRoom *      room = sRoomById(shard, room_id);
  SfuPeer *      subscriber = SfuShardPeerByUfrag(shard, subscriber_ufrag, ufrag_length);
  uint32_t       publisher_slot = 0;
  SfuPeer *      publisher = NULL;
  if (SfuShardSsrcLookup(shard, publisher_ssrc, &publisher_slot)) publisher = &shard->peers.slots[publisher_slot];

  if (!room || !subscriber || !publisher || subscriber->config || subscriber->room != room || publisher->room != room ||
      publisher_ssrc == out_ssrc) {
    shard->spec->counters->subscription_failures++;
    return;
  }
  /* Zero asks the shard to assign one: the allocator skips SSRCs already live
   * here or published on this shard, so a binding is never ambiguous. */
  if (!out_ssrc) out_ssrc = SfuShardOutboundAllocate(shard);
  uint32_t published_slot;
  if (!out_ssrc || SfuShardSsrcLookup(shard, out_ssrc, &published_slot)) {
    shard->spec->counters->subscription_failures++;
    return;
  }

  const uint32_t slot = (uint32_t)(subscriber - shard->peers.slots);
  SfuSubscription *existing = SfuShardSubscriptionFor(subscriber, publisher_ssrc);
  for (size_t index = 0; index < SFU_PEER_SUBS_MAX; index++) {
    SfuSubscription *subscription = &subscriber->subscriptions[index];
    if (existing ? subscription != existing : subscription->active) continue;
    const bool is_new = !subscription->active;
    const bool rebind = !is_new && subscription->out_ssrc != out_ssrc;
    /* Bind the new outbound SSRC first: if the index refuses, the old binding
     * stays intact rather than leaving the subscriber unreachable. */
    if (!SfuShardOutboundInsert(shard, out_ssrc, slot, (uint8_t)index)) {
      shard->spec->counters->subscription_failures++;
      return;
    }
    if (rebind) {
      (void)SfuShardOutboundRemove(shard, subscription->out_ssrc);
      subscription->sent_packets = 0;
      subscription->sent_octets = 0;
    }
    subscription->publisher_ssrc = publisher_ssrc;
    subscription->out_ssrc = out_ssrc;
    subscription->seq_offset = seq_offset;
    subscription->ts_offset = 0; /* no timestamp rewriting yet; the SR relay reads it */
    subscription->active = true;
    if (is_new && subscriber->subscription_count < SFU_PEER_SUBS_MAX) subscriber->subscription_count++;
    shard->spec->counters->subscriptions_added++;
    /* A new binding starts mid-GOP: one keyframe request, subject to the
     * per-publisher throttle, so the newcomer has something decodable. */
    if (is_new || rebind) SfuShardPliOnJoin(shard, publisher, publisher_ssrc);
    return;
  }
  shard->spec->counters->subscription_failures++;
}

SfuPublisherTrack *
SfuShardBindPublication(SfuShard *shard, SfuPeer *publisher, size_t media_index, uint32_t ssrc)
{
  SfuPeerConfigSection *section = &publisher->config->media[media_index];
  uint32_t owner;
  if (!ssrc || section->bound_ssrc || publisher->track_count >= SFU_PEER_TRACK_MAX ||
      SfuShardSsrcLookup(shard, ssrc, &owner) || SfuShardOutboundLookup(shard, ssrc, NULL, NULL)) return NULL;

  const uint32_t slot = (uint32_t)(publisher - shard->peers.slots);
  if (!SfuShardSsrcInsert(shard, ssrc, slot)) return NULL;

  SfuPublisherTrack *track = SfuPeerTrackAppend(publisher, ssrc, section->kind);
  track->clock_rate = section->clock_rate;
  track->audio_level.extension_id = section->audio_level_extension_id;
  track->audio_level.allow_mixed = section->audio_level_allow_mixed;
  section->bound_ssrc = ssrc;
  SfuRoom *room = publisher->room;
  for (size_t i = 0; i < room->member_count; ++i) {
    SfuPeer *subscriber = room->members[i];
    for (size_t j = 0; j < SFU_PEER_SUBS_MAX; ++j) {
      SfuSubscription *sub = &subscriber->subscriptions[j];
      if (sub->active && sub->publisher == publisher && sub->publisher_media_index == media_index) {
        sub->publisher_ssrc = ssrc;
      }
    }
  }
  shard->spec->counters->tracks_added++;
  return track;
}

SfuPublisherTrack *
SfuShardPublishedTrack(const SfuShard *shard, SfuPeer *publisher, uint32_t ssrc)
{
  uint32_t slot = 0;
  if (!SfuShardSsrcLookup(shard, ssrc, &slot) || &shard->peers.slots[slot] != publisher) return NULL;
  return SfuPeerTrackFor(publisher, ssrc);
}

SfuSubscription *
SfuShardSubscriptionFor(SfuPeer *subscriber, uint32_t publisher_ssrc)
{
  if (!subscriber || !publisher_ssrc) return NULL;
  for (size_t index = 0; index < SFU_PEER_SUBS_MAX; index++) {
    SfuSubscription *subscription = &subscriber->subscriptions[index];
    if (subscription->active && subscription->publisher_ssrc == publisher_ssrc) return subscription;
  }
  return NULL;
}

static void
sRemoveSubscription(SfuShard *shard, SfuPeer *subscriber, SfuSubscription *subscription)
{
  (void)SfuShardOutboundRemove(shard, subscription->out_ssrc);
  free(subscription->sent_mapping);
  *subscription = (SfuSubscription){0};
  subscriber->subscription_count--;
}

void
SfuShardRoomDetachPeer(SfuShard *shard, SfuPeer *peer)
{
  SfuRoom *room = peer->room;
  if (!room) return;

  for (size_t index = 0; index < room->member_count; index++) {
    if (room->members[index] != peer) continue;
    room->members[index] = room->members[room->member_count - 1];
    room->member_count--;
    shard->rooms.members--;
    break;
  }
  /* Release subscriptions before the publisher's SSRCs can be reassigned. */
  for (size_t member_index = 0; member_index < room->member_count; member_index++) {
    SfuPeer *subscriber = room->members[member_index];

    for (size_t index = 0; index < SFU_PEER_SUBS_MAX; index++) {
      SfuSubscription *subscription = &subscriber->subscriptions[index];
      if (!subscription->active) continue;
      if (subscription->publisher ? subscription->publisher != peer
                                  : !SfuShardPublishedTrack(shard, peer, subscription->publisher_ssrc)) continue;

      sRemoveSubscription(shard, subscriber, subscription);
    }
  }

  peer->room = NULL;
  for (size_t i = 0; i < SFU_PEER_SUBS_MAX; ++i) {
    if (peer->subscriptions[i].active) sRemoveSubscription(shard, peer, &peer->subscriptions[i]);
  }

  if (room->member_count == 0) {
    sRoomRelease(shard, room);
    shard->spec->counters->rooms_released++;
  }
}

/* The wire ICE index holds the initial remote ufrag. Only control commands
 * resolve the signalling key, which stays stable across ICE restarts. */
SfuPeer *
SfuShardPeerByKey(SfuShard *shard, const char *key, size_t length)
{
  SfuPeer *indexed = SfuShardPeerByUfrag(shard, key, length);
  if (indexed && !indexed->config) return indexed;
  for (size_t i = 0; i < shard->spec->peer_capacity; ++i) {
    SfuPeer *peer = &shard->peers.slots[i];
    if (peer->config && strlen(peer->config->ufrag) == length &&
        memcmp(peer->config->ufrag, key, length) == 0) return peer;
  }
  return NULL;
}

static bool
sSameConfig(const SfuPeerConfigSnapshot *left, const SfuPeerConfigSnapshot *right)
{
  if (left->room_id != right->room_id || left->media_count != right->media_count ||
      strcmp(left->remote_ufrag, right->remote_ufrag) != 0 ||
      strcmp(left->remote_password, right->remote_password) != 0 ||
      memcmp(left->fingerprint, right->fingerprint, sizeof(left->fingerprint)) != 0) return false;
  for (size_t i = 0; i < left->media_count; ++i) {
    const SfuPeerConfigSection *a = &left->media[i];
    const SfuPeerConfigSection *b = &right->media[i];
    if (strcmp(a->mid, b->mid) != 0 || a->direction != b->direction || a->kind != b->kind ||
        a->clock_rate != b->clock_rate || a->payload_type != b->payload_type ||
        a->mid_extension_id != b->mid_extension_id || a->audio_level_extension_id != b->audio_level_extension_id ||
        a->audio_level_allow_mixed != b->audio_level_allow_mixed || a->publisher_ssrc != b->publisher_ssrc ||
        a->outbound_ssrc != b->outbound_ssrc || strcmp(a->publisher_ufrag, b->publisher_ufrag) != 0 ||
        strcmp(a->publisher_mid, b->publisher_mid) != 0) return false;
  }
  return true;
}

static bool
sMatchesTransport(const SfuPeer *peer, const SfuPeerConfigSnapshot *config)
{
  const SfuIceGeneration *generation = &peer->ice_current;
  if (peer->ice_pending.local_ufrag[0]) generation = &peer->ice_pending;
  const char *ufrag = generation->local_ufrag[0] ? generation->remote_ufrag : peer->ufrag;
  const char *password = generation->local_ufrag[0] ? generation->remote_password : peer->local_pwd;
  return strcmp(ufrag, config->remote_ufrag) == 0 && strcmp(password, config->remote_password) == 0;
}

static bool
sResolveSubscription(SfuShard *shard, SfuRoom *room, SfuPeer *peer,
                     const SfuPeerConfigSection *section, SfuSubscription *binding)
{
  SfuPeer *publisher = SfuShardPeerByKey(shard, section->publisher_ufrag, strlen(section->publisher_ufrag));
  if (!publisher || publisher == peer || publisher->room != room || !publisher->config) return false;

  for (size_t i = 0; i < publisher->config->media_count; ++i) {
    const SfuPeerConfigSection *source = &publisher->config->media[i];
    if (strcmp(source->mid, section->publisher_mid) != 0) continue;
    if (source->direction != SFU_PEER_CONFIG_PUBLISH || source->kind != section->kind ||
        source->clock_rate != section->clock_rate) return false;
    *binding = (SfuSubscription){.publisher = publisher, .publisher_media_index = i,
                                 .publisher_ssrc = source->bound_ssrc, .out_ssrc = section->outbound_ssrc,
                                 .active = true};
    return true;
  }
  return false;
}

typedef struct {
  SfuPublisherTrack tracks[SFU_PEER_TRACK_MAX];
  SfuSubscription subscriptions[SFU_PEER_SUBS_MAX];
  bool new_sent_mapping[SFU_PEER_SUBS_MAX];
  size_t track_count;
  size_t subscription_count;
} SfuPeerConfigPlan;

static bool
sPlanPublication(SfuShard *shard, SfuPeer *peer, const SfuPeerConfigSection *previous,
                 SfuPeerConfigSection *section, SfuPeerConfigPlan *plan)
{
  if (previous && previous->direction == SFU_PEER_CONFIG_PUBLISH) {
    if (!section->bound_ssrc) section->bound_ssrc = previous->bound_ssrc;
  }
  const uint32_t ssrc = section->bound_ssrc;
  if (!ssrc) return true;

  uint32_t owner;
  if (SfuShardSsrcLookup(shard, ssrc, &owner) && &shard->peers.slots[owner] != peer) return false;
  if (SfuShardOutboundLookup(shard, ssrc, &owner, NULL) && &shard->peers.slots[owner] != peer) return false;
  if (plan->track_count == SFU_PEER_TRACK_MAX) return false;
  for (size_t i = 0; i < plan->track_count; ++i) {
    if (plan->tracks[i].ssrc == ssrc) return false;
  }

  SfuPublisherTrack *current = NULL;
  if (previous && previous->bound_ssrc == ssrc && previous->clock_rate == section->clock_rate) {
    current = SfuPeerTrackFor(peer, ssrc);
  }
  SfuPublisherTrack *planned = &plan->tracks[plan->track_count++];
  *planned = current ? *current :
      (SfuPublisherTrack){.ssrc = ssrc, .kind = (uint8_t)section->kind, .clock_rate = section->clock_rate};
  if (planned->audio_level.extension_id != section->audio_level_extension_id ||
      planned->audio_level.allow_mixed != section->audio_level_allow_mixed) {
    memset(&planned->audio_level, 0, sizeof(planned->audio_level));
    planned->audio_level.extension_id = section->audio_level_extension_id;
    planned->audio_level.allow_mixed = section->audio_level_allow_mixed;
  }
  return true;
}

static bool
sPlanSubscription(SfuShard *shard, SfuRoom *room, SfuPeer *peer, const SfuPeerConfigSection *previous,
                  const SfuPeerConfigSection *section, size_t media_index, SfuPeerConfigPlan *plan)
{
  if (plan->subscription_count == SFU_PEER_SUBS_MAX) return false;
  SfuSubscription next;
  if (!sResolveSubscription(shard, room, peer, section, &next)) return false;
  uint32_t owner;
  uint8_t index;
  if (SfuShardSsrcLookup(shard, next.out_ssrc, &owner) && &shard->peers.slots[owner] != peer) return false;
  if (SfuShardOutboundLookup(shard, next.out_ssrc, &owner, &index)) {
    if (&shard->peers.slots[owner] != peer) return false;
    const SfuSubscription *current = &peer->subscriptions[index];
    /* An outbound SSRC can survive with its statistics and sequence mapping
     * only while it still refers to the same source on the same MID. */
    if (!previous || previous->outbound_ssrc != next.out_ssrc || current->publisher != next.publisher ||
        current->publisher_media_index != next.publisher_media_index ||
        current->publisher_ssrc != next.publisher_ssrc) return false;
    next = *current;
  } else if (peer && SfuShardSrtpOutboundUsed(peer, next.out_ssrc)) {
    /* A new binding must never reuse the previous source's SRTP packet indices. */
    return false;
  }
  if (previous && previous->direction == SFU_PEER_CONFIG_SUBSCRIBE &&
      previous->outbound_ssrc != section->outbound_ssrc &&
      SfuShardOutboundLookup(shard, previous->outbound_ssrc, &owner, &index) &&
      &shard->peers.slots[owner] == peer) {
    const SfuSubscription *current = &peer->subscriptions[index];
    if (current->publisher == next.publisher &&
        current->publisher_media_index == next.publisher_media_index) return false;
  }

  for (size_t i = 0; i < plan->subscription_count; ++i) {
    const SfuSubscription *other = &plan->subscriptions[i];
    if (next.out_ssrc == other->out_ssrc || (next.publisher == other->publisher &&
        next.publisher_media_index == other->publisher_media_index)) return false;
  }
  next.subscriber_media_index = media_index;
  plan->subscriptions[plan->subscription_count++] = next;
  return true;
}

static bool
sBuildPlan(SfuShard *shard, SfuRoom *room, SfuPeer *peer, SfuPeerConfigSnapshot *next, SfuPeerConfigPlan *plan)
{
  const SfuPeerConfigSnapshot *old = peer ? peer->config : NULL;
  if (old && old->media_count > next->media_count) return false;
  for (size_t i = 0; i < next->media_count; ++i) {
    SfuPeerConfigSection *section = &next->media[i];
    const SfuPeerConfigSection *previous = old && i < old->media_count ? &old->media[i] : NULL;
    if (previous && (strcmp(section->mid, previous->mid) != 0 || section->kind != previous->kind)) return false;
    if (section->direction == SFU_PEER_CONFIG_PUBLISH &&
        !sPlanPublication(shard, peer, previous, section, plan)) return false;
    if (section->direction == SFU_PEER_CONFIG_SUBSCRIBE &&
        !sPlanSubscription(shard, room, peer, previous, section, i, plan)) return false;
  }
  for (size_t i = 0; i < plan->track_count; ++i) {
    for (size_t j = 0; j < plan->subscription_count; ++j) {
      if (plan->tracks[i].ssrc == plan->subscriptions[j].out_ssrc) return false;
    }
  }
  size_t published = shard->ssrc_count;
  if (peer) {
    const uint32_t slot = (uint32_t)(peer - shard->peers.slots);
    for (size_t i = 0; i < peer->track_count; ++i) {
      uint32_t owner;
      if (SfuShardSsrcLookup(shard, peer->tracks[i].ssrc, &owner) && owner == slot) --published;
    }
  }
  return (published + plan->track_count) * 4u <= (shard->ssrc_mask + 1u) * 3u &&
         (shard->subscriptions.count - (peer ? peer->subscription_count : 0u) + plan->subscription_count) * 4u <=
             (shard->subscriptions.mask + 1u) * 3u;
}

static void
sDropChangedPublications(SfuShard *shard, SfuPeer *publisher, const SfuPeerConfigSnapshot *next)
{
  if (!publisher->config) return;
  const SfuPeerConfigSnapshot *old = publisher->config;
  SfuRoom *room = publisher->room;
  for (size_t i = 0; i < old->media_count; ++i) {
    const SfuPeerConfigSection *before = &old->media[i];
    const SfuPeerConfigSection *after = &next->media[i];
    if (before->direction != SFU_PEER_CONFIG_PUBLISH ||
        (after->direction == SFU_PEER_CONFIG_PUBLISH && before->bound_ssrc == after->bound_ssrc &&
         before->clock_rate == after->clock_rate)) continue;
    for (size_t j = 0; j < room->member_count; ++j) {
      SfuPeer *subscriber = room->members[j];
      for (size_t k = 0; k < SFU_PEER_SUBS_MAX; ++k) {
        SfuSubscription *sub = &subscriber->subscriptions[k];
        if (!sub->active) continue;
        if (sub->publisher ? sub->publisher == publisher && sub->publisher_media_index == i
                           : before->bound_ssrc && sub->publisher_ssrc == before->bound_ssrc) {
          sRemoveSubscription(shard, subscriber, sub);
        }
      }
    }
  }
}

/* Only new bindings allocate history. On failure the plan still borrows
 * existing histories, so release just the new allocations. */
static bool
sAllocateSentMappings(SfuPeerConfigPlan *plan)
{
  SfuRtpSentMapping *allocated[SFU_PEER_SUBS_MAX];
  size_t count = 0;
  for (size_t i = 0; i < plan->subscription_count; ++i) {
    SfuSubscription *sub = &plan->subscriptions[i];
    if (sub->sent_mapping) continue;
    sub->sent_mapping = calloc(SFU_NACK_VIDEO_SLOTS, sizeof(*sub->sent_mapping));
    if (!sub->sent_mapping) {
      for (size_t j = 0; j < count; ++j) free(allocated[j]);
      return false;
    }
    allocated[count++] = sub->sent_mapping;
    plan->new_sent_mapping[i] = true;
  }
  return true;
}

static void
sCommitMedia(SfuShard *shard, SfuPeer *peer, SfuPeerConfigSnapshot *next, SfuPeerConfigPlan *plan)
{
  const uint32_t slot = (uint32_t)(peer - shard->peers.slots);
  sDropChangedPublications(shard, peer, next);
  SfuShardSsrcRemovePeer(shard, slot);
  for (size_t i = 0; i < peer->track_count; ++i) {
    SfuPublisherTrack *track = &peer->tracks[i];
    bool retained = false;
    for (size_t j = 0; j < plan->track_count; ++j) {
      if (track->ring == plan->tracks[j].ring && track->ssrc == plan->tracks[j].ssrc) retained = true;
    }
    if (!retained) SfuShardNackRelease(shard, track);
  }

  for (size_t i = 0; i < SFU_PEER_SUBS_MAX; ++i) {
    SfuSubscription *old = &peer->subscriptions[i];
    if (!old->active) continue;
    (void)SfuShardOutboundRemove(shard, old->out_ssrc);
    bool retained = false;
    for (size_t j = 0; j < plan->subscription_count; ++j) {
      if (old->sent_mapping == plan->subscriptions[j].sent_mapping) retained = true;
    }
    if (!retained) free(old->sent_mapping);
  }
  memcpy(peer->tracks, plan->tracks, sizeof(peer->tracks));
  peer->track_count = plan->track_count;
  memcpy(peer->subscriptions, plan->subscriptions, sizeof(peer->subscriptions));
  peer->subscription_count = plan->subscription_count;
  /* Ownership and load checks in sBuildPlan guarantee these insertions on
   * the same shard turn, after the old keys have been removed. */
  for (size_t i = 0; i < peer->track_count; ++i) (void)SfuShardSsrcInsert(shard, peer->tracks[i].ssrc, slot);
  for (size_t i = 0; i < peer->subscription_count; ++i) {
    (void)SfuShardOutboundInsert(shard, peer->subscriptions[i].out_ssrc, slot, (uint8_t)i);
  }
  free(peer->config);
  peer->config = next;
}

static SfuPeerConfigResult
sDescribeConfigIce(SfuShard *shard, SfuPeer *peer, const SfuPeerConfigSnapshot *config, BufferDescriptor *ice)
{
  if (!ice) return SFU_PEER_CONFIG_APPLIED;
  if (peer && !sMatchesTransport(peer, config)) {
    SfuCommand restart = {
      .ufrag_length = (uint8_t)strlen(config->ufrag),
      .password_length = (uint8_t)strlen(config->remote_password)
    };
    memcpy(restart.ufrag, config->ufrag, restart.ufrag_length);
    memcpy(restart.restart_ufrag, config->remote_ufrag, strlen(config->remote_ufrag));
    memcpy(restart.password, config->remote_password, restart.password_length);
    SfuShardRestartIce(shard, &restart, ice);
    if (!ice->data || !ice->size || strstr(ice->data, "generation failed")) return SFU_PEER_CONFIG_UNAVAILABLE;
    return strstr(ice->data, "\"error\"") ? SFU_PEER_CONFIG_REJECTED : SFU_PEER_CONFIG_APPLIED;
  }
  const SfuIceGeneration *generation = !peer ? NULL : peer->ice_pending.local_ufrag[0] ? &peer->ice_pending
                                                                                     : &peer->ice_current;
  const bool restarted = generation && generation->local_ufrag[0];
  BufferDescriptorAppendFormatted(ice, "{\"ice_ufrag\":\"%s\",\"ice_pwd\":\"%s\"}",
                                  restarted ? generation->local_ufrag : shard->spec->ice_ufrag,
                                  restarted ? generation->local_password : shard->spec->ice_pwd);
  return ice->size ? SFU_PEER_CONFIG_APPLIED : SFU_PEER_CONFIG_UNAVAILABLE;
}

static SfuPeerConfigResult
sApplyPeerConfig(SfuShard *shard, SfuRoom *room, SfuPeer *peer, const SfuPeerConfigSnapshot *config,
                 BufferDescriptor *ice)
{
  if (peer && peer->config && config->revision <= peer->config->revision) {
    return config->revision == peer->config->revision && sSameConfig(peer->config, config)
             ? sDescribeConfigIce(shard, peer, config, ice) : SFU_PEER_CONFIG_CONFLICT;
  }
  if (!room || (peer && peer->room && peer->room != room) ||
      ((!peer || !peer->room) && room->member_count == room->member_capacity)) return SFU_PEER_CONFIG_REJECTED;
  if (peer && (!peer->dtls.has_remote_fingerprint ||
               memcmp(peer->dtls.remote_fingerprint, config->fingerprint, sizeof(config->fingerprint)) != 0 ||
               (!ice && !sMatchesTransport(peer, config)) ||
               (!peer->config && (peer->track_count || peer->subscription_count)))) return SFU_PEER_CONFIG_REJECTED;
  if (!peer && (SfuShardPeerByUfrag(shard, config->ufrag, strlen(config->ufrag)) ||
                SfuShardPeerByUfrag(shard, config->remote_ufrag, strlen(config->remote_ufrag)) ||
                SfuShardPeerByKey(shard, config->remote_ufrag, strlen(config->remote_ufrag)))) {
    return SFU_PEER_CONFIG_REJECTED;
  }

  SfuPeerConfigSnapshot *next = malloc(sizeof(*next));
  if (!next) return SFU_PEER_CONFIG_UNAVAILABLE;
  *next = *config;
  SfuPeerConfigPlan plan = {0};
  if (!sBuildPlan(shard, room, peer, next, &plan)) {
    free(next);
    return SFU_PEER_CONFIG_REJECTED;
  }
  if (!sAllocateSentMappings(&plan)) {
    free(next);
    return SFU_PEER_CONFIG_UNAVAILABLE;
  }
  SfuPeerConfigResult transport = sDescribeConfigIce(shard, peer, config, ice);
  if (transport != SFU_PEER_CONFIG_APPLIED) {
    for (size_t i = 0; i < plan.subscription_count; ++i) {
      if (plan.new_sent_mapping[i]) free(plan.subscriptions[i].sent_mapping);
    }
    free(next);
    return transport;
  }
  if (!peer) {
    SfuCommand registration = {
      .ufrag_length = (uint8_t)strlen(config->remote_ufrag),
      .password_length = (uint8_t)strlen(config->remote_password),
      .has_remote_fingerprint = true
    };
    memcpy(registration.ufrag, config->remote_ufrag, registration.ufrag_length);
    memcpy(registration.password, config->remote_password, registration.password_length);
    memcpy(registration.remote_fingerprint, config->fingerprint, sizeof(registration.remote_fingerprint));
    if (!SfuShardRegisterPeer(shard, &registration)) {
      for (size_t i = 0; i < plan.subscription_count; ++i) free(plan.subscriptions[i].sent_mapping);
      free(next);
      return SFU_PEER_CONFIG_REJECTED;
    }
    peer = SfuShardPeerByUfrag(shard, registration.ufrag, registration.ufrag_length);
  }
  if (!peer->room) sJoinRoom(shard, room, peer);
  sCommitMedia(shard, peer, next, &plan);
  return SFU_PEER_CONFIG_APPLIED;
}

SfuPeerConfigResult
SfuShardApplyPeerConfig(SfuShard *shard, const SfuPeerConfigSnapshot *config, BufferDescriptor *ice)
{
  SfuPeer *peer = SfuShardPeerByKey(shard, config->ufrag, strlen(config->ufrag));
  if (ice && !peer && config->revision != 1) return SFU_PEER_CONFIG_REJECTED;
  SfuRoom *room = sRoomById(shard, config->room_id);
  const bool create_room = ice && !room && !peer;
  if (create_room) {
    SfuShardCreateRoom(shard, config->room_id);
    room = sRoomById(shard, config->room_id);
    if (!room) return SFU_PEER_CONFIG_UNAVAILABLE;
  }
  const SfuPeerConfigResult result = sApplyPeerConfig(shard, room, peer, config, ice);
  if (create_room && result != SFU_PEER_CONFIG_APPLIED) sRoomRelease(shard, room);
  return result;
}
