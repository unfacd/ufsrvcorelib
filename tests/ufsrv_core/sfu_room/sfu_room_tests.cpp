/**
 * @file sfu_room_tests.cpp
 * @brief The room index: more rooms than the old per-shard pool could hold, and
 *        the bounded mode refusing beyond rooms.max.
 *
 * Each test onboards one shard on an ephemeral loopback port and drives the
 * public command API, so the path is the one signalling uses: enqueue, the shard
 * thread applies it, then the console description reports the result.
 *
 * Copyright (C) 2015-2026  unfacd works
 */

#include "gtest/gtest.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_server.h>
#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>
}

/* The shard pins its thread through this; the test process needs no affinity. */
extern "C" void UfsrvCpuAffinityPin(UfsrvThreadType, int) {}

namespace {

bool wait_until(const std::function<bool()> &predicate, int timeout_ms = 8000)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return predicate();
}

uint16_t free_udp_port()
{
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return 0;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    close(fd);
    return 0;
  }
  socklen_t length = sizeof(address);
  uint16_t  port = 0;
  if (getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) == 0) port = ntohs(address.sin_port);
  close(fd);
  return port;
}

class SfuRoomTest : public ::testing::Test {
protected:
  void StartServer(unsigned rooms_max)
  {
    SfuServerSpec spec = {};
    spec.router_count = 1;
    spec.bind_host = "127.0.0.1";
    spec.base_port = free_udp_port();
    spec.recv_batch = 8;
    spec.send_batch = 8;
    spec.slab_packets = 128;
    spec.peer_capacity = 8;
    spec.queue_depth = 512;
    spec.command_pool = 512;
    spec.poll_ms = 5;
    spec.rooms_max = rooms_max;

    set_ = SfuShardOnboard(&spec);
    ASSERT_NE(set_, nullptr);
    server_ = SfuServerStart(set_);
    ASSERT_NE(server_, nullptr);
  }

  void TearDown() override
  {
    /* SfuServerStop() takes ownership of the set and releases it. */
    if (server_) {
      SfuServerStop(server_);
      set_ = nullptr;
    }
    if (set_) SfuShardOffboard(set_);
    server_ = nullptr;
    set_ = nullptr;
  }

  std::string describe()
  {
    BufferDescriptor reply;
    BufferDescriptorInit(&reply, 8192);
    SfuServerDescribeShard(server_, 0, &reply);
    const std::string document(reply.data ? reply.data : "");
    BufferDescriptorRelease(&reply);
    return document;
  }

  long counter(const char *name)
  {
    const std::string document = describe();
    const std::string key = std::string("\"") + name + "\":";
    const size_t      at = document.find(key);
    if (at == std::string::npos) return -1;
    return std::stol(document.substr(at + key.size()));
  }

  /* Enqueue in batches so the command queue is never the thing under test. */
  void create_rooms(uint64_t first, uint64_t count)
  {
    constexpr uint64_t kBatch = 32;
    for (uint64_t done = 0; done < count; done += kBatch) {
      const uint64_t remaining = count - done;
      const uint64_t batch = remaining < kBatch ? remaining : kBatch;
      for (uint64_t index = 0; index < batch; index++) {
        ASSERT_TRUE(SfuServerCreateRoom(server_, 0, first + done + index));
      }
      ASSERT_TRUE(wait_until([&] { return counter("rooms") == static_cast<long>(done + batch); }));
    }
  }

  SfuShardSet *set_ = nullptr;
  SfuServer *   server_ = nullptr;
};

TEST_F(SfuRoomTest, TwoHundredRoomsLiveOnOneShard)
{
  StartServer(256);
  create_rooms(1000, 200);

  EXPECT_EQ(counter("rooms"), 200);
  EXPECT_EQ(counter("room_limit"), 256);
  EXPECT_EQ(counter("room_failures"), 0);
}

TEST_F(SfuRoomTest, TheMapRefusesBeyondRoomsMax)
{
  StartServer(96);
  create_rooms(2000, 96);
  ASSERT_EQ(counter("rooms"), 96);

  /* The next commands are accepted by the queue and refused by the map. */
  for (uint64_t index = 0; index < 8; index++) ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 3000 + index));
  ASSERT_TRUE(wait_until([&] { return counter("room_failures") == 8; }));
  EXPECT_EQ(counter("rooms"), 96);
  EXPECT_EQ(counter("room_limit"), 96);
}

TEST_F(SfuRoomTest, JoinKeyframeRequestIsRefusedForAnUnreachablePublisher)
{
  StartServer(16);

  /* Both peers are registered but neither has completed ICE, so the publisher
   * has no nominated tuple and no outbound SRTP session to answer on. */
  const uint8_t fingerprint[32] = {0x5A};
  ASSERT_TRUE(SfuServerRegisterPeer(server_, 0, 1, "regPub", "pwdRegPub", fingerprint));
  ASSERT_TRUE(SfuServerRegisterPeer(server_, 0, 2, "regSub", "pwdRegSub", fingerprint));
  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9500));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));

  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9500, "regPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9500, "regSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; }));

  const uint32_t publisher_ssrc = 0x2B3C4D5Eu;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9500, "regPub", publisher_ssrc, SFU_TRACK_VIDEO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));

  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9500, "regSub", publisher_ssrc, 0x12340001u, 0));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));

  /* The request is attempted and refused: nothing is sent, and the refusal is
   * visible rather than silent. */
  ASSERT_TRUE(wait_until([&] { return counter("pli_on_join") == 1; })) << describe();
  ASSERT_TRUE(wait_until([&] { return counter("pli_dropped") >= 1; })) << describe();
  EXPECT_EQ(counter("pli_forwarded"), 0);
  EXPECT_EQ(counter("pli_suppressed"), 0);
}

} // namespace
