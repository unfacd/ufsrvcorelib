/**
 * @file sfu_shard_ice.c
 * @brief STUN binding requests in, ICE state and a binding response out.
 *
 * The order is deliberate and security-relevant:
 *
 *   1. parse + FINGERPRINT        — cheap, catches garbage before any state
 *   2. USERNAME halves            — left names the peer, right must be ours
 *   3. MESSAGE-INTEGRITY          — checked with this shard's own ice-pwd
 *   4. replay guard               — an exact replay never reaches state
 *   5. address policy             — bind only now, never before step 3
 *   6. consent + nomination       — a replay can extend neither
 *   7. response                   — in place, same slot, no allocation
 *
 * The credentials are the shard's, not the peer's: RFC 8445 has the requester
 * sign with the responder's password, so a browser signs with the pair the SDP
 * answer advertised and verifies the response with the same one. The peer's
 * registered ufrag is what identifies it; its registered password is the
 * remote credential and is never used to check inbound requests.
 *
 * Only a nominated pair carries media. During a signaled ICE restart the
 * previously selected pair remains active until new credentials are nominated.
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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <ufsrvcorelib/ufsrv_core/turn/codec/ns_turn_ioaddr.h>
#include <ufsrvcorelib/ufsrv_core/turn/codec/ns_turn_msg.h>
#include <ufsrvcorelib/ufsrv_core/turn/codec/ns_turn_msg_defs.h>

#include "sfu_server_priv.h"
#include "dtls/sfu_shard_dtls.h"
#include "sfu_shard_flow.h"
#include "sfu_shard_nack.h"
#include "sfu_shard_ice.h"
#include "events/sfu_shard_events.h"
#include "sfu_shard_room.h"

/* RFC 8445 USE-CANDIDATE: not in the codec's defs because TURN never sends it. */
#define SFU_ATTRIBUTE_USE_CANDIDATE 0x0025u

/* Each SDP ice-char contributes six random bits: 48 for the ufrag and
 * 144 for the password at the configured lengths (RFC 8839 section 5.4). */
bool
SfuIceGenerateCredentials(char *ufrag, char *password)
{
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint8_t random[SFU_ICE_UFRAG_LENGTH + SFU_ICE_PWD_LENGTH];
  if (RAND_bytes(random, sizeof(random)) != 1) return false;

  for (size_t index = 0; index < SFU_ICE_UFRAG_LENGTH; index++) {
    ufrag[index] = alphabet[random[index] & 0x3Fu];
  }
  ufrag[SFU_ICE_UFRAG_LENGTH] = '\0';

  for (size_t index = 0; index < SFU_ICE_PWD_LENGTH; index++) {
    password[index] = alphabet[random[SFU_ICE_UFRAG_LENGTH + index] & 0x3Fu];
  }
  password[SFU_ICE_PWD_LENGTH] = '\0';
  return true;
}

static uint64_t
sNowMilliseconds(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000u + (uint64_t)(now.tv_nsec / 1000000);
}

static uint32_t
sUfragHash(const char *ufrag, size_t length)
{
  uint32_t hash = 2166136261u; /* FNV-1a */
  for (size_t index = 0; index < length; index++) {
    hash = (hash ^ (uint32_t)(unsigned char)ufrag[index]) * 16777619u;
  }
  return hash;
}

/* --- ufrag index: open addressing, backward-shift delete ------------------ */

SfuPeer *
SfuShardPeerByUfrag(SfuShard *shard, const char *ufrag, size_t length)
{
  if (!length || length > SFU_UFRAG_MAX) return NULL;

  const uint32_t hash = sUfragHash(ufrag, length);
  const size_t   mask = shard->ice.mask;
  size_t         index = hash & mask;

  for (size_t probe = 0; probe <= mask; probe++) {
    const SfuUfragEntry *entry = &shard->ice.index[index];
    if (!entry->slot_plus_one) return NULL;
    if (entry->hash == hash) {
      SfuPeer *peer = &shard->peers.slots[entry->slot_plus_one - 1u];
      if (peer->state != SFU_PEER_STATE_FREE && peer->ufrag_length == length &&
          memcmp(peer->ufrag, ufrag, length) == 0) {
        return peer;
      }
    }
    index = (index + 1) & mask;
  }
  return NULL;
}

