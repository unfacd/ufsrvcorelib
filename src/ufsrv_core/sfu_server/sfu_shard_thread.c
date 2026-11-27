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
 * @file sfu_shard_thread.c
 * @brief One thread per shard: apply signalling commands between wake-ups.
 *
 * The media path (recvmmsg/sendmmsg, DTLS, SRTP) lands with the I/O engine; what
 * exists here is the lifecycle and the control-plane drain the engine will run
 * at the top of every turn. The thread touches only its own SfuShard, plus the
 * one stop flag the main thread writes.
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <uflib/cdt/cdt_mpsc_queue.h>
#include <uflib/cdt/cdt_treiber_stack.h>
#include <uflib/slab/inlined_slab.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <stdlib.h>

#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>

#include "sfu_server_priv.h"
#include "sfu_shard_flow.h"
#include "sfu_shard_ice.h"
#include "sfu_shard_room.h"
#include "dtls/sfu_shard_dtls.h"
#include "rtcp/sfu_shard_rtcp.h"
#include "srtp/sfu_shard_srtp.h"

/* Datagram classes the fetch path recognises before anything is dispatched. */
typedef enum SfuDatagramClass {
  SFU_DATAGRAM_STUN,
  SFU_DATAGRAM_DTLS,
  SFU_DATAGRAM_RTP,
  SFU_DATAGRAM_RTCP,
  SFU_DATAGRAM_OTHER
} SfuDatagramClass;

/** Batches a shard will pull before it services commands and wakes again. */
#define SFU_MAX_BATCHES_PER_TURN 4u

static uint64_t
sNowMilliseconds(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000u + (uint64_t)(now.tv_nsec / 1000000);
}

/**
 * Resolve a media datagram's sender. Only a CONNECTED peer with a bound tuple
 * has an entry here: registration and ICE authentication put it there, never an
 * unauthenticated packet. The table value alone is not proof of a live peer, so
 * the slot state is checked too.
 */
static SfuPeer *
sResolvePeer(SfuShard *shard, const struct sockaddr_storage *source)
{
  const SfuFlowKey key = SfuFlowKeyFromSource(shard, source);
  uint32_t         slot = 0;

  if (SfuFlowFind(shard, &key, &slot) && slot < shard->peers.capacity &&
      shard->peers.slots[slot].state == SFU_PEER_STATE_CONNECTED) {
    SfuPeer *peer = &shard->peers.slots[slot];
    peer->last_rx_ms = sNowMilliseconds();
    return peer;
  }
  return NULL;
}

static uint32_t
sReadU32(const uint8_t *buffer)
{
  uint32_t value;
  memcpy(&value, buffer, sizeof(value));
  return ntohl(value);
}

static SfuDatagramClass
sClassify(const uint8_t *datagram, size_t length)
{
  if (length >= 20 && (datagram[0] & 0xC0u) == 0 && sReadU32(datagram + 4) == 0x2112A442u) return SFU_DATAGRAM_STUN;
  if (length >= 13 && datagram[0] >= 20 && datagram[0] <= 23 && datagram[1] == 0xFE) return SFU_DATAGRAM_DTLS;
  if ((datagram[0] & 0xC0u) == 0x80u) {
    const unsigned payload_type = datagram[1] & 0x7Fu;
    /* A minimal RTCP receiver report is 8 bytes; RTP always carries a 12-byte
     * header. Checking the payload type first keeps an 8-byte RR classified. */
    if (length >= 8 && payload_type >= 64 && payload_type <= 95) return SFU_DATAGRAM_RTCP;
    if (length >= 12) return SFU_DATAGRAM_RTP;
  }
  return SFU_DATAGRAM_OTHER;
}

/**
 * Bind the batch's iovecs to packets minted from the pool, then pull one batch
 * with a single recvmmsg. Every buffer is returned to the pool before this
 * returns: nothing owns a packet across turns yet, so a packet can never be
 * lost to an early return.
 *
 * @return datagrams received; 0 when the turn had none to read.
 */
