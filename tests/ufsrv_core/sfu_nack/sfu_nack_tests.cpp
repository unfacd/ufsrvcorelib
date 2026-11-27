/**
 * @file sfu_nack_tests.cpp
 * @brief The retention ring over a hand-built shard: grant, wrap, miss, refusal.
 *
 * Copyright (C) 2015-2026  unfacd works
 */

#include "gtest/gtest.h"

#include <cstddef>
#include <cstdint>

extern "C" {
#include <uflib/slab/inlined_slab.h>

#include "model/counters_type.h"
#include "model/shard_type.h"
#include "model/track_type.h"
#include "sfu_shard_nack.h"

bool SfuShardOutboundLookup(const SfuShard *shard, uint32_t out_ssrc, uint32_t *slot, uint8_t *subscription_index)
{
  (void)shard;
  (void)out_ssrc;
  (void)slot;
  (void)subscription_index;
  return false;
}

bool SfuShardSsrcLookup(const SfuShard *shard, uint32_t ssrc, uint32_t *slot)
{
  (void)shard;
  (void)ssrc;
  (void)slot;
  return false;
}
}

namespace {

constexpr size_t kRingCount = 4;
constexpr size_t kPoolPackets = 4096;

/** Bytes a retained packet would hold; only the length matters to the ring. */
void fill_rtp(uint8_t *packet, uint16_t sequence)
{
  packet[0] = 0x80;
  packet[1] = 96;
  packet[2] = static_cast<uint8_t>(sequence >> 8);
  packet[3] = static_cast<uint8_t>(sequence);
  for (size_t index = 4; index < 12; index++) packet[index] = static_cast<uint8_t>(index);
}

struct RingFixture {
  SfuPacket   slabs[kPoolPackets];
  SfuNackRing rings[kRingCount];
  SfuNackSlot entries[kRingCount * SFU_NACK_VIDEO_SLOTS];
  SfuCounters counters{};
  SfuShardSpec spec{};
  SfuShard     shard{};

  explicit RingFixture(size_t pool_packets = kPoolPackets)
  {
    spec.slab = slabs;
    spec.slab_packets = static_cast<unsigned>(pool_packets);
    spec.peer_capacity = static_cast<unsigned>(kRingCount);
    spec.counters = &counters;
    shard.spec = &spec;

    InlinedSlabDescriptor descriptor{};
    descriptor.region = slabs;
    descriptor.region_bytes = pool_packets * sizeof(SfuPacket);
    descriptor.capacity = pool_packets;
    descriptor.slot_size = sizeof(SfuPacket);
    descriptor.alignment = alignof(SfuPacket);
    descriptor.link_offset = offsetof(SfuPacket, link);
    EXPECT_EQ(InlinedSlabInit(&shard.packets.slab, &descriptor), 0);

    shard.nack.rings = rings;
    shard.nack.slots = entries;
    shard.nack.capacity = kRingCount;
    shard.nack.retransmit_per_second = 250;
    shard.nack.free_head = 0;
    for (size_t index = kRingCount; index > 0; index--) {
      rings[index - 1].slots = &entries[(index - 1) * SFU_NACK_VIDEO_SLOTS];
      rings[index - 1].capacity = 0;
      rings[index - 1].stored = 0;
      rings[index - 1].next_free = shard.nack.free_head;
      shard.nack.free_head = static_cast<uint32_t>(index);
    }
  }

  SfuPublisherTrack track(SfuTrackKind kind)
  {
    SfuPublisherTrack publisher{};
    publisher.ssrc = 0x11223344u;
    publisher.kind = static_cast<uint8_t>(kind);
    return publisher;
  }

  void retain(SfuPublisherTrack &publisher, uint16_t sequence)
  {
    uint8_t packet[12];
    fill_rtp(packet, sequence);
    SfuShardNackRetain(&shard, &publisher, packet, sizeof(packet), sequence);
  }
};

} // namespace

TEST(SfuNack, AttachGrantsCapacityByKind)
{
  RingFixture fixture;
  auto video = fixture.track(SFU_TRACK_VIDEO);
  auto audio = fixture.track(SFU_TRACK_AUDIO);

  SfuShardNackAttach(&fixture.shard, &video);
  SfuShardNackAttach(&fixture.shard, &audio);

  ASSERT_NE(video.ring, nullptr);
  ASSERT_NE(audio.ring, nullptr);
  EXPECT_EQ(video.ring->capacity, SFU_NACK_VIDEO_SLOTS);
  EXPECT_EQ(audio.ring->capacity, SFU_NACK_AUDIO_SLOTS);
  EXPECT_NE(video.ring, audio.ring);
}

