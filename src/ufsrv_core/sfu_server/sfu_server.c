/**
 * Copyright (c) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file sfu_server.c
 * @brief Lifetime of a shard set: start one thread per shard, deliver
 *        signalling commands, join on stop.
 *
 * The server owns no socket and no per-shard state: onboarding already created
 * every context and the host owns the set. What is left here is the control
 * plane, which is deliberately the only cross-thread path into a shard.
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <uflib/cdt/cdt_mpsc_queue.h>
#include <uflib/cdt/cdt_treiber_stack.h>

#include "sfu_server_priv.h"

#include <ufsrvcorelib/ufsrv_core/sfu_server/command/sfu_console_router.h>
#include "dtls/sfu_dtls_context.h"
#include "dtls/sfu_shard_dtls.h"
#include "events/sfu_event_egress.h"

/** Raise the shard's eventfd: the only wake-up it has. */
static void
sKickShard(const SfuShardSpec *spec)
{
  const uint64_t token = 1;
  while (write(spec->wake_fd, &token, sizeof(token)) < 0 && errno == EINTR) {
  }
}

SfuServer *
SfuServerStart(SfuShardSet *shards)
{
  if (!shards) return NULL;
  if (!shards->count || !shards->specs || !shards->shards) {
    SfuShardOffboard(shards);
    return NULL;
  }

  SfuServer *server = calloc(1, sizeof(*server));
  if (!server) {
    SfuShardOffboard(shards);
    return NULL;
  }
  server->set = shards;

  /* One DTLS context for the process, built before any shard thread exists so the certificate, its fingerprint and the SRTP profile list are immutable
   * for the lifetime of the set. */
  server->dtls = SfuDtlsContextCreate(NULL, NULL);
  if (!server->dtls) {
    SfuShardOffboard(shards);
    free(server);
    return NULL;
  }
  if (shards->events.mode != SFU_EVENT_EGRESS_DISABLED) {
    /* The transport adapter will supply the worker's consumer. */
    server->event_egress = SfuEventEgressStart(shards->events.queue_capacity, NULL, NULL);
    if (!server->event_egress) {
      syslog(LOG_ERR, "%s: could not initialize SFU event egress", __func__);
      SfuServerStop(server);
      return NULL;
    }
  }
  for (size_t index = 0; index < shards->count; index++) {
    shards->shards[index].dtls_context = server->dtls;
    shards->shards[index].event_egress = server->event_egress;
    shards->shards[index].event_settings = &shards->events;
  }

  size_t started = 0;
  for (; started < shards->count; started++) {
    SfuShard *shard = &shards->shards[started];
    atomic_store(&shard->up, 1);
    if (pthread_create(&shard->thread, NULL, SfuShardThread, shard) != 0) {
      syslog(LOG_ERR, "%s: COULD NOT start shard %zu", __func__, started);
      atomic_store(&shard->up, 0);
      break;
    }
  }
  server->threads_started = started;

  if (started != shards->count) {
    SfuServerStop(server);
    return NULL;
  }

  syslog(LOG_INFO, "%s: shards:'%zu', first_port:'%u'", __func__, shards->count, shards->specs[0].port);
  return server;
}

/**
 * Queue one command on its shard. reply is NULL for fire-and-forget, or the
 * requestor's envelope for a command whose answer it waits on.
 */
static bool
sEnqueue(SfuServer *server, const SfuCommand *command, SfuCommandReply *reply)
{
  SfuShardSet *set = server->set;
  if (command->shard_index >= set->count) return false;

  SfuShard *shard = &set->shards[command->shard_index];
  if (!atomic_load(&shard->up)) {
    shard->spec->counters->commands_dropped++;
    return false;
  }

  const uint32_t index = CDT_TreiberStackPop(&shard->commands.free, shard->commands.pool, sizeof(SfuCommandNode),
                                             offsetof(SfuCommandNode, next_free));
  if (!index) {
    /* The pool is the bound the unbounded queue does not provide. */
    shard->spec->counters->commands_dropped++;
    return false;
  }

  if (reply) {
    pthread_mutex_lock(&reply->mutex);
    reply->state = SFU_COMMAND_STATE_QUEUED;
    pthread_mutex_unlock(&reply->mutex);
  }

  SfuCommandNode *node = &shard->commands.pool[index];
  node->command        = *command;
  node->reply          = reply;
  mpsc_queue_insert(&shard->commands.queue, &node->node);
  sKickShard(shard->spec);
  return true;
}