static bool
sUfragInsert(SfuShard *shard, uint32_t slot)
{
  const SfuPeer *peer = &shard->peers.slots[slot];
  const size_t   mask = shard->ice.mask;
  const uint32_t hash = sUfragHash(peer->ufrag, peer->ufrag_length);

  if ((shard->ice.count + 1u) * 4u > (mask + 1u) * 3u) return false; /* 75% load */

  size_t index = hash & mask;
  for (size_t probe = 0; probe <= mask; probe++) {
    SfuUfragEntry *entry = &shard->ice.index[index];
    if (entry->slot_plus_one == 0) {
      entry->slot_plus_one = slot + 1u;
      entry->hash = hash;
      shard->ice.count++;
      return true;
    }
    if (entry->slot_plus_one == slot + 1u) return true;
    index = (index + 1) & mask;
  }
  return false;
}

static void
sUfragRemove(SfuShard *shard, uint32_t slot)
{
  const size_t   mask = shard->ice.mask;
  const uint32_t hash = sUfragHash(shard->peers.slots[slot].ufrag, shard->peers.slots[slot].ufrag_length);
  size_t         index = hash & mask;

  for (size_t probe = 0; probe <= mask; probe++) {
    SfuUfragEntry *entry = &shard->ice.index[index];
    if (!entry->slot_plus_one) return;
    if (entry->slot_plus_one == slot + 1u) {
      /* Backward-shift deletion keeps every probe chain terminating. */
      for (size_t next = (index + 1) & mask; shard->ice.index[next].slot_plus_one; next = (next + 1) & mask) {
        SfuUfragEntry *following = &shard->ice.index[next];
        const size_t home = following->hash & mask;
        if (((index - home) & mask) >= ((next - home) & mask)) continue;
        *entry = *following;
        entry = following;
        index = next;
      }
      entry->slot_plus_one = 0;
      entry->hash = 0;
      shard->ice.count--;
      return;
    }
    index = (index + 1) & mask;
  }
}

/* --- peer slots ----------------------------------------------------------- */

void
SfuShardPeerResetIce(SfuPeer *peer)
{
  peer->ufrag_length = 0;
  peer->password_length = 0;
  peer->ufrag[0] = '\0';
  memset(peer->local_pwd, 0, sizeof(peer->local_pwd));
  memset(peer->tid_ring, 0, sizeof(peer->tid_ring));
  peer->tid_next = 0;
  peer->ice_current = (SfuIceGeneration){0};
  peer->ice_pending = (SfuIceGeneration){0};
  peer->restart_started_ms = 0;
}

static SfuPeer *
sPeerAllocate(SfuShard *shard, uint32_t *slot_out)
{
  if (!shard->peers.free_head) return NULL;

  const uint32_t slot = shard->peers.free_head - 1u;
  SfuPeer *      peer = &shard->peers.slots[slot];
  shard->peers.free_head = peer->slot_next;

  peer->state = SFU_PEER_STATE_NEW;
  peer->slot_next = 0;
  peer->address_length = 0;
  memset(&peer->address, 0, sizeof(peer->address));
  peer->last_rx_ms = sNowMilliseconds();
  peer->last_consent_ms = 0;
  SfuShardDtlsReset(shard, peer);
  memset(peer->dtls.remote_fingerprint, 0, sizeof(peer->dtls.remote_fingerprint));
  peer->dtls.has_remote_fingerprint = false;
  SfuShardPeerResetIce(peer);

  shard->peers.count++;
  *slot_out = slot;
  return peer;
}

