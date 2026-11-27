/**
 * @file sfu_subscription_tests.cpp
 * @brief The outbound-SSRC index against a hand-built shard.
 *
 * The index is what RTCP feedback resolves against, so its invariants are
 * checked without a socket or a shard thread: one binding per SSRC, no
 * duplicates across subscribers, peer removal, the 75 % load ceiling, and an
 * allocator that refuses to hand out an SSRC that is already live either as an
 * outbound binding or as a publisher's.
 *
 * Copyright (C) 2015-2026  unfacd works
 */

#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>

#include "model/shard_type.h"
#include "sfu_shard_room.h"
#include "sfu_shard_subscription.h"
#include "sfu_shard_track.h"
}

/* The engine pins its thread through this; this test never starts one. */
extern "C" void UfsrvCpuAffinityPin(UfsrvThreadType, int) {}

namespace {

class SfuSubscriptionTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    std::memset(&shard, 0, sizeof shard);
    SfuShardSubscriptionsInit(&shard, entries, kEntries, 0x9E3779B97F4A7C15ull);
    shard.ssrc_index = ssrc_index;
    shard.ssrc_mask = kSsrcEntries - 1u;
    shard.ssrc_count = 0;
  }

  /* Publish one SSRC on @p peer_slot so the allocator has something to avoid. */
  void publish(uint32_t ssrc, uint32_t peer_slot) { ASSERT_TRUE(SfuShardSsrcInsert(&shard, ssrc, peer_slot)); }

  enum { kEntries = 128, kSsrcEntries = 64 };

  SfuShard          shard{};
  SfuOutboundEntry  entries[kEntries]{};
  SfuSsrcEntry      ssrc_index[kSsrcEntries]{};
};

TEST_F(SfuSubscriptionTest, BindsAndResolvesOneSsrc)
{
  ASSERT_TRUE(SfuShardOutboundInsert(&shard, 0x11110001u, 3u, 2u));
  uint32_t slot = 0;
  uint8_t  subscription = 0;
  EXPECT_TRUE(SfuShardOutboundLookup(&shard, 0x11110001u, &slot, &subscription));
  EXPECT_EQ(slot, 3u);
  EXPECT_EQ(subscription, 2u);
  EXPECT_EQ(shard.subscriptions.count, 1u);

  EXPECT_FALSE(SfuShardOutboundLookup(&shard, 0x11110002u, &slot, &subscription));
  EXPECT_TRUE(SfuShardOutboundRemove(&shard, 0x11110001u));
  EXPECT_FALSE(SfuShardOutboundRemove(&shard, 0x11110001u));
  EXPECT_EQ(shard.subscriptions.count, 0u);
}

TEST_F(SfuSubscriptionTest, RebindIsIdempotentAndConflictsAreRejected)
{
  ASSERT_TRUE(SfuShardOutboundInsert(&shard, 0x22220001u, 1u, 0u));
  EXPECT_TRUE(SfuShardOutboundInsert(&shard, 0x22220001u, 1u, 0u)); /* same pair */
  EXPECT_EQ(shard.subscriptions.count, 1u);
  EXPECT_FALSE(SfuShardOutboundInsert(&shard, 0x22220001u, 2u, 0u)); /* another peer */
  EXPECT_FALSE(SfuShardOutboundInsert(&shard, 0x22220001u, 1u, 1u)); /* another slot */
  EXPECT_FALSE(SfuShardOutboundInsert(&shard, 0u, 1u, 0u));         /* zero is not an SSRC */
}

TEST_F(SfuSubscriptionTest, RemovingAPeerRemovesOnlyItsBindings)
{
  ASSERT_TRUE(SfuShardOutboundInsert(&shard, 0x33330001u, 4u, 0u));
  ASSERT_TRUE(SfuShardOutboundInsert(&shard, 0x33330002u, 4u, 1u));
  ASSERT_TRUE(SfuShardOutboundInsert(&shard, 0x33330003u, 5u, 0u));

  SfuShardOutboundRemovePeer(&shard, 4u);
  EXPECT_FALSE(SfuShardOutboundLookup(&shard, 0x33330001u, nullptr, nullptr));
  EXPECT_FALSE(SfuShardOutboundLookup(&shard, 0x33330002u, nullptr, nullptr));
  EXPECT_TRUE(SfuShardOutboundLookup(&shard, 0x33330003u, nullptr, nullptr));
  EXPECT_EQ(shard.subscriptions.count, 1u);
}