static size_t
sReceiveBatch(SfuShard *shard)
{
  const SfuShardSpec *spec = shard->spec;
  const size_t taken = InlinedSlabTakeBulk(&shard->packets.slab, (void **)shard->packets.recv_packets, spec->recv_batch);
  if (!taken) {
    shard->spec->counters->drops_slab++;
    return 0;
  }

  for (size_t index = 0; index < taken; index++) {
    SfuPacket *packet = shard->packets.recv_packets[index];
    struct iovec *iov = &shard->packets.recv_iov[index];
    struct msghdr *header = &shard->packets.recv_msgs[index].msg_hdr;

    iov->iov_base = packet->data + SFU_PACKET_HEADROOM;
    iov->iov_len = SFU_PACKET_CAPACITY;
    header->msg_name = &shard->packets.recv_peers[index];
    header->msg_namelen = sizeof(shard->packets.recv_peers[index]);
    header->msg_iov = iov;
    header->msg_iovlen = 1;
    header->msg_control = NULL;
    header->msg_controllen = 0;
    header->msg_flags = 0;
    shard->packets.recv_msgs[index].msg_len = 0;
  }

  const int received = recvmmsg(shard->fd, shard->packets.recv_msgs, (unsigned)taken, MSG_DONTWAIT | MSG_TRUNC, NULL);
  if (received <= 0) {
    InlinedSlabPutBulk(&shard->packets.slab, (void **)shard->packets.recv_packets, taken);
    return 0; /* EAGAIN and interrupted reads are both "nothing this turn" */
  }

  SfuCounters *counters = spec->counters;
  counters->rx_batches++;
  for (int index = 0; index < received; index++) {
    const size_t length = shard->packets.recv_msgs[index].msg_len;
    counters->rx_datagrams++;

    /* Classify before the truncation verdict: a datagram the kernel truncated
     * is still a read datagram, so the class counters must add up to
     * rx_datagrams. Only the first bytes matter for the class, and those are
     * always inside the iovec. */
    SfuPacket *packet = shard->packets.recv_packets[index];
    packet->length = (uint16_t)(length > UINT16_MAX ? UINT16_MAX : length);
    const SfuDatagramClass class = sClassify(packet->data + SFU_PACKET_HEADROOM, length);
    switch (class) {
      case SFU_DATAGRAM_STUN: counters->rx_stun++; break;
      case SFU_DATAGRAM_DTLS: counters->rx_dtls++; break;
      case SFU_DATAGRAM_RTP:  counters->rx_rtp++; break;
      case SFU_DATAGRAM_RTCP: counters->rx_rtcp++; break;
      case SFU_DATAGRAM_OTHER:
      default: counters->rx_other++; break;
    }

    if (length > SFU_PACKET_CAPACITY) {
      counters->drops_truncated++;
      continue;
    }

    const struct sockaddr_storage *source = &shard->packets.recv_peers[index];
    const socklen_t                source_length = shard->packets.recv_msgs[index].msg_hdr.msg_namelen;

    if (class == SFU_DATAGRAM_STUN) {
      size_t reply_length = 0;
      if (SfuShardHandleStun(shard, packet, source, source_length, &reply_length) == SFU_ICE_REPLY) {
        if (sendto(shard->fd, packet->data + SFU_PACKET_HEADROOM, reply_length, 0,
                   (const struct sockaddr *)source, source_length) >= 0) {
          counters->tx_datagrams++;
        } else {
          counters->drops_send++;
        }
      }
      continue;
    }

    SfuPeer *peer = sResolvePeer(shard, source);
    if (!peer) {
      counters->peer_misses++;
      counters->drops_no_peer++;
    } else {
      counters->rx_resolved++;
      if (class == SFU_DATAGRAM_DTLS) {
        SfuShardHandleDtls(shard, peer, packet, source, source_length);
      } else if (class == SFU_DATAGRAM_RTCP) {
        (void)SfuShardHandleRtcp(shard, peer, packet);
      } else if (class == SFU_DATAGRAM_RTP) {
        size_t reply_length = 0;
        if (SfuShardHandleSrtp(shard, peer, packet, &reply_length) == SFU_MEDIA_ECHO) {
          if (sendto(shard->fd, packet->data + SFU_PACKET_HEADROOM, reply_length, 0,
                     (const struct sockaddr *)source, source_length) >= 0) {
            counters->tx_datagrams++;
          } else {
            counters->drops_send++;
          }
        }
      }
    }
  }

  InlinedSlabPutBulk(&shard->packets.slab, (void **)shard->packets.recv_packets, taken);
  return (size_t)received;
}

/**
 * One JSON object describing the contract this shard was started with, logged
 * once before the loop. It is the shard's own view (the spec is read-only), so
 * an operator can see exactly what the thread owns without attaching.
 */
static void
sLogShardSpecs(const SfuShard *shard)
{
  const SfuShardSpec *spec = shard->spec;
  size_t              queue_depth = 0;
  for (size_t other = 0; other < spec->shard_count; other++) {
    if (spec->inbound[other]) {
      queue_depth = spec->inbound[other]->queue_sz;
      break;
    }
  }

  char document[512];
  const int length = snprintf(document, sizeof(document),
      "{\"event\":\"sfu_shard_start\",\"pid\":%ld,\"thread\":%lu,\"index\":%zu,\"shards\":%zu,"
      "\"peer_shards\":%zu,\"port\":%u,\"fd\":%d,\"wake_fd\":%d,\"slab_packets\":%u,"
      "\"recv_batch\":%u,\"send_batch\":%u,\"peer_capacity\":%u,\"command_capacity\":%u,"
      "\"queue_depth\":%zu,\"poll_ms\":%u}",
      (long)getpid(), (unsigned long)pthread_self(), spec->index, spec->shard_count,
      spec->shard_count ? spec->shard_count - 1 : 0, spec->port, spec->fd, spec->wake_fd, spec->slab_packets,
      spec->recv_batch, spec->send_batch, spec->peer_capacity, shard->commands.capacity, queue_depth, spec->poll_ms);
  if (length < 0 || (size_t)length >= sizeof(document)) {
    syslog(LOG_ERR, "%s: shard:'%zu' spec document did not fit", __func__, spec->index);
    return;
  }
  syslog(LOG_INFO, "%s", document);
}