bool
SfuShardRegisterPeer(SfuShard *shard, const SfuCommand *command)
{
  SfuCounters *counters = shard->spec->counters;

  if (!command->ufrag_length || command->ufrag_length > SFU_UFRAG_MAX || !command->password_length ||
      command->password_length > SFU_PASSWORD_MAX) {
    counters->peers_dropped++;
    return false;
  }
  if (SfuShardPeerByUfrag(shard, command->ufrag, command->ufrag_length) ||
      SfuShardPeerByKey(shard, command->ufrag, command->ufrag_length)) {
    counters->peers_dropped++; /* duplicate ufrag: the map must stay unambiguous */
    return false;
  }

  uint32_t slot = 0;
  SfuPeer *peer = sPeerAllocate(shard, &slot);
  if (!peer) {
    counters->peers_dropped++;
    return false;
  }

  memcpy(peer->ufrag, command->ufrag, command->ufrag_length);
  peer->ufrag[command->ufrag_length] = '\0';
  memcpy(peer->local_pwd, command->password, command->password_length);
  peer->local_pwd[command->password_length] = '\0';
  peer->ufrag_length = command->ufrag_length;
  peer->password_length = command->password_length;
  if (command->has_remote_fingerprint) {
    memcpy(peer->dtls.remote_fingerprint, command->remote_fingerprint, sizeof(peer->dtls.remote_fingerprint));
    peer->dtls.has_remote_fingerprint = true;
  }

  if (!sUfragInsert(shard, slot)) {
    SfuShardPeerResetIce(peer);
    peer->state = SFU_PEER_STATE_FREE;
    peer->slot_next = shard->peers.free_head;
    shard->peers.free_head = slot + 1u;
    shard->peers.count--;
    counters->peers_dropped++;
    return false;
  }
  counters->peers_admitted++;
  return true;
}

/* --- address policy ------------------------------------------------------- */

static bool
sSameAddress(const struct sockaddr_storage *left, const struct sockaddr_storage *right)
{
  if (left->ss_family != right->ss_family) return false;

  if (left->ss_family == AF_INET) {
    const struct sockaddr_in *a = (const struct sockaddr_in *)left;
    const struct sockaddr_in *b = (const struct sockaddr_in *)right;
    return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
  }
  const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)left;
  const struct sockaddr_in6 *b = (const struct sockaddr_in6 *)right;
  return a->sin6_port == b->sin6_port && a->sin6_scope_id == b->sin6_scope_id &&
         memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0;
}

/* Return the original transaction for a retry, or remember a fresh check. */
static const SfuIceTransaction *
sRememberTransaction(SfuPeer *peer, const stun_tid *tid, const char *local_ufrag,
                     const struct sockaddr_storage *source)
{
  _Static_assert(SFU_ICE_TRANSACTION_ID_SIZE == STUN_TID_SIZE, "STUN transaction ID size");
  for (size_t index = 0; index < SFU_PEER_TID_RING; index++) {
    const SfuIceTransaction *entry = &peer->tid_ring[index];
    if (strcmp(local_ufrag, entry->local_ufrag) == 0 && memcmp(entry->id, tid->tsx_id, sizeof(entry->id)) == 0) {
      return entry;
    }
  }

  SfuIceTransaction *entry = &peer->tid_ring[peer->tid_next];
  memcpy(entry->id, tid->tsx_id, sizeof(entry->id));
  strcpy(entry->local_ufrag, local_ufrag);
  entry->source = *source;
  peer->tid_next = (uint8_t)((peer->tid_next + 1u) % SFU_PEER_TID_RING);
  return NULL;
}

static void
sUnbindAddress(SfuShard *shard, SfuPeer *peer)
{
  if (!peer->address_length) return;
  const SfuFlowKey key = SfuFlowKeyFromSource(shard, &peer->address);
  SfuFlowErase(shard, &key);
  peer->address_length = 0;
}

