/**
 * @file sfu_ice_tests.cpp
 * @brief End-to-end ICE responder tests: real sockets, real codec, real shard.
 *
 * Each test starts one shard on an ephemeral loopback port, provisions a peer
 * through the public registration command, then drives STUN binding requests
 * from one or more client sockets and inspects the shard's own counters. Media
 * datagrams are used as the probe for "which tuple is bound": a resolved RTP
 * datagram increments rx_resolved, an unresolved one increments drops_no_peer.
 *
 * Copyright (C) 2015-2026  unfacd works
 */

#include "gtest/gtest.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <functional>
#include <cstring>
#include <string>
#include <thread>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/sfu_server/command/sfu_console_router.h>
#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_server.h>
#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>
#include <ufsrvcorelib/ufsrv_core/turn/codec/ns_turn_msg.h>
#include <ufsrvcorelib/ufsrv_core/turn/codec/ns_turn_msg_defs.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>
}

/* The shard pins its thread through this; the test process needs no affinity. */
extern "C" void UfsrvCpuAffinityPin(UfsrvThreadType, int) {}

namespace {

constexpr uint16_t kUseCandidate = 0x0025;

void sleep_ms(int milliseconds)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

/* Timing-sensitive tests poll for the shard's own counter instead of sleeping
 * a fixed interval: under valgrind the shard thread runs far behind the test. */
bool wait_until(const std::function<bool()> &predicate, int timeout_ms = 8000)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    sleep_ms(25);
  }
  return predicate();
}

uint16_t free_udp_port(bool v6 = false)
{
  const int family = v6 ? AF_INET6 : AF_INET;
  const int fd = socket(family, SOCK_DGRAM, 0);
  sockaddr_storage storage{};
  socklen_t length;
  if (v6) {
    auto *addr6 = reinterpret_cast<sockaddr_in6 *>(&storage);
    addr6->sin6_family = AF_INET6;
    addr6->sin6_addr = in6addr_loopback;
    addr6->sin6_port = 0;
    length = sizeof(*addr6);
  } else {
    auto *addr4 = reinterpret_cast<sockaddr_in *>(&storage);
    addr4->sin_family = AF_INET;
    addr4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr4->sin_port = 0;
    length = sizeof(*addr4);
  }
  bind(fd, reinterpret_cast<sockaddr *>(&storage), length);
  getsockname(fd, reinterpret_cast<sockaddr *>(&storage), &length);
  close(fd);
  return ntohs(v6 ? reinterpret_cast<sockaddr_in6 *>(&storage)->sin6_port
                  : reinterpret_cast<sockaddr_in *>(&storage)->sin_port);
}