/**
 * Room and transport work lands with the room interface. Counting the command
 * keeps the drain honest until then: a delivered command is visible, and one
 * that is dropped is counted where it was dropped.
 */
static void
sApplyCommand(SfuShard *shard, SfuCommandNode *node)
{
  shard->spec->counters->commands_applied++;

  switch (node->command.type) {
    case SFU_COMMAND_REGISTER_PEER: {
      const bool accepted = SfuShardRegisterPeer(shard, &node->command);
      if (node->reply) node->reply->accepted = accepted;
      break;
    }
    case SFU_COMMAND_CREATE_ROOM:
      SfuShardCreateRoom(shard, node->command.room_id);
      break;
    case SFU_COMMAND_JOIN_ROOM:
      SfuShardJoinRoom(shard, node->command.room_id, node->command.ufrag, node->command.ufrag_length);
      break;
    case SFU_COMMAND_ADD_TRACK:
      SfuShardAddTrack(shard, node->command.room_id, node->command.ufrag, node->command.ufrag_length,
                       node->command.ssrc, (SfuTrackKind)node->command.track_kind, node->command.clock_rate);
      break;
    case SFU_COMMAND_SUBSCRIBE:
      SfuShardSubscribe(shard, node->command.room_id, node->command.ufrag, node->command.ufrag_length,
                        node->command.ssrc, node->command.out_ssrc, node->command.seq_offset);
      break;
    case SFU_COMMAND_LEAVE_PEER:
      if (!node->reply || SfuShardPeerByKey(shard, node->command.ufrag, node->command.ufrag_length)) {
        SfuShardLeavePeer(shard, node->command.ufrag, node->command.ufrag_length);
      }
      if (node->reply) node->reply->accepted = true;
      break;
    case SFU_COMMAND_PEER_STATUS:
      if (node->reply) {
        const bool present = SfuShardPeerByKey(shard, node->command.ufrag, node->command.ufrag_length) != NULL;
        BufferDescriptorAppendFormatted(&node->reply->reply, "%s", present ? "true" : "false");
        node->reply->accepted = true;
      }
      break;
    case SFU_COMMAND_APPLY_NEGOTIATED_CONFIG:
    case SFU_COMMAND_APPLY_PEER_CONFIG:
      if (node->reply) {
        BufferDescriptor *ice = node->command.type == SFU_COMMAND_APPLY_NEGOTIATED_CONFIG ? &node->reply->reply : NULL;
        node->reply->config_result = SfuShardApplyPeerConfig(shard, node->reply->config, ice);
      }
      break;
    case SFU_COMMAND_RESTART_ICE:
      if (node->reply) SfuShardRestartIce(shard, &node->command, &node->reply->reply);
      break;
    case SFU_COMMAND_DESCRIBE_ROOM:
      if (node->reply) SfuShardDescribeRoom(shard, node->command.room_id, &node->reply->reply);
      break;
    case SFU_COMMAND_DESCRIBE_SHARD:
      /* The requestor's envelope is the only cross-thread buffer it hands over;
       * it outlives this call because the requestor is waiting on the reply. */
      if (node->reply) DescribeShard(shard, &node->reply->reply);
      break;
    default:
      syslog(LOG_DEBUG, "%s: shard:'%zu', command:'%d' awaiting the room interface",
             __func__, shard->spec->index, (int)node->command.type);
      break;
  }
}