static bool
sUfragValid(const char *ufrag)
{
  if (!ufrag) return false;
  const size_t length = strlen(ufrag);
  return length > 0 && length <= SFU_COMMAND_UFRAG_MAX;
}

PUBLIC_API bool
SfuServerCreateRoom(SfuServer *server, size_t shard_index, uint64_t room_id)
{
  if (!server || !room_id) return false;
  const SfuCommand command = {.type = SFU_COMMAND_CREATE_ROOM, .shard_index = shard_index, .room_id = room_id};
  return sEnqueue(server, &command, NULL);
}

PUBLIC_API bool
SfuServerJoinRoom(SfuServer *server, size_t shard_index, uint64_t room_id, const char *ufrag)
{
  if (!server || !room_id || !sUfragValid(ufrag)) return false;
  SfuCommand command = {
    .type         = SFU_COMMAND_JOIN_ROOM, .shard_index = shard_index, .room_id = room_id,
    .ufrag_length = (uint8_t)strlen(ufrag)
  };
  memcpy(command.ufrag, ufrag, command.ufrag_length);
  return sEnqueue(server, &command, NULL);
}

PUBLIC_API bool
SfuServerAddTrack(SfuServer *  server, size_t shard_index, uint64_t room_id, const char *ufrag, uint32_t ssrc,
                  SfuTrackKind kind, uint32_t clock_rate)
{
  if (!server || !room_id || !ssrc || !sUfragValid(ufrag)) return false;
  SfuCommand command = {
    .type       = SFU_COMMAND_ADD_TRACK, .shard_index = shard_index, .room_id     = room_id, .ssrc = ssrc,
    .track_kind = (uint8_t)kind, .clock_rate          = clock_rate, .ufrag_length = (uint8_t)strlen(ufrag)
  };
  memcpy(command.ufrag, ufrag, command.ufrag_length);
  return sEnqueue(server, &command, NULL);
}

PUBLIC_API bool
SfuServerSubscribe(SfuServer *server, size_t           shard_index, uint64_t room_id, const char *subscriber_ufrag,
                   uint32_t   publisher_ssrc, uint32_t out_ssrc, uint16_t    seq_offset)
{
  if (!server || !room_id || !publisher_ssrc || !out_ssrc || !sUfragValid(subscriber_ufrag)) return false;
  SfuCommand command = {
    .type     = SFU_COMMAND_SUBSCRIBE, .shard_index = shard_index, .room_id     = room_id, .ssrc = publisher_ssrc,
    .out_ssrc = out_ssrc, .seq_offset               = seq_offset, .ufrag_length = (uint8_t)strlen(subscriber_ufrag)
  };
  memcpy(command.ufrag, subscriber_ufrag, command.ufrag_length);
  return sEnqueue(server, &command, NULL);
}

PUBLIC_API bool
SfuServerLeavePeer(SfuServer *server, size_t shard_index, const char *ufrag)
{
  if (!server || !sUfragValid(ufrag)) return false;
  SfuCommand command = {
    .type = SFU_COMMAND_LEAVE_PEER, .shard_index = shard_index, .ufrag_length = (uint8_t)strlen(ufrag)
  };
  memcpy(command.ufrag, ufrag, command.ufrag_length);
  return sEnqueue(server, &command, NULL);
}

PUBLIC_API bool
SfuServerCertificateFingerprint(const SfuServer *server, uint8_t out[32])
{
  if (!server || !out) return false;
  return SfuDtlsContextFingerprint(server->dtls, out);
}

/** The address the shard's socket answers on, as text; a wildcard bind says so. */
static void
sBoundAddress(const SfuShardSpec *spec, char *out, size_t out_size)
{
  static const uint8_t kUnspecified6[16] = {0};

  if (spec->family == AF_INET6) {
    if (memcmp(spec->bound_address6, kUnspecified6, sizeof(kUnspecified6)) != 0 &&
        inet_ntop(AF_INET6, spec->bound_address6, out, (socklen_t)out_size)) {
      return;
    }
    snprintf(out, out_size, "::");
    return;
  }
  if (spec->bound_address && inet_ntop(AF_INET, &spec->bound_address, out, (socklen_t)out_size)) return;
  snprintf(out, out_size, "0.0.0.0");
}