/** Install the new pair before removing the previous one. */
static bool
sBindAddress(SfuShard *shard, SfuPeer *peer, const struct sockaddr_storage *source, socklen_t source_length)
{
  if (peer->address_length && sSameAddress(&peer->address, source)) return true;

  const uint32_t slot = (uint32_t)(peer - shard->peers.slots);
  const SfuFlowKey key = SfuFlowKeyFromSource(shard, source);
  uint32_t owner = 0;
  /* The flow key may normalize addresses, so an existing entry for the same
   * peer must stay in place even if the sockaddr representation changed. */
  if (SfuFlowFind(shard, &key, &owner)) return owner == slot;
  if (!SfuFlowInsert(shard, &key, slot)) return false;

  sUnbindAddress(shard, peer);
  peer->address = *source;
  peer->address_length = source_length;
  return true;
}

void
SfuShardRestartIce(SfuShard *shard, const SfuCommand *command, BufferDescriptor *reply)
{
  SfuPeer *peer = SfuShardPeerByKey(shard, command->ufrag, command->ufrag_length);
  if (!peer) {
    BufferDescriptorAppendFormatted(reply, "{\"error\":\"unknown peer\"}");
    return;
  }

  SfuIceGeneration *pending = &peer->ice_pending;
  const SfuIceGeneration *current = &peer->ice_current;
  const char *remote_ufrag = current->local_ufrag[0] ? current->remote_ufrag : peer->ufrag;
  const char *remote_password = current->local_ufrag[0] ? current->remote_password : peer->local_pwd;
  const bool same_ufrag = strcmp(remote_ufrag, command->restart_ufrag) == 0;
  const bool same_password = strlen(remote_password) == command->password_length &&
                             memcmp(remote_password, command->password, command->password_length) == 0;
  const SfuIceGeneration *answer = pending;

  if (pending->local_ufrag[0]) {
    if (strcmp(pending->remote_ufrag, command->restart_ufrag) != 0 ||
        strlen(pending->remote_password) != command->password_length ||
        memcmp(pending->remote_password, command->password, command->password_length) != 0) {
      BufferDescriptorAppendFormatted(reply, "{\"error\":\"ICE restart already pending\"}");
      return;
    }
  } else if (same_ufrag && same_password && current->local_ufrag[0]) {
    answer = current; /* Retried signaling after the new pair was selected. */
  } else {
    if (same_ufrag || same_password) {
      BufferDescriptorAppendFormatted(reply, "{\"error\":\"both remote ICE credentials must change\"}");
      return;
    }

    SfuIceGeneration generation = {0};
    do {
      if (!SfuIceGenerateCredentials(generation.local_ufrag, generation.local_password)) {
        BufferDescriptorAppendFormatted(reply, "{\"error\":\"ICE credential generation failed\"}");
        return;
      }
      /* The random prefix authenticates the generation. The suffix routes
       * the check directly to its peer slot without an extra lookup table. */
      snprintf(generation.local_ufrag + SFU_ICE_UFRAG_LENGTH,
               sizeof(generation.local_ufrag) - SFU_ICE_UFRAG_LENGTH, "+%u",
               (unsigned)(peer - shard->peers.slots));
    } while (strcmp(generation.local_ufrag, current->local_ufrag) == 0 ||
             strcmp(generation.local_password, current->local_ufrag[0] ? current->local_password
                                                                    : shard->spec->ice_pwd) == 0);
    memcpy(generation.remote_ufrag, command->restart_ufrag, strlen(command->restart_ufrag));
    memcpy(generation.remote_password, command->password, command->password_length);
    *pending = generation;
    peer->restart_started_ms = sNowMilliseconds();
  }

  BufferDescriptorAppendFormatted(reply, "{\"ice_ufrag\":\"%s\",\"ice_pwd\":\"%s\"}",
                                  answer->local_ufrag, answer->local_password);
}

/* --- request handling ----------------------------------------------------- */

/**
 * Split USERNAME into its two halves: "<local_ufrag>:<peer_ufrag>".
 *
 * RFC 8445 has the requester put the receiving agent's fragment first, so the
 * left half must be this shard's own ufrag and the right half names the
 * registered peer. Both checks belong to the caller.
 */