int client_socket(uint16_t port, bool v6 = false)
{
  const int fd = socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
  timeval timeout{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (v6) {
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_loopback;
    addr.sin6_port = htons(port);
    connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    return fd;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  return fd;
}

struct Request {
  uint8_t bytes[1500];
  size_t  length;
};

Request build_binding_request(const std::string &username, const std::string &password, bool use_candidate)
{
  Request request{};
  request.length = 0;
  stun_init_request_str(STUN_METHOD_BINDING, request.bytes, &request.length);

  stun_tid tid;
  stun_tid_generate_in_message_str(request.bytes, &tid);

  stun_attr_add_str(request.bytes, &request.length, STUN_ATTRIBUTE_USERNAME,
                    reinterpret_cast<const uint8_t *>(username.c_str()), static_cast<int>(username.size()));
  if (use_candidate) {
    stun_attr_add_str(request.bytes, &request.length, kUseCandidate, nullptr, 0);
  }

  password_t pwd;
  std::memset(pwd, 0, sizeof(pwd));
  std::strncpy(reinterpret_cast<char *>(pwd), password.c_str(), sizeof(password_t) - 1);
  stun_attr_add_integrity_by_user_short_term_str(request.bytes, &request.length,
                                                 reinterpret_cast<const uint8_t *>(username.c_str()), pwd,
                                                 SHATYPE_SHA1);
  stun_attr_add_fingerprint_str(request.bytes, &request.length);
  return request;
}

/** Send a request and wait for the binding success response; false on timeout. */
bool exchange(int fd, const Request &request, const std::string &password)
{
  if (send(fd, request.bytes, request.length, 0) < 0) return false;

  uint8_t response[1500];
  const ssize_t received = recv(fd, response, sizeof(response), 0);
  if (received <= 0) return false;

  bool fingerprint = false;
  if (!stun_is_command_message_full_check_str(response, static_cast<size_t>(received), true, &fingerprint) ||
      !fingerprint) {
    return false;
  }
  if (!stun_is_success_response_str(response, static_cast<size_t>(received))) return false;
  if (stun_get_method_str(response, static_cast<size_t>(received)) != STUN_METHOD_BINDING) return false;

  stun_tid sent;
  stun_tid got;
  stun_tid_from_message_str(request.bytes, request.length, &sent);
  stun_tid_from_message_str(response, static_cast<size_t>(received), &got);
  if (std::memcmp(sent.tsx_id, got.tsx_id, STUN_TID_SIZE) != 0) return false;

  return stun_check_message_integrity_str(TURN_CREDENTIALS_SHORT_TERM, response, static_cast<size_t>(received),
                                          reinterpret_cast<const uint8_t *>(""), nullptr,
                                          reinterpret_cast<const uint8_t *>(password.c_str()), SHATYPE_SHA1) == 1;
}

void send_rtp(int fd)
{
  uint8_t datagram[60];
  std::memset(datagram, 0, sizeof(datagram));
  datagram[0] = 0x80;
  datagram[1] = 111;
  send(fd, datagram, sizeof(datagram), 0);
}

class SfuIceTest : public ::testing::Test {
protected:
  void StartServer(unsigned consent_seconds = 5, unsigned idle_seconds = 5, const char *bind_host = "127.0.0.1")
  {
    port_ = free_udp_port(bind_host[0] == ':');
    SfuServerSpec spec = {};
    spec.router_count = 1;
    spec.bind_host = bind_host;
    spec.base_port = port_;
    spec.recv_batch = 8;
    spec.send_batch = 8;
    spec.slab_packets = 256;
    spec.peer_capacity = 8;
    spec.queue_depth = 64;
    spec.poll_ms = 5;
    spec.command_pool = 32;
    spec.peers_idle_timeout_seconds = idle_seconds;
    spec.ice_consent_timeout_seconds = consent_seconds;

    set_ = SfuShardOnboard(&spec);
    ASSERT_NE(set_, nullptr);
    server_ = SfuServerStart(set_);
    ASSERT_NE(server_, nullptr);
    ice_pwd_ = transport_field("ice_pwd");
    ASSERT_EQ(ice_pwd_.size(), 24u);
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
    BufferDescriptorInit(&reply, 4096);
    SfuServerDescribeShard(server_, 0, &reply);
    const std::string document(reply.data ? reply.data : "");
    BufferDescriptorRelease(&reply);
    return document;
  }

  /* The transport document is what signalling turns into an SDP answer; the
   * tests read the shard's own credentials from it, which is also the only
   * source a real client would have. */
  std::string transport()
  {
    BufferDescriptor reply;
    BufferDescriptorInit(&reply, 1024);
    SfuServerDescribeTransport(server_, 0, &reply);
    const std::string document(reply.data ? reply.data : "");
    BufferDescriptorRelease(&reply);
    return document;
  }

  std::string transport_field(const char *name)
  {
    const std::string document = transport();
    const std::string key = std::string("\"") + name + "\":\"";
    const size_t      at = document.find(key);
    if (at == std::string::npos) return {};
    const size_t begin = at + key.size();
    const size_t end = document.find('"', begin);
    return document.substr(begin, end - begin);
  }

  /* RFC 8445: the requester puts the receiver's fragment first. */
  std::string sfu_username(const char *peer) { return transport_field("ice_ufrag") + ":" + std::string(peer); }

  long counter(const char *name)
  {
    const std::string document = describe();
    const std::string key = std::string("\"") + name + "\":";
    const size_t      at = document.find(key);
    if (at == std::string::npos) return -1;
    return std::stol(document.substr(at + key.size()));
  }

  bool register_peer(const char *ufrag, const char *password, uint64_t id = 1)
  {
    return SfuServerRegisterPeer(server_, 0, id, ufrag, password, nullptr);
  }

  uint16_t port_ = 0;
  std::string ice_pwd_;
  SfuShardSet *set_ = nullptr;
  SfuServer *   server_ = nullptr;
};

TEST_F(SfuIceTest, NominationIsTheOnlyWayToConnected)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragA", "passwordA"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  const int fd = client_socket(port_);
  ASSERT_GE(fd, 0);

  /* An authenticated check without USE-CANDIDATE is answered but not nominated. */
  EXPECT_TRUE(exchange(fd, build_binding_request(sfu_username("ufragA"), ice_pwd_, false), ice_pwd_))
      << describe();
  ASSERT_TRUE(wait_until([&] { return counter("ice_authenticated") >= 1; }));
  EXPECT_EQ(counter("ice_nominated"), 0);

  EXPECT_TRUE(exchange(fd, build_binding_request(sfu_username("ufragA"), ice_pwd_, true), ice_pwd_));
  ASSERT_TRUE(wait_until([&] { return counter("ice_nominated") >= 1; }));
  EXPECT_EQ(counter("peer_count"), 1);
  close(fd);
}

TEST_F(SfuIceTest, RetransmittedNominationGetsAnotherBindingResponse)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragRetry", "passwordRetry"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  const int fd = client_socket(port_);
  ASSERT_GE(fd, 0);
  const Request request = build_binding_request(sfu_username("ufragRetry"), ice_pwd_, true);

  // Discard the first response and retry with the same transaction ID.
  ASSERT_EQ(send(fd, request.bytes, request.length, 0), static_cast<ssize_t>(request.length));
  uint8_t discarded[1500];
  ASSERT_GT(recv(fd, discarded, sizeof(discarded), 0), 0);

  EXPECT_TRUE(exchange(fd, request, ice_pwd_));
  EXPECT_EQ(counter("ice_nominated"), 1);
  EXPECT_EQ(counter("ice_replay_dropped"), 0);
  close(fd);
}

