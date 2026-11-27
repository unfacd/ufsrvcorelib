#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include "events/sfu_event_egress.h"
#include <ufsrvcorelib/ufsrv_core/sfu_server/events/sfu_disconnect_reason_enum.h>
void SfuTestEmitShardTransitions(SfuEventEgress *egress);
}

namespace {
struct Recorder {
  std::mutex mutex;
  std::condition_variable changed;
  bool first_dequeued = false;
  bool resume = false;
  std::vector<SfuEvent> events;
};

bool Record(void *context, const SfuEvent *event)
{
  auto &recorder = *static_cast<Recorder *>(context);
  std::unique_lock lock(recorder.mutex);
  if (recorder.events.empty()) {
    recorder.first_dequeued = true;
    recorder.changed.notify_one();
    recorder.changed.wait_for(lock, std::chrono::seconds(2), [&] { return recorder.resume; });
  }
  recorder.events.push_back(*event);
  return true;
}
}

TEST(SfuEventEgress, WakesDrainsAndDropsWhenFull)
{
  Recorder recorder;
  const SfuEventEgressAdapter adapter{.submit = Record, .finish = nullptr};
  SfuEventEgress *egress = SfuEventEgressStart(4, &adapter, &recorder);
  ASSERT_NE(egress, nullptr);

  SfuEvent event{};
  event.type = SFU_EVT_PEER_CONNECTED;
  EXPECT_TRUE(SfuEventEgressTrySubmit(egress, &event));
  {
    std::unique_lock lock(recorder.mutex);
    EXPECT_TRUE(recorder.changed.wait_for(lock, std::chrono::seconds(1),
                                         [&] { return recorder.first_dequeued; }));
  }

  std::array<bool, 4> accepted{};
  std::array<std::thread, 4> producers;
  for (size_t i = 0; i < producers.size(); ++i) {
    producers[i] = std::thread([&, i] {
      SfuEvent submitted = event;
      submitted.timestamp_ms = i + 1;
      accepted[i] = SfuEventEgressTrySubmit(egress, &submitted);
      submitted.timestamp_ms = 99;
    });
  }
  for (auto &producer : producers) producer.join();
  for (bool result : accepted) EXPECT_TRUE(result);
  EXPECT_FALSE(SfuEventEgressTrySubmit(egress, &event));

  {
    std::lock_guard lock(recorder.mutex);
    recorder.resume = true;
  }
  recorder.changed.notify_one();
  SfuEventEgressStop(egress);

  ASSERT_EQ(recorder.events.size(), 5u);
  std::array<bool, 5> seen{};
  for (size_t i = 0; i < recorder.events.size(); ++i) {
    const auto &received = recorder.events[i];
    EXPECT_EQ(received.seq, i + 1);
    ASSERT_LT(received.timestamp_ms, seen.size());
    EXPECT_FALSE(seen[received.timestamp_ms]);
    seen[received.timestamp_ms] = true;
  }
}

TEST(SfuEventEgress, ShardTransitionsAndAdapterFinish)
{
  struct Capture {
    std::vector<SfuEvent> records;
    uint64_t deadline = 0;
  } capture;
  const SfuEventEgressAdapter adapter{
    .submit = [](void *context, const SfuEvent *event) {
      static_cast<Capture *>(context)->records.push_back(*event);
      return true;
    },
    .finish = [](void *context, uint64_t deadline) {
      static_cast<Capture *>(context)->deadline = deadline;
    },
  };
  SfuEventEgress *egress = SfuEventEgressStart(8, &adapter, &capture);
  ASSERT_NE(egress, nullptr);
  SfuTestEmitShardTransitions(egress);
  SfuEventEgressStop(egress);

  ASSERT_EQ(capture.records.size(), 5u);
  EXPECT_GT(capture.deadline, 0u);
  const auto &records = capture.records;
  EXPECT_EQ(records[0].type, SFU_EVT_PEER_DISCONNECTED);
  EXPECT_EQ(records[0].detail, SFU_DISC_ICE_TIMEOUT);
  EXPECT_EQ(records[1].type, SFU_EVT_PEER_CONNECTED);
  EXPECT_EQ(records[2].type, SFU_EVT_DOMINANT_SPEAKER);
  EXPECT_EQ(records[2].detail, 18u);
  EXPECT_EQ(records[3].type, SFU_EVT_DOMINANT_SPEAKER);
  EXPECT_EQ(records[3].detail, 0u);
  EXPECT_EQ(records[4].type, SFU_EVT_PEER_DISCONNECTED);
  EXPECT_EQ(records[4].detail, SFU_DISC_DTLS_CLOSED);

  for (size_t i = 0; i < records.size(); ++i) {
    const auto &event = records[i];
    EXPECT_EQ(event.seq, i + 1);
    EXPECT_GT(event.timestamp_ms, 0u);
    EXPECT_EQ(std::string_view(event.room_id, event.room_id_len), "42");
    EXPECT_EQ(std::string_view(event.peer_id, event.peer_id_len), i == 3 ? "" : "stable-session");
  }
}
