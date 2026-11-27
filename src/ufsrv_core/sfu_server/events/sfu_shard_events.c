#include "events/sfu_shard_events.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_server.h>

#include "events/sfu_event_egress.h"
#include "sfu_peer_config_priv.h"

static void
sSubmit(SfuShard *shard, const SfuRoom *room, const SfuPeer *peer, SfuEventTypeEnum type, uint8_t detail)
{
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now) != 0) return;

  SfuEvent event = {
    .timestamp_ms = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u,
    .type = type,
    .detail = detail,
  };
  event.room_id_len = (uint8_t)snprintf(event.room_id, sizeof(event.room_id), "%" PRIu64, room->room_id);
  if (peer) {
    const char *key = peer->config ? peer->config->ufrag : peer->ufrag;
    const size_t length = strnlen(key, SFU_MAX_ID_LEN + 1);
    if (!length || length > SFU_MAX_ID_LEN) return;
    event.peer_id_len = (uint8_t)length;
    memcpy(event.peer_id, key, length);
  }
  SfuEventEgressTrySubmit(shard->event_egress, &event);
}

void
SfuShardEventPeerConnected(SfuShard *shard, SfuPeer *peer)
{
  if (!shard->event_egress || !peer->room || peer->dtls.state != SFU_DTLS_READY || peer->event_connected) return;
  peer->event_connected = true;
  if (shard->event_settings->emit_peer_connected) {
    sSubmit(shard, peer->room, peer, SFU_EVT_PEER_CONNECTED, 0);
  }
}

void
SfuShardEventPeerDisconnected(SfuShard *shard, SfuPeer *peer, SfuDisconnectReasonEnum reason)
{
  if (!shard->event_egress || !peer->event_connected) return;
  peer->event_connected = false;
  if (peer->room && shard->event_settings->emit_peer_disconnected) {
    sSubmit(shard, peer->room, peer, SFU_EVT_PEER_DISCONNECTED, reason);
  }
}

void
SfuShardEventSpeaker(SfuShard *shard, const SfuRoom *room, const SfuPeer *peer, uint8_t level)
{
  if (shard->event_egress && shard->event_settings->emit_dominant_speaker) {
    sSubmit(shard, room, peer, SFU_EVT_DOMINANT_SPEAKER, level);
  }
}