TEST(SfuNack, RingWrapsAndEvictsWithoutGrowing)
{
  RingFixture fixture;
  auto publisher = fixture.track(SFU_TRACK_VIDEO);
  SfuShardNackAttach(&fixture.shard, &publisher);

  for (uint32_t sequence = 1; sequence <= SFU_NACK_VIDEO_SLOTS + 88u; sequence++)
    fixture.retain(publisher, static_cast<uint16_t>(sequence));

  EXPECT_EQ(publisher.ring->stored, SFU_NACK_VIDEO_SLOTS);
  EXPECT_EQ(fixture.shard.packets.slab.leased, SFU_NACK_VIDEO_SLOTS);
  EXPECT_EQ(fixture.counters.drops_slab, 0u);
}

TEST(SfuNack, EvictedSequenceReadsAsMiss)
{
  RingFixture fixture;
  auto publisher = fixture.track(SFU_TRACK_VIDEO);
  SfuShardNackAttach(&fixture.shard, &publisher);

  for (uint32_t sequence = 1; sequence <= SFU_NACK_VIDEO_SLOTS; sequence++)
    fixture.retain(publisher, static_cast<uint16_t>(sequence));

  const SfuNackSlot *held = &publisher.ring->slots[1u & (publisher.ring->capacity - 1u)];
  ASSERT_EQ(held->sequence, 1u);

  fixture.retain(publisher, static_cast<uint16_t>(SFU_NACK_VIDEO_SLOTS + 1u));
  const SfuNackSlot *replaced = &publisher.ring->slots[1u & (publisher.ring->capacity - 1u)];
  EXPECT_EQ(replaced->sequence, SFU_NACK_VIDEO_SLOTS + 1u);
  EXPECT_NE(replaced->sequence, 1u);
}

TEST(SfuNack, RefusalIsCountedOnceAndLeavesNoRing)
{
  RingFixture fixture(100);
  auto publisher = fixture.track(SFU_TRACK_VIDEO);

  SfuShardNackAttach(&fixture.shard, &publisher);
  EXPECT_EQ(publisher.ring, nullptr);
  EXPECT_TRUE(publisher.ring_denied);
  EXPECT_EQ(fixture.counters.nack_retention_refused, 1u);

  SfuShardNackAttach(&fixture.shard, &publisher);
  EXPECT_EQ(fixture.counters.nack_retention_refused, 1u);

  fixture.retain(publisher, 7u);
  EXPECT_EQ(publisher.ring, nullptr);
  EXPECT_EQ(fixture.shard.packets.slab.leased, 0u);
  EXPECT_EQ(fixture.counters.nack_retention_refused, 1u);
}

TEST(SfuNack, PressureHalvesVideoAndNotAudio)
{
  RingFixture fixture(4000);
  for (size_t taken = 0; taken < 3200; taken++) ASSERT_NE(InlinedSlabTake(&fixture.shard.packets.slab), nullptr);

  auto video = fixture.track(SFU_TRACK_VIDEO);
  auto audio = fixture.track(SFU_TRACK_AUDIO);
  SfuShardNackAttach(&fixture.shard, &video);
  SfuShardNackAttach(&fixture.shard, &audio);

  ASSERT_NE(video.ring, nullptr);
  ASSERT_NE(audio.ring, nullptr);
  EXPECT_EQ(video.ring->capacity, SFU_NACK_PRESSURE_SLOTS);
  EXPECT_EQ(audio.ring->capacity, SFU_NACK_AUDIO_SLOTS);
}

TEST(SfuNack, ReleaseReturnsEverySlotAndTheDescriptor)
{
  RingFixture fixture;
  auto publisher = fixture.track(SFU_TRACK_VIDEO);
  SfuShardNackAttach(&fixture.shard, &publisher);
  for (uint16_t sequence = 1; sequence <= 20u; sequence++) fixture.retain(publisher, sequence);

  ASSERT_EQ(fixture.shard.packets.slab.leased, 20u);
  const uint32_t free_head = fixture.shard.nack.free_head;

  SfuShardNackRelease(&fixture.shard, &publisher);

  EXPECT_EQ(publisher.ring, nullptr);
  EXPECT_EQ(fixture.shard.packets.slab.leased, 0u);
  EXPECT_NE(fixture.shard.nack.free_head, free_head);

  SfuShardNackAttach(&fixture.shard, &publisher);
  EXPECT_NE(publisher.ring, nullptr);
}