static bool
sParseUsername(const SfuPacket *packet, size_t length, char *remote, size_t *remote_length, char *local,
               size_t *local_length)
{
  const uint8_t *buf = packet->data + SFU_PACKET_HEADROOM;

  const stun_attr_ref attr = stun_attr_get_first_covered_by_type_str(buf, length, STUN_ATTRIBUTE_USERNAME);
  if (!attr) return false;

  const int attr_length = stun_attr_get_len(attr);
  const uint8_t *value = stun_attr_get_value(attr);
  if (!value || attr_length <= 1 || (size_t)attr_length >= SFU_UFRAG_MAX * 2u + 2u) return false;

  char username[SFU_UFRAG_MAX * 2u + 2u];
  memcpy(username, value, (size_t)attr_length);
  username[attr_length] = '\0';

  const char *colon = memchr(username, ':', (size_t)attr_length);
  if (!colon) return false;
  if (memchr(colon + 1, ':', (size_t)(username + attr_length - (colon + 1)))) return false;

  const size_t left = (size_t)(colon - username);
  const size_t right = (size_t)attr_length - left - 1u;
  if (!left || left > SFU_ICE_RESTART_UFRAG_MAX || !right || right > SFU_UFRAG_MAX ||
      memchr(username, '\0', (size_t)attr_length)) return false;

  memcpy(remote, username, left);
  remote[left] = '\0';
  memcpy(local, colon + 1, right);
  local[right] = '\0';
  *remote_length = left;
  *local_length = right;
  return true;
}

static size_t
sBuildResponse(SfuPacket *packet, const stun_tid *tid, const struct sockaddr_storage *source, const char *username,
               const char *password)
{
  uint8_t *buf = packet->data + SFU_PACKET_HEADROOM;
  size_t   length = SFU_PACKET_CAPACITY;

  ioa_addr reflexive;
  memset(&reflexive, 0, sizeof(reflexive));
  if (source->ss_family == AF_INET) {
    addr_cpy4(&reflexive, (const struct sockaddr_in *)source);
  } else {
    addr_cpy6(&reflexive, (const struct sockaddr_in6 *)source);
  }

  if (!stun_set_binding_response_str(buf, &length, (stun_tid *)tid, &reflexive, 0, NULL, 0, false, false, false)) {
    return 0;
  }

  password_t pwd;
  memset(pwd, 0, sizeof(pwd));
  strncpy((char *)pwd, password, sizeof(password_t) - 1u);

  hmackey_t key;
  memset(key, 0, sizeof(key));
  if (!stun_attr_add_integrity_str(TURN_CREDENTIALS_SHORT_TERM, buf, &length, key, pwd, SHATYPE_SHA1)) return 0;
  if (!stun_attr_add_fingerprint_str(buf, &length)) return 0;

  (void)username; /* USERNAME is not echoed in a binding success response */
  return length;
}

