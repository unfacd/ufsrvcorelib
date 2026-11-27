#ifndef UFSRVCORELIB_SFU_SHARD_EVENTS_H
#define UFSRVCORELIB_SFU_SHARD_EVENTS_H

#include <ufsrvcorelib/ufsrv_core/sfu_server/events/sfu_disconnect_reason_enum.h>

#include "model/shard_type.h"

void SfuShardEventPeerConnected(SfuShard *shard, SfuPeer *peer);
void SfuShardEventPeerDisconnected(SfuShard *shard, SfuPeer *peer, SfuDisconnectReasonEnum reason);
void SfuShardEventSpeaker(SfuShard *shard, const SfuRoom *room, const SfuPeer *peer, uint8_t level);

#endif