TEST_F(SfuIceTest, ReplayedRequestDoesNotMoveTheTuple)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragB", "passwordB"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  const int alice = client_socket(port_);
  const int mallory = client_socket(port_);
  ASSERT_GE(alice, 0);
  ASSERT_GE(mallory, 0);

  ASSERT_TRUE(exchange(alice, build_binding_request(sfu_username("ufragB"), ice_pwd_, true), ice_pwd_));
  ASSERT_TRUE(wait_until([&] { return counter("ice_nominated") == 1; }));

  const long resolved_before = counter("rx_resolved");
  const long missed_before = counter("drops_no_peer");

  /* Capture one fresh authenticated request, let it land, then replay the exact
   * bytes from another source. Authentication passes; the replay guard must
   * still refuse to touch consent or the tuple. */
  const Request fresh = build_binding_request(sfu_username("ufragB"), ice_pwd_, false);
  ASSERT_TRUE(exchange(alice, fresh, ice_pwd_));
  sleep_ms(100);

  send(mallory, fresh.bytes, fresh.length, 0); /* no response expected */
  ASSERT_TRUE(wait_until([&] { return counter("ice_replay_dropped") >= 1; }));

  send_rtp(mallory);
  send_rtp(alice);
  sleep_ms(150);

  EXPECT_EQ(counter("drops_no_peer") - missed_before, 1); /* mallory's media unresolved */
  EXPECT_EQ(counter("rx_resolved") - resolved_before, 1); /* alice's still resolves */
  close(alice);
  close(mallory);
}

TEST_F(SfuIceTest, ChecksOnAnotherTupleDoNotChangeTheSelectedPair)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragC", "passwordC"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));
  const int old_path = client_socket(port_);
  const int new_path = client_socket(port_);
  ASSERT_GE(old_path, 0);
  ASSERT_GE(new_path, 0);

  ASSERT_TRUE(exchange(old_path, build_binding_request(sfu_username("ufragC"), ice_pwd_, true), ice_pwd_));
  for (unsigned attempt = 0; attempt < 4; attempt++) {
    ASSERT_TRUE(exchange(new_path, build_binding_request(sfu_username("ufragC"), ice_pwd_, false), ice_pwd_));
  }
  ASSERT_TRUE(exchange(new_path, build_binding_request(sfu_username("ufragC"), ice_pwd_, true), ice_pwd_));
  EXPECT_EQ(counter("ice_nominated"), 1);

  const long misses = counter("drops_no_peer");
  send_rtp(new_path);
  ASSERT_TRUE(wait_until([&] { return counter("drops_no_peer") > misses; }));
  const long resolved = counter("rx_resolved");
  send_rtp(old_path);
  ASSERT_TRUE(wait_until([&] { return counter("rx_resolved") > resolved; }));
  close(old_path);
  close(new_path);
}