TEST_F(SfuSubscriptionTest, AllocatorSkipsLiveAndPublishedSsrcs)
{
  publish(0x44440001u, 0u);

  const uint32_t first = SfuShardOutboundAllocate(&shard);
  ASSERT_NE(first, 0u);
  ASSERT_TRUE(SfuShardOutboundInsert(&shard, first, 0u, 0u));

  const uint32_t second = SfuShardOutboundAllocate(&shard);
  ASSERT_NE(second, 0u);
  EXPECT_NE(second, first);
  EXPECT_NE(second, 0x44440001u);
  EXPECT_FALSE(SfuShardOutboundLookup(&shard, second, nullptr, nullptr));

  /* A published SSRC must never be handed out, however many draws it takes. */
  for (int draw = 0; draw < 256; draw++) {
    const uint32_t candidate = SfuShardOutboundAllocate(&shard);
    ASSERT_NE(candidate, 0u);
    EXPECT_NE(candidate, 0x44440001u);
  }
}

TEST_F(SfuSubscriptionTest, RefusesAtTheLoadCeiling)
{
  size_t bound = 0;
  for (uint32_t ssrc = 1; ssrc < 4096; ssrc++) {
    if (!SfuShardOutboundInsert(&shard, 0x55550000u + ssrc, (uint32_t)(ssrc % 8u), 0u)) break;
    bound++;
  }
  EXPECT_EQ(bound, kEntries * 3u / 4u); /* the same 75 % ceiling as the publisher index */
  EXPECT_EQ(shard.subscriptions.count, bound);
}

TEST(SfuTrackStatsTest, CountsPacketsAndExtendsTheSequence)
{
  SfuPublisherTrack track{};
  track.clock_rate = 48000;
  SfuTrackOnRtp(&track, 100, 0, 1000000);
  SfuTrackOnRtp(&track, 101, 960, 21000000);
  SfuTrackOnRtp(&track, 102, 1920, 41000000);
  EXPECT_EQ(track.packets_received, 3u);
  EXPECT_EQ(track.max_seq_ext, 102u);
  EXPECT_EQ(track.base_seq, 100u);
  EXPECT_EQ(track.first_rtp_ns, 1000000u);
}

TEST(SfuTrackStatsTest, ExtendsThroughTheSequenceWrap)
{
  SfuPublisherTrack track{};
  track.clock_rate = 90000;
  SfuTrackOnRtp(&track, 65534, 0, 1000000);
  SfuTrackOnRtp(&track, 65535, 3000, 34000000);
  SfuTrackOnRtp(&track, 0, 6000, 67000000);
  SfuTrackOnRtp(&track, 1, 9000, 100000000);
  EXPECT_EQ(track.packets_received, 4u);
  EXPECT_EQ(track.max_seq_ext, 65537u);
  EXPECT_EQ(track.cycles, 65536u);
}

TEST(SfuTrackStatsTest, EvenSpacingProducesNoJitter)
{
  SfuPublisherTrack track{};
  track.clock_rate = 48000;
  for (uint32_t index = 0; index < 8; index++) {
    SfuTrackOnRtp(&track, (uint16_t)(1000 + index), index * 960u, (uint64_t)index * 20000000ull);
  }
  EXPECT_EQ(track.jitter_q4, 0u);
}

TEST(SfuTrackStatsTest, UnevenArrivalRaisesTheJitterEstimate)
{
  SfuPublisherTrack track{};
  track.clock_rate = 48000;
  SfuTrackOnRtp(&track, 1000, 0, 0);
  SfuTrackOnRtp(&track, 1001, 960, 10000000);  /* 10 ms early */
  SfuTrackOnRtp(&track, 1002, 1920, 40000000); /* 30 ms late */
  SfuTrackOnRtp(&track, 1003, 2880, 50000000); /* 10 ms early */
  EXPECT_GT(track.jitter_q4, 0u);
}

TEST(SfuTrackStatsTest, ClockRateDefaultsFollowTheKind)
{
  EXPECT_EQ(SfuTrackClockRate(SFU_TRACK_AUDIO), 48000u);
  EXPECT_EQ(SfuTrackClockRate(SFU_TRACK_VIDEO), 90000u);
}

} // namespace