/** Apply one command and complete its reply envelope, if present. */
static void
sDispatchCommand(SfuShard *shard, SfuCommandNode *node)
{
  SfuCommandReply *reply = node->reply;
  const bool synchronous_commit = reply && (node->command.type == SFU_COMMAND_REGISTER_PEER ||
                                             node->command.type == SFU_COMMAND_APPLY_PEER_CONFIG ||
                                             node->command.type == SFU_COMMAND_APPLY_NEGOTIATED_CONFIG);
  if (!synchronous_commit) sApplyCommand(shard, node);
  if (!reply) return;

  pthread_mutex_lock(&reply->mutex);
  if (reply->state == SFU_COMMAND_STATE_ABANDONED) {
    pthread_mutex_unlock(&reply->mutex);
    /* The requestor timed out and will never read this document: the shard is
     * the only owner left, so it releases the envelope it was handed. */
    BufferDescriptorRelease(&reply->reply);
    pthread_cond_destroy(&reply->done);
    pthread_mutex_destroy(&reply->mutex);
    free(reply);
    return;
  }
  /* Commit and publish the result under the same lock. If the
   * request already timed out, the command must leave state untouched. */
  if (synchronous_commit) sApplyCommand(shard, node);
  reply->state = SFU_COMMAND_STATE_DONE;
  pthread_cond_signal(&reply->done);
  pthread_mutex_unlock(&reply->mutex);
}

/**
 * Release peers that have been idle longer than their timeout. Runs on the
 * shard's own thread, at most once a second, so the sweep never disturbs a
 * batch's hot path.
 */
static void
sExpirePeers(SfuShard *shard, uint64_t now_ms)
{
  if (now_ms < shard->next_expiry_ms) return;
  shard->next_expiry_ms = now_ms + 1000u;

  for (size_t slot = 0; slot < shard->spec->peer_capacity; slot++) {
    SfuPeer *peer = &shard->peers.slots[slot];
    if (peer->state == SFU_PEER_STATE_FREE) continue;
    SfuShardDtlsTick(shard, peer, now_ms);
    (void)SfuShardPeerExpire(shard, peer, slot, now_ms);
  }
}

/** Drain the shard's queue and return every node to the pool's free stack. */
static void
sDrainCommands(SfuShard *shard)
{
  struct mpsc_queue_node *node = NULL;
  for (;;) {
    const enum mpsc_queue_poll_result result = mpsc_queue_poll(&shard->commands.queue, &node);
    /* RETRY means a producer is mid-insert: stop, never spin on a media thread. */
    if (result != MPSC_QUEUE_ITEM) return;

    SfuCommandNode *command = (SfuCommandNode *)((char *)node - offsetof(SfuCommandNode, node));
    sDispatchCommand(shard, command);
    command->reply = NULL;
    CDT_TreiberStackPush(&shard->commands.free, shard->commands.pool, sizeof(SfuCommandNode),
                         offsetof(SfuCommandNode, next_free), (uint32_t)(command - shard->commands.pool));
  }
}

void *
SfuShardThread(void *ptr)
{
  SfuShard *shard = ptr;

  prctl(PR_SET_NAME, "ufSfuShard");
  UfsrvCpuAffinityPin(UFSRV_THREAD_WORKER, (int)shard->spec->index);
  sLogShardSpecs(shard);

  while (atomic_load(&shard->up)) {
    /* Drain first, then take the clock: a command applied during the drain
     * (peer registration, consent-touching state) stamps its timestamps after
     * this turn's earlier now, and an expiry sweep comparing against a stale
     * now would see a negative age and release the peer immediately. */
    sDrainCommands(shard);
    const uint64_t now_ms = sNowMilliseconds();
    SfuShardRtcpSweep(shard, now_ms);
    sExpirePeers(shard, now_ms);
    SfuShardSpeakerSweep(shard, now_ms);
    if (!atomic_load(&shard->up)) break;

    struct pollfd channels[] = {
      {.fd = shard->fd, .events = POLLIN},
      {.fd = shard->spec->wake_fd, .events = POLLIN},
    };
    unsigned wait_ms = shard->spec->poll_ms ? shard->spec->poll_ms : 100u;
    if (shard->rooms.members) {
      const uint64_t poll_now = sNowMilliseconds();
      const uint64_t until_speaker = shard->next_speaker_ms > poll_now ? shard->next_speaker_ms - poll_now : 0;
      if (until_speaker < wait_ms) wait_ms = (unsigned)until_speaker;
    }
    const int ready = poll(channels, 2, (int)wait_ms);
    if (ready <= 0) continue;

    if (channels[0].revents & POLLIN) {
      for (unsigned batch = 0; batch < SFU_MAX_BATCHES_PER_TURN; batch++) {
        if (!sReceiveBatch(shard)) break;
      }
    }
    if (channels[1].revents & POLLIN) {
      uint64_t token = 0;
      while (read(shard->spec->wake_fd, &token, sizeof(token)) < 0 && errno == EINTR) {}
      sDrainCommands(shard);
    }
  }

  /* Apply what was already queued when the stop flag was raised. */
  sDrainCommands(shard);
  if (shard->event_egress) {
    for (size_t slot = 0; slot < shard->peers.capacity; ++slot) {
      SfuPeer *peer = &shard->peers.slots[slot];
      if (peer->state != SFU_PEER_STATE_FREE) SfuShardPeerRelease(shard, peer, slot, SFU_DISC_NONE);
    }
  }
  return NULL;
}