TEST_F(SfuIceTest, RestartKeepsTheOldPathUntilNewCredentialsAreNominated)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragD", "passwordD"));
  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 441));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 441, "ufragD"));
  ASSERT_TRUE(wait_until([&] { return counter("room_members") == 1; }));
  const int old_path = client_socket(port_);
  const int new_path = client_socket(port_);
  ASSERT_GE(old_path, 0);
  ASSERT_GE(new_path, 0);

  const std::string old_username = sfu_username("ufragD");
  ASSERT_TRUE(exchange(old_path, build_binding_request(old_username, ice_pwd_, true), ice_pwd_));

  const auto restart = [&] {
    BufferDescriptor reply;
    BufferDescriptorInit(&reply, 256);
    SfuServerRestartIce(server_, 0, "ufragD", "nextUfrag", "nextPassword012345678901234", &reply);
    const std::string document(reply.data ? reply.data : "");
    BufferDescriptorRelease(&reply);
    return document;
  };
  const auto document = restart();
  const auto field = [&](const std::string &name) {
    const std::string key = "\"" + name + "\":\"";
    const auto at = document.find(key);
    if (at == std::string::npos) return std::string{};
    const auto begin = at + key.size();
    return document.substr(begin, document.find('"', begin) - begin);
  };
  const std::string next_ufrag = field("ice_ufrag");
  const std::string next_password = field("ice_pwd");
  ASSERT_FALSE(next_ufrag.empty()) << document;
  ASSERT_FALSE(next_password.empty()) << document;
  EXPECT_NE(next_ufrag, transport_field("ice_ufrag"));
  EXPECT_NE(next_password, ice_pwd_);
  EXPECT_EQ(restart(), document);

  const std::string next_username = next_ufrag + ":nextUfrag";
  ASSERT_TRUE(exchange(new_path, build_binding_request(next_username, next_password, false), next_password));
  EXPECT_EQ(counter("ice_nominated"), 1);
  const long resolved = counter("rx_resolved");
  send_rtp(old_path);
  ASSERT_TRUE(wait_until([&] { return counter("rx_resolved") > resolved; }));

  const auto nomination = build_binding_request(next_username, next_password, true);
  ASSERT_TRUE(exchange(new_path, nomination, next_password));
  EXPECT_TRUE(exchange(new_path, nomination, next_password));
  EXPECT_EQ(counter("ice_nominated"), 2);
  EXPECT_EQ(counter("room_members"), 1);
  EXPECT_EQ(counter("flow_size"), 1);
  EXPECT_EQ(restart(), document);

  const long new_resolved = counter("rx_resolved");
  send_rtp(new_path);
  ASSERT_TRUE(wait_until([&] { return counter("rx_resolved") > new_resolved; }));
  const long misses = counter("drops_no_peer");
  send_rtp(old_path);
  ASSERT_TRUE(wait_until([&] { return counter("drops_no_peer") > misses; }));
  EXPECT_FALSE(exchange(old_path, build_binding_request(old_username, ice_pwd_, true), ice_pwd_));

  EXPECT_TRUE(SfuServerLeavePeer(server_, 0, "ufragD"));
  EXPECT_TRUE(wait_until([&] { return counter("peer_count") == 0; }));
  close(old_path);
  close(new_path);
}

TEST_F(SfuIceTest, UnknownUfragIsSilent)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragE", "passwordE"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  const int fd = client_socket(port_);
  ASSERT_GE(fd, 0);
  EXPECT_FALSE(exchange(fd, build_binding_request(sfu_username("nosuch"), ice_pwd_, true), ice_pwd_));
  ASSERT_TRUE(wait_until([&] { return counter("ice_unknown_ufrag") >= 1; }));
  close(fd);
}