SfuIceOutcome
SfuShardHandleStun(SfuShard *shard, SfuPacket *packet, const struct sockaddr_storage *source,
                   socklen_t source_length, size_t *reply_length)
{
  SfuCounters *counters = shard->spec->counters;
  uint8_t *    buf = packet->data + SFU_PACKET_HEADROOM;
  const size_t length = packet->length;

  counters->ice_requests++;

  bool fingerprint_present = false;
  if (!stun_is_command_message_full_check_str(buf, length, true, &fingerprint_present) || !fingerprint_present) {
    counters->ice_fingerprint_failures++;
    return SFU_ICE_DROPPED;
  }
  if (!stun_is_binding_request_str(buf, length, 0)) return SFU_ICE_DROPPED;

  char   peer_ufrag[SFU_UFRAG_MAX + 1u];
  char   local_ufrag[SFU_UFRAG_MAX + 1u];
  size_t peer_length = 0;
  size_t local_length = 0;
  if (!sParseUsername(packet, length, local_ufrag, &local_length, peer_ufrag, &peer_length)) {
    return SFU_ICE_DROPPED;
  }

  const SfuShardSpec *spec = shard->spec;
  const char *password = NULL;
  bool pending = false;
  SfuPeer *peer = NULL;

  if (strcmp(local_ufrag, spec->ice_ufrag) == 0) {
    peer = SfuShardPeerByUfrag(shard, peer_ufrag, peer_length);
    if (!peer) {
      counters->ice_unknown_ufrag++;
      return SFU_ICE_DROPPED;
    }
    if (!peer->ice_current.local_ufrag[0]) password = spec->ice_pwd;
  } else if (local_length > SFU_ICE_UFRAG_LENGTH + 1u && local_ufrag[SFU_ICE_UFRAG_LENGTH] == '+') {
    size_t slot = 0;
    for (size_t index = SFU_ICE_UFRAG_LENGTH + 1u; index < local_length; index++) {
      const char digit = local_ufrag[index];
      if (digit < '0' || digit > '9' || slot > (UINT32_MAX - (unsigned)(digit - '0')) / 10u) {
        slot = spec->peer_capacity;
        break;
      }
      slot = slot * 10u + (unsigned)(digit - '0');
    }
    if (slot < spec->peer_capacity) {
      peer = &shard->peers.slots[slot];
      const SfuIceGeneration *generation = &peer->ice_current;
      if (strcmp(local_ufrag, peer->ice_pending.local_ufrag) == 0) {
        generation = &peer->ice_pending;
        pending = true;
      }
      if (strcmp(local_ufrag, generation->local_ufrag) == 0 &&
          strcmp(peer_ufrag, generation->remote_ufrag) == 0) {
        password = generation->local_password;
      }
    }
  }
  if (!password) {
    counters->ice_ufrag_mismatch++;
    return SFU_ICE_DROPPED;
  }

  if (stun_check_message_integrity_str(TURN_CREDENTIALS_SHORT_TERM, buf, length, (const uint8_t *)peer_ufrag, NULL,
                                       (const uint8_t *)password, SHATYPE_SHA1) != 1) {
    counters->ice_auth_failures++;
    return SFU_ICE_DROPPED;
  }
  counters->ice_authenticated++;

  stun_tid tid;
  stun_tid_from_message_str(buf, length, &tid);
  const SfuIceTransaction *previous = sRememberTransaction(peer, &tid, local_ufrag, source);
  if (previous) {
    if (!sSameAddress(&previous->source, source)) {
      counters->ice_replay_dropped++;
      return SFU_ICE_DROPPED;
    }
    /* A lost response can be recovered without repeating any ICE state changes. */
  } else {
    const uint64_t now_ms = sNowMilliseconds();
    const stun_attr_ref use_candidate =
        stun_attr_get_first_covered_by_type_str(buf, length, SFU_ATTRIBUTE_USE_CANDIDATE);
    const bool nominate = use_candidate && stun_attr_get_len(use_candidate) == 0;
    if (nominate && (pending || (peer->state == SFU_PEER_STATE_NEW && !peer->restart_started_ms))) {
      if (!sBindAddress(shard, peer, source, source_length)) {
        /* Leave the transaction retryable if the target flow could not be installed. */
        peer->tid_next = (uint8_t)((peer->tid_next + SFU_PEER_TID_RING - 1u) % SFU_PEER_TID_RING);
        peer->tid_ring[peer->tid_next].local_ufrag[0] = '\0';
        return SFU_ICE_DROPPED;
      }
      if (pending) {
        if (peer->state == SFU_PEER_STATE_CONNECTED) counters->ice_rebind_promoted++;
        peer->ice_current = peer->ice_pending;
        peer->ice_pending = (SfuIceGeneration){0};
        peer->restart_started_ms = 0;
        password = peer->ice_current.local_password;
        pending = false;
      }
      peer->state = SFU_PEER_STATE_CONNECTED;
      counters->ice_nominated++;
    }
    if (!pending && peer->address_length && sSameAddress(&peer->address, source)) {
      peer->last_consent_ms = now_ms;
      peer->last_rx_ms = now_ms;
    } else if (peer->state == SFU_PEER_STATE_NEW && !peer->restart_started_ms) {
      peer->last_rx_ms = now_ms;
    }
  }

  const size_t reply = sBuildResponse(packet, &tid, source, peer_ufrag, password);
  if (!reply) return SFU_ICE_DROPPED;
  *reply_length = reply;
  return SFU_ICE_REPLY;
}

