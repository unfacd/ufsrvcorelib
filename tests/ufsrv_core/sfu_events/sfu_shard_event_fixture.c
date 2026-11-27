#include <string.h>

#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_server.h>

#include "events/sfu_event_egress.h"
#include "events/sfu_shard_events.h"
#include "sfu_peer_config_priv.h"

void
SfuTestEmitShardTransitions(SfuEventEgress *egress)
{
  SfuEventEgressSpec settings = {
    .emit_peer_disconnected = true,
    .emit_dominant_speaker = true,
  };
  SfuShard shard = {.event_settings = &settings, .event_egress = egress};
  SfuRoom room = {.room_id = 42};
  SfuPeerConfigSnapshot config = {.ufrag = "stable-session"};
  SfuPeer peer = {.room = &room, .config = &config, .ufrag = "remote-ice"};

  SfuShardEventPeerConnected(&shard, &peer); /* no DTLS yet */
  peer.dtls.state = SFU_DTLS_READY;
  SfuShardEventPeerConnected(&shard, &peer); /* connect output disabled */
  SfuShardEventPeerDisconnected(&shard, &peer, SFU_DISC_ICE_TIMEOUT);

  settings.emit_peer_connected = true;
  SfuShardEventPeerConnected(&shard, &peer);
  SfuShardEventPeerConnected(&shard, &peer); /* repeat */
  SfuShardEventSpeaker(&shard, &room, &peer, 18);
  SfuShardEventSpeaker(&shard, &room, NULL, 0);
  SfuShardEventPeerDisconnected(&shard, &peer, SFU_DISC_DTLS_CLOSED);
  SfuShardEventPeerDisconnected(&shard, &peer, SFU_DISC_ADMIN_KICK); /* later release */
  memset(&config, 0, sizeof(config));
}