TEST_F(SfuIceTest, ExpiredPeerReleasesItsUfrag)
{
  StartServer(2, 1);
  ASSERT_TRUE(register_peer("ufragF", "passwordF"));
  sleep_ms(100);
  ASSERT_EQ(counter("ufrag_count"), 1);

  /* idle timeout is one second for an un-nominated peer */
  ASSERT_TRUE(wait_until([&] { return counter("peer_count") == 0; }, 10000));
  EXPECT_EQ(counter("ufrag_count"), 0);
  EXPECT_GE(counter("peers_expired"), 1);
}

TEST_F(SfuIceTest, RegistrationCapacityIsBounded)
{
  StartServer();
  for (int index = 0; index < 10; index++) {
    char ufrag[16];
    char password[16];
    std::snprintf(ufrag, sizeof(ufrag), "ufrag%02d", index);
    std::snprintf(password, sizeof(password), "pass%02d", index);
    EXPECT_EQ(register_peer(ufrag, password, static_cast<uint64_t>(index + 1)), index < 8);
  }
  ASSERT_TRUE(wait_until([&] { return counter("peer_count") == 8; }));
  EXPECT_EQ(counter("peers_admitted"), 8);
  EXPECT_GE(counter("peers_dropped"), 2);
  EXPECT_EQ(counter("ufrag_count"), 8);
}

TEST_F(SfuIceTest, ConsentExpiryReleasesAConnectedPeer)
{
  StartServer(1, 5);
  ASSERT_TRUE(register_peer("ufragG", "passwordG"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  const int fd = client_socket(port_);
  ASSERT_GE(fd, 0);
  ASSERT_TRUE(exchange(fd, build_binding_request(sfu_username("ufragG"), ice_pwd_, true), ice_pwd_));
  ASSERT_TRUE(wait_until([&] { return counter("ice_nominated") == 1; }));

  /* Consent is one second here: silence must release the peer and its ufrag. */
  ASSERT_TRUE(wait_until([&] { return counter("peer_count") == 0; }, 8000));
  EXPECT_GE(counter("ice_consent_expired"), 1);
  EXPECT_EQ(counter("ufrag_count"), 0);
  close(fd);
}

TEST_F(SfuIceTest, V6BindingRequestResolvesAndNominates)
{
  StartServer(5, 5, "::");
  ASSERT_TRUE(register_peer("ufragH", "passwordH"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  const int fd = client_socket(port_, true);
  ASSERT_GE(fd, 0);
  EXPECT_TRUE(exchange(fd, build_binding_request(sfu_username("ufragH"), ice_pwd_, true), ice_pwd_));
  ASSERT_TRUE(wait_until([&] { return counter("ice_nominated") == 1; }));
  EXPECT_EQ(counter("peer_count"), 1);
  EXPECT_EQ(counter("flow_size"), 1);
  close(fd);
}


TEST_F(SfuIceTest, RoomLifecycleRegistersMembershipTrackAndRelease)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragR", "passwordR"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 4242));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));

  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 4242, "ufragR"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 1; }));

  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 4242, "ufragR", 0x11223344u, SFU_TRACK_AUDIO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  EXPECT_EQ(counter("room_members"), 1);
  EXPECT_EQ(counter("ssrc_count"), 1);

  ASSERT_TRUE(SfuServerLeavePeer(server_, 0, "ufragR"));
  ASSERT_TRUE(wait_until([&] { return counter("peers_released") == 1; }));
  EXPECT_EQ(counter("peer_count"), 0);
  EXPECT_EQ(counter("ufrag_count"), 0);
  EXPECT_EQ(counter("rooms"), 0);
  EXPECT_EQ(counter("room_members"), 0);
  EXPECT_EQ(counter("ssrc_count"), 0);
}

TEST_F(SfuIceTest, JoinRoomRejectsAnUnregisteredPeer)
{
  StartServer();
  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 4343));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));

  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 4343, "nobody"));
  ASSERT_TRUE(wait_until([&] { return counter("room_join_failures") == 1; }));
  EXPECT_EQ(counter("room_members"), 0);
}