/* --- expiry --------------------------------------------------------------- */

bool
SfuShardPeerExpire(SfuShard *shard, SfuPeer *peer, size_t slot, uint64_t now_ms)
{
  SfuCounters * counters = shard->spec->counters;
  const uint64_t consent_ms = (uint64_t)shard->spec->ice_consent_timeout_seconds * 1000u;
  const uint64_t idle_ms = (uint64_t)shard->spec->peers_idle_timeout_seconds * 1000u;

  /* A timestamp newer than the sweep clock means this turn's now is stale; no
   * age can be computed from it. */
  const bool clock_current = now_ms >= peer->last_rx_ms && now_ms >= peer->last_consent_ms;
  if (!clock_current) return false;
  if (peer->restart_started_ms && idle_ms && now_ms >= peer->restart_started_ms &&
      now_ms - peer->restart_started_ms >= idle_ms) {
    peer->ice_pending = (SfuIceGeneration){0};
    peer->restart_started_ms = 0;
    if (!peer->address_length) {
      SfuShardPeerRelease(shard, peer, slot, SFU_DISC_ICE_TIMEOUT);
      return true;
    }
  }
  if (peer->state == SFU_PEER_STATE_CONNECTED) {
    if (now_ms - peer->last_consent_ms < consent_ms) return false;
    counters->ice_consent_expired++;
    if (peer->restart_started_ms) {
      sUnbindAddress(shard, peer);
      peer->state = SFU_PEER_STATE_NEW;
      return false;
    }
  } else {
    if (peer->restart_started_ms || !idle_ms || now_ms - peer->last_rx_ms < idle_ms) return false;
    counters->peers_expired++;
  }

  SfuShardPeerRelease(shard, peer, slot, SFU_DISC_ICE_TIMEOUT);
  return true;
}

void
SfuShardPeerRelease(SfuShard *shard, SfuPeer *peer, size_t slot, SfuDisconnectReasonEnum reason)
{
  SfuCounters *counters = shard->spec->counters;

  SfuShardEventPeerDisconnected(shard, peer, reason);
  SfuShardRoomDetachPeer(shard, peer);
  SfuShardSsrcRemovePeer(shard, (uint32_t)slot);

  sUnbindAddress(shard, peer);
  if (peer->ufrag_length) sUfragRemove(shard, (uint32_t)slot);
  SfuShardDtlsReset(shard, peer);

  SfuShardPeerResetIce(peer);
  free(peer->config);
  peer->config = NULL;
  for (size_t track = 0; track < peer->track_count; track++) SfuShardNackRelease(shard, &peer->tracks[track]);
  peer->room = NULL;
  peer->track_count = 0;
  peer->state = SFU_PEER_STATE_FREE;
  peer->address_length = 0;
  peer->last_rx_ms = 0;
  peer->last_consent_ms = 0;
  peer->slot_next = shard->peers.free_head;
  shard->peers.free_head = (uint32_t)slot + 1u;
  shard->peers.count--;
  counters->peers_released++;
}

void
SfuShardLeavePeer(SfuShard *shard, const char *ufrag, size_t ufrag_length)
{
  SfuPeer *peer = SfuShardPeerByKey(shard, ufrag, ufrag_length);
  if (!peer) {
    shard->spec->counters->peers_dropped++;
    return;
  }
  SfuShardPeerRelease(shard, peer, (size_t)(peer - shard->peers.slots), SFU_DISC_ADMIN_KICK);
}