PUBLIC_API BufferDescriptor *
SfuServerDescribeTransport(SfuServer *server, size_t shard_index, BufferDescriptor *reply)
{
  if (!reply || !reply->data) return reply;
  if (!server || shard_index >= server->set->count) {
    BufferDescriptorAppendFormatted(reply, "{\"error\":\"unknown shard\"}");
    return reply;
  }

  const SfuShardSpec *spec = &server->set->specs[shard_index];
  char                address[INET6_ADDRSTRLEN] = {0};
  sBoundAddress(spec, address, sizeof(address));

  BufferDescriptorAppendFormatted(reply,
                                  "{\"shard\":%zu,\"address\":\"%s\",\"port\":%u,\"ice_ufrag\":\"%s\","
                                  "\"ice_pwd\":\"%s\",\"dtls_fingerprint\":\"",
                                  spec->index, address, spec->port, spec->ice_ufrag, spec->ice_pwd);

  uint8_t fingerprint[32];
  if (SfuDtlsContextFingerprint(server->dtls, fingerprint)) {
    for (size_t index = 0; index < sizeof(fingerprint); index++) {
      BufferDescriptorAppendFormatted(reply, "%s%02x", index ? ":" : "", fingerprint[index]);
    }
  }
  BufferDescriptorAppendFormatted(reply, "\"}");
  return reply;
}

bool
SfuServerSignalCommand(SfuServer *server, const SfuCommand *command)
{
  if (!server || !command || command->type == SFU_COMMAND_APPLY_PEER_CONFIG) return false;
  return sEnqueue(server, command, NULL);
}

/** How long a request will wait for a shard before reporting it unresponsive. */
#define SFU_COMMAND_TIMEOUT_SECONDS 5u

static bool
sRequestShard(SfuServer *server, const SfuCommand *command, BufferDescriptor *reply,
              const SfuPeerConfigSnapshot *config, SfuPeerConfigResult *config_result)
{
  if (reply && !reply->data) return false;

  if (!server) {
    if (reply) BufferDescriptorAppendFormatted(reply, "{\"error\":\"no server\"}");
    return false;
  }

  /* The envelope may outlive this call: a timed-out request is handed to the
   * shard to free, so it can never live on the requestor's stack. */
  SfuCommandReply *envelope = calloc(1, sizeof(*envelope) + (config ? sizeof(*config) : 0u));
  if (!envelope) {
    if (reply) BufferDescriptorAppendFormatted(reply, "{\"error\":\"envelope\"}");
    return false;
  }
  if (pthread_mutex_init(&envelope->mutex, NULL) != 0) {
    free(envelope);
    if (reply) BufferDescriptorAppendFormatted(reply, "{\"error\":\"envelope mutex\"}");
    return false;
  }
  if (pthread_cond_init(&envelope->done, NULL) != 0) {
    pthread_mutex_destroy(&envelope->mutex);
    free(envelope);
    if (reply) BufferDescriptorAppendFormatted(reply, "{\"error\":\"envelope condition\"}");
    return false;
  }
  if (reply) BufferDescriptorInit(&envelope->reply, 256);
  if (reply && !envelope->reply.data) {
    pthread_cond_destroy(&envelope->done);
    pthread_mutex_destroy(&envelope->mutex);
    free(envelope);
    BufferDescriptorAppendFormatted(reply, "{\"error\":\"envelope buffer\"}");
    return false;
  }
  if (config) {
    envelope->config = (SfuPeerConfigSnapshot *)(envelope + 1);
    *envelope->config = *config;
  }
  envelope->state = SFU_COMMAND_STATE_IDLE;

  if (!sEnqueue(server, command, envelope)) {
    BufferDescriptorRelease(&envelope->reply);
    pthread_cond_destroy(&envelope->done);
    pthread_mutex_destroy(&envelope->mutex);
    free(envelope);
    if (reply) {
      BufferDescriptorAppendFormatted(reply, "{\"error\":\"shard unavailable\",\"shard\":%zu}", command->shard_index);
    }
    return false;
  }

  struct timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += SFU_COMMAND_TIMEOUT_SECONDS;

  bool answered = false;
  pthread_mutex_lock(&envelope->mutex);
  while (envelope->state != SFU_COMMAND_STATE_DONE) {
    const int rc = pthread_cond_timedwait(&envelope->done, &envelope->mutex, &deadline);
    if (rc != 0 && rc != EINTR) break;
  }
  answered = envelope->state == SFU_COMMAND_STATE_DONE;
  if (!answered) envelope->state = SFU_COMMAND_STATE_ABANDONED;
  pthread_mutex_unlock(&envelope->mutex);

  if (!answered) {
    /* The shard owns the envelope and releases it when it drains the command. */
    if (reply) {
      BufferDescriptorAppendFormatted(reply, "{\"error\":\"shard did not answer within %u seconds\",\"shard\":%zu}",
                                      SFU_COMMAND_TIMEOUT_SECONDS, command->shard_index);
    }
    return false;
  }

  if (reply) {
    if (envelope->reply.data && envelope->reply.size > 0) {
      BufferDescriptorAppendFormatted(reply, "%s", envelope->reply.data);
    } else {
      BufferDescriptorAppendFormatted(reply, "{\"error\":\"empty shard reply\"}");
    }
  }
  if (config_result) *config_result = envelope->config_result;
  const bool accepted = envelope->accepted;
  BufferDescriptorRelease(&envelope->reply);
  pthread_cond_destroy(&envelope->done);
  pthread_mutex_destroy(&envelope->mutex);
  free(envelope);
  return accepted;
}