TEST_F(SfuIceTest, TransportDocumentCarriesCandidateCredentialsAndFingerprint)
{
  StartServer();

  const std::string document = transport();
  EXPECT_NE(document.find("\"shard\":0"), std::string::npos) << document;
  EXPECT_NE(document.find(std::string("\"address\":\"127.0.0.1\"")), std::string::npos) << document;
  EXPECT_NE(document.find("\"port\":" + std::to_string(port_)), std::string::npos) << document;

  /* RFC 8445 sizes: at least 4 and 22 characters; we generate 8 and 24. */
  EXPECT_EQ(transport_field("ice_ufrag").size(), 8u);
  EXPECT_EQ(transport_field("ice_pwd").size(), 24u);

  /* 32 bytes as lowercase colon-separated hex, the SDP a=fingerprint form. */
  const std::string fingerprint = transport_field("dtls_fingerprint");
  EXPECT_EQ(fingerprint.size(), 95u);
  size_t colons = 0;
  for (size_t index = 0; index < fingerprint.size(); index++) {
    if (index % 3u == 2u) EXPECT_EQ(fingerprint[index], ':');
    else EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(fingerprint[index])));
  }
  for (char digit : fingerprint) colons += digit == ':' ? 1u : 0u;
  EXPECT_EQ(colons, 31u);

  /* The credentials are per shard and never the ones a peer registered. */
  ASSERT_TRUE(register_peer("ufragT", "passwordT"));
  EXPECT_NE(transport_field("ice_pwd"), "passwordT");
}

TEST_F(SfuIceTest, ConsoleRouteServesTheTransportDocument)
{
  StartServer();
  /* The host attaches the router; the server stops it. */
  ASSERT_EQ(SfuCommandConsoleAttachResourceRouter(server_), 0);

  BufferDescriptor reply;
  BufferDescriptorInit(&reply, 1024);
  SfuCommandConsoleRouteResource(server_, "GET", "transport/0", &reply);
  const std::string document(reply.data ? reply.data : "");
  BufferDescriptorRelease(&reply);

  /* The host mounts /server and forwards the remainder here, so this is the
   * exact document signalling reads in production. */
  EXPECT_NE(document.find("\"ice_ufrag\""), std::string::npos) << document;
  EXPECT_NE(document.find("\"dtls_fingerprint\""), std::string::npos) << document;
  EXPECT_EQ(document.find("\"error\""), std::string::npos) << document;

  BufferDescriptorInit(&reply, 256);
  SfuCommandConsoleRouteResource(server_, "GET", "transport/9", &reply);
  const std::string missing(reply.data ? reply.data : "");
  BufferDescriptorRelease(&reply);
  EXPECT_NE(missing.find("unknown shard"), std::string::npos) << missing;
}

TEST_F(SfuIceTest, RequestForAnotherTransportUfragIsRejected)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragM", "passwordM"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  const int fd = client_socket(port_);
  ASSERT_GE(fd, 0);
  /* Signed with the right password, but addressed to a transport this shard
   * does not serve: no response, and the mismatch is counted. */
  EXPECT_FALSE(exchange(fd, build_binding_request("00000000:ufragM", ice_pwd_, true), ice_pwd_));
  ASSERT_TRUE(wait_until([&] { return counter("ice_ufrag_mismatch") >= 1; }));
  EXPECT_EQ(counter("ice_authenticated"), 0);
  close(fd);
}

TEST_F(SfuIceTest, RequestSignedWithTheWrongPasswordIsRejected)
{
  StartServer();
  ASSERT_TRUE(register_peer("ufragP", "passwordP"));
  ASSERT_TRUE(wait_until([&] { return counter("ufrag_count") == 1; }));

  const int fd = client_socket(port_);
  ASSERT_GE(fd, 0);
  /* The peer's registered password is not the shard's: a request signed with it
   * fails, which is the whole point of owning distinct credentials. */
  EXPECT_FALSE(exchange(fd, build_binding_request(sfu_username("ufragP"), "passwordP", true), ice_pwd_));
  ASSERT_TRUE(wait_until([&] { return counter("ice_auth_failures") >= 1; }));
  EXPECT_EQ(counter("ice_nominated"), 0);
  close(fd);
}

}  // namespace