PUBLIC_API bool
SfuServerRegisterPeer(SfuServer *server, size_t shard_index, uint64_t peer_id, const char *ufrag, const char *password,
                      const uint8_t *remote_fingerprint)
{
  if (!server || !ufrag || !password) return false;

  const size_t ufrag_length    = strlen(ufrag);
  const size_t password_length = strlen(password);
  if (!ufrag_length || ufrag_length > SFU_COMMAND_UFRAG_MAX || !password_length || password_length >
    SFU_COMMAND_PASSWORD_MAX) {
    return false;
  }

  SfuCommand command = {
    .type         = SFU_COMMAND_REGISTER_PEER, .shard_index = shard_index, .peer_id = peer_id,
    .ufrag_length = (uint8_t)ufrag_length, .password_length = (uint8_t)password_length
  };
  memcpy(command.ufrag, ufrag, ufrag_length);
  memcpy(command.password, password, password_length);
  if (remote_fingerprint) {
    memcpy(command.remote_fingerprint, remote_fingerprint, sizeof(command.remote_fingerprint));
    command.has_remote_fingerprint = true;
  }
  return sRequestShard(server, &command, NULL, NULL, NULL);
}

PUBLIC_API bool
SfuServerPeerLifecycle(SfuServer *server, size_t shard_index, const char *ufrag, bool *present)
{
  if (!server || !sUfragValid(ufrag)) return false;
  SfuCommand command = {
    .type = present ? SFU_COMMAND_PEER_STATUS : SFU_COMMAND_LEAVE_PEER,
    .shard_index = shard_index,
    .ufrag_length = (uint8_t)strlen(ufrag)
  };
  memcpy(command.ufrag, ufrag, command.ufrag_length);
  if (!present) return sRequestShard(server, &command, NULL, NULL, NULL);

  BufferDescriptor document = {0};
  BufferDescriptorInit(&document, 64);
  const bool answered = sRequestShard(server, &command, &document, NULL, NULL);
  if (answered) *present = strcmp(document.data, "true") == 0;
  BufferDescriptorRelease(&document);
  return answered;
}

PUBLIC_API BufferDescriptor *
SfuServerDescribeShard(SfuServer *server, size_t shard_index, BufferDescriptor *reply)
{
  if (!reply || !reply->data) return reply;
  const SfuCommand command = {.type = SFU_COMMAND_DESCRIBE_SHARD, .shard_index = shard_index};
  (void)sRequestShard(server, &command, reply, NULL, NULL);
  return reply;
}

BufferDescriptor *
SfuServerDescribeRoom(SfuServer *server, size_t shard_index, uint64_t room_id, BufferDescriptor *reply)
{
  if (!reply || !reply->data) return reply;
  const SfuCommand command = {.type = SFU_COMMAND_DESCRIBE_ROOM, .shard_index = shard_index, .room_id = room_id};
  (void)sRequestShard(server, &command, reply, NULL, NULL);
  return reply;
}

PUBLIC_API BufferDescriptor *
SfuServerRestartIce(SfuServer *server, size_t shard_index, const char *ufrag, const char *remote_ufrag,
                    const char *remote_password, BufferDescriptor *reply)
{
  static const char ice_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  if (!reply || !reply->data) return reply;
  const size_t remote_length = remote_ufrag ? strlen(remote_ufrag) : 0;
  const size_t password_length = remote_password ? strlen(remote_password) : 0;
  if (!sUfragValid(ufrag) || remote_length < 4u || remote_length > SFU_COMMAND_UFRAG_MAX ||
      password_length < 22u || password_length > SFU_COMMAND_PASSWORD_MAX ||
      strspn(remote_ufrag, ice_chars) != remote_length || strspn(remote_password, ice_chars) != password_length) {
    BufferDescriptorAppendFormatted(reply, "{\"error\":\"invalid ICE restart credentials\"}");
    return reply;
  }

  SfuCommand command = {.type = SFU_COMMAND_RESTART_ICE, .shard_index = shard_index,
                        .ufrag_length = (uint8_t)strlen(ufrag), .password_length = (uint8_t)password_length};
  memcpy(command.ufrag, ufrag, command.ufrag_length);
  memcpy(command.restart_ufrag, remote_ufrag, remote_length);
  memcpy(command.password, remote_password, password_length);
  (void)sRequestShard(server, &command, reply, NULL, NULL);
  return reply;
}

void
SfuServerStop(SfuServer *server)
{
  if (!server) return;

  SfuShardSet *set = server->set;
  for (size_t index = 0; index < server->threads_started; index++) {
    atomic_store(&set->shards[index].up, 0);
    sKickShard(set->shards[index].spec);
  }
  for (size_t index = 0; index < server->threads_started; index++) pthread_join(set->shards[index].thread, NULL);
  SfuEventEgressStop(server->event_egress);

  /* Threads are joined: release every peer's SSL and SRTP sessions before the
   * context (and libsrtp's global state) goes away. */
  for (size_t index = 0; index < set->count; index++) {
    SfuShard *shard = &set->shards[index];
    for (size_t slot = 0; slot < shard->spec->peer_capacity; slot++) {
      if (shard->peers.slots[slot].state == SFU_PEER_STATE_FREE) continue;
      SfuPeer *peer = &shard->peers.slots[slot];
      for (size_t i = 0; i < SFU_PEER_SUBS_MAX; ++i) free(peer->subscriptions[i].sent_mapping);
      free(peer->config);
      SfuShardDtlsReset(shard, &shard->peers.slots[slot]);
      shard->peers.slots[slot].state = SFU_PEER_STATE_FREE;
    }
  }
  SfuDtlsContextDestroy(server->dtls);
  SfuCommandConsoleDetachResourceRouter(server);
  free(server);
  SfuShardOffboard(set);
}

static bool
sCopyConfigToken(char *out, size_t capacity, const char *value, size_t minimum, const char *alphabet)
{
  if (!value) return false;
  const size_t length = strnlen(value, capacity);
  if (length < minimum || length == capacity || strspn(value, alphabet) != length) return false;
  memcpy(out, value, length);
  return true;
}

static bool
sCopyConfigSection(SfuPeerConfigSection *out, const SfuPeerConfigMediaV1 *media)
{
  static const char ice_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  static const char mid_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!#$%&'*+-.^_`{|}~";
  if (!sCopyConfigToken(out->mid, sizeof(out->mid), media->mid, 1, mid_chars) ||
      (media->kind != SFU_TRACK_AUDIO && media->kind != SFU_TRACK_VIDEO)) return false;
  out->kind = media->kind;
  out->direction = media->direction;
  if (media->direction == SFU_PEER_CONFIG_INACTIVE) {
    return !media->clock_rate && !media->payload_type && !media->mid_extension_id && !media->audio_level_extension_id &&
           !media->audio_level_allow_mixed &&
           !media->publisher_ufrag && !media->publisher_mid && !media->publisher_ssrc && !media->outbound_ssrc;
  }
  if (!media->clock_rate || !media->mid_extension_id || media->payload_type > 127 ||
      (media->payload_type >= 64 && media->payload_type <= 95)) return false;
  out->clock_rate = media->clock_rate;
  out->payload_type = media->payload_type;
  out->mid_extension_id = media->mid_extension_id;
  if (media->audio_level_extension_id &&
      (media->direction != SFU_PEER_CONFIG_PUBLISH || media->kind != SFU_TRACK_AUDIO ||
       media->audio_level_extension_id == media->mid_extension_id)) return false;
  if (media->audio_level_allow_mixed && !media->audio_level_extension_id) return false;
  out->audio_level_extension_id = media->audio_level_extension_id;
  out->audio_level_allow_mixed = media->audio_level_allow_mixed;
  if (media->direction == SFU_PEER_CONFIG_PUBLISH) {
    out->publisher_ssrc = media->publisher_ssrc;
    out->bound_ssrc = media->publisher_ssrc;
    return !media->publisher_ufrag && !media->publisher_mid && !media->outbound_ssrc;
  }
  out->outbound_ssrc = media->outbound_ssrc;
  return media->direction == SFU_PEER_CONFIG_SUBSCRIBE && media->outbound_ssrc && !media->publisher_ssrc &&
         sCopyConfigToken(out->publisher_ufrag, sizeof(out->publisher_ufrag), media->publisher_ufrag, 1, ice_chars) &&
         sCopyConfigToken(out->publisher_mid, sizeof(out->publisher_mid), media->publisher_mid, 1, mid_chars);
}

static SfuPeerConfigResult
sApplyPeerConfigV1(SfuServer *server, size_t shard_index, const SfuPeerConfigV1 *config, BufferDescriptor *ice)
{
  static const char ice_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (!server) return SFU_PEER_CONFIG_UNAVAILABLE;
  if (shard_index >= server->set->count || !config || !config->revision || !config->room_id ||
      config->media_count > SFU_CONFIG_MEDIA_MAX || (config->media_count && !config->media)) {
    return SFU_PEER_CONFIG_REJECTED;
  }
  SfuPeerConfigSnapshot snapshot = {
    .revision = config->revision, .room_id = config->room_id, .media_count = config->media_count
  };
  if (!sCopyConfigToken(snapshot.ufrag, sizeof(snapshot.ufrag), config->ufrag, 1, ice_chars) ||
      !sCopyConfigToken(snapshot.remote_ufrag, sizeof(snapshot.remote_ufrag), config->remote_ufrag, 4, ice_chars) ||
      !sCopyConfigToken(snapshot.remote_password, sizeof(snapshot.remote_password), config->remote_password, 22, ice_chars)) {
    return SFU_PEER_CONFIG_REJECTED;
  }
  memcpy(snapshot.fingerprint, config->fingerprint, sizeof(snapshot.fingerprint));
  for (size_t i = 0; i < snapshot.media_count; ++i) {
    if (!sCopyConfigSection(&snapshot.media[i], &config->media[i])) return SFU_PEER_CONFIG_REJECTED;
    for (size_t j = 0; j < i; ++j) {
      const SfuPeerConfigSection *a = &snapshot.media[i], *b = &snapshot.media[j];
      if (strcmp(a->mid, b->mid) == 0 ||
          (a->audio_level_extension_id && a->audio_level_extension_id == b->mid_extension_id) ||
          (b->audio_level_extension_id && b->audio_level_extension_id == a->mid_extension_id)) {
        return SFU_PEER_CONFIG_REJECTED;
      }
    }
  }
  const SfuCommand command = {
    .type = ice ? SFU_COMMAND_APPLY_NEGOTIATED_CONFIG : SFU_COMMAND_APPLY_PEER_CONFIG, .shard_index = shard_index
  };
  SfuPeerConfigResult result = SFU_PEER_CONFIG_UNAVAILABLE;
  (void)sRequestShard(server, &command, ice, &snapshot, &result);
  return result;
}

PUBLIC_API SfuPeerConfigResult
SfuServerApplyPeerConfigV1(SfuServer *server, size_t shard_index, const SfuPeerConfigV1 *config)
{
  return sApplyPeerConfigV1(server, shard_index, config, NULL);
}

PUBLIC_API SfuPeerConfigResult
SfuServerApplyNegotiatedPeerConfigV1(SfuServer *server, size_t shard_index, const SfuPeerConfigV1 *config,
                                   BufferDescriptor *ice)
{
  if (!ice || !ice->data) return SFU_PEER_CONFIG_UNAVAILABLE;
  return sApplyPeerConfigV1(server, shard_index, config, ice);
}
