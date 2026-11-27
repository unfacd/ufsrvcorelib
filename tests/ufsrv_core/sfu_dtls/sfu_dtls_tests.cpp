/**
 * @file sfu_dtls_tests.cpp
 * @brief End-to-end DTLS handshake against a real shard over UDP.
 *
 * A peer is provisioned with its certificate fingerprint, ICE-nominated over a
 * real socket, and then a real OpenSSL DTLS client (datagram BIO, not memory
 * BIOs — the client side is not under test) drives the handshake the shard runs
 * over a memory BIO pair. The assertions are the shard's own counters plus the
 * negotiated SRTP profile on the client.
 *
 * Copyright (C) 2015-2026  unfacd works
 */

#include "gtest/gtest.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <thread>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/srtp.h>
#include <openssl/x509.h>
#include <srtp3/srtp.h>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_server.h>
#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>
#include <ufsrvcorelib/ufsrv_core/turn/codec/ns_turn_msg.h>
#include <ufsrvcorelib/ufsrv_core/turn/codec/ns_turn_msg_defs.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include "srtp/sfu_shard_srtp.h"
}

extern "C" void UfsrvCpuAffinityPin(UfsrvThreadType, int) {}

namespace {

constexpr uint16_t kUseCandidate = 0x0025;

void sleep_ms(int milliseconds)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

uint16_t free_udp_port()
{
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t length = sizeof(addr);
  bind(fd, reinterpret_cast<sockaddr *>(&addr), length);
  getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &length);
  close(fd);
  return ntohs(addr.sin_port);
}

int client_socket(uint16_t port)
{
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  timeval timeout{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
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

Request build_binding_request(const std::string &username, const std::string &password)
{
  Request request{};
  stun_init_request_str(STUN_METHOD_BINDING, request.bytes, &request.length);

  stun_tid tid;
  stun_tid_generate_in_message_str(request.bytes, &tid);

  stun_attr_add_str(request.bytes, &request.length, STUN_ATTRIBUTE_USERNAME,
                    reinterpret_cast<const uint8_t *>(username.c_str()), static_cast<int>(username.size()));
  stun_attr_add_str(request.bytes, &request.length, kUseCandidate, nullptr, 0);

  password_t pwd;
  std::memset(pwd, 0, sizeof(pwd));
  std::strncpy(reinterpret_cast<char *>(pwd), password.c_str(), sizeof(password_t) - 1);
  stun_attr_add_integrity_by_user_short_term_str(request.bytes, &request.length,
                                                 reinterpret_cast<const uint8_t *>(username.c_str()), pwd,
                                                 SHATYPE_SHA1);
  stun_attr_add_fingerprint_str(request.bytes, &request.length);
  return request;
}

bool ice_exchange(int fd, const Request &request, const std::string &password)
{
  if (send(fd, request.bytes, request.length, 0) < 0) return false;
  uint8_t response[1500];
  const ssize_t received = recv(fd, response, sizeof(response), 0);
  if (received <= 0) return false;
  return stun_is_success_response_str(response, static_cast<size_t>(received)) &&
         stun_check_message_integrity_str(TURN_CREDENTIALS_SHORT_TERM, response, static_cast<size_t>(received),
                                          reinterpret_cast<const uint8_t *>(""), nullptr,
                                          reinterpret_cast<const uint8_t *>(password.c_str()), SHATYPE_SHA1) == 1;
}

/** A throwaway client identity plus the SHA-256 of its DER certificate. */
struct ClientIdentity {
  EVP_PKEY *key = nullptr;
  X509 *    certificate = nullptr;
  uint8_t   fingerprint[32] = {0};

  bool create()
  {
    key = EVP_EC_gen("P-256");
    if (!key) return false;
    certificate = X509_new();
    if (!certificate) return false;
    X509_set_version(certificate, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), 2);
    X509_gmtime_adj(X509_getm_notBefore(certificate), 0);
    X509_gmtime_adj(X509_getm_notAfter(certificate), 3600);
    X509_set_pubkey(certificate, key);
    X509_NAME *name = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("client"), -1, -1, 0);
    X509_set_issuer_name(certificate, name);
    if (!X509_sign(certificate, key, EVP_sha256())) return false;

    unsigned char *der = nullptr;
    const int      length = i2d_X509(certificate, &der);
    if (length <= 0) return false;
    unsigned int  digest_length = 0;
    const bool    ok = EVP_Digest(der, static_cast<size_t>(length), fingerprint, &digest_length, EVP_sha256(), NULL) == 1;
    OPENSSL_free(der);
    return ok && digest_length == sizeof(fingerprint);
  }

  ~ClientIdentity()
  {
    if (certificate) X509_free(certificate);
    if (key) EVP_PKEY_free(key);
  }
};


srtp_t make_session(srtp_ssrc_type_t type, const uint8_t *key, const uint8_t *salt, size_t salt_length)
{
  srtp_policy_t policy = nullptr;
  if (srtp_policy_create(&policy) != srtp_err_status_ok) return nullptr;
  const srtp_ssrc_t ssrc = {.type = type, .value = 0};
  srtp_t            session = nullptr;
  const bool        ok = srtp_policy_set_ssrc(policy, ssrc) == srtp_err_status_ok &&
                  srtp_policy_set_profile(policy, srtp_profile_aes128_cm_sha1_80) == srtp_err_status_ok &&
                  srtp_policy_add_key(policy, key, 16, salt, salt_length, nullptr, 0) == srtp_err_status_ok &&
                  srtp_policy_validate(policy) == srtp_err_status_ok &&
                  srtp_create(&session, policy) == srtp_err_status_ok;
  srtp_policy_destroy(policy);
  return ok ? session : nullptr;
}


/** A registered, nominated, DTLS-established and SRTP-ready test peer. */
struct PeerLink {
  int      fd = -1;
  SSL *    ssl = nullptr;
  srtp_t   in = nullptr;
  srtp_t   out = nullptr;
  uint16_t profile = 0; ///< negotiated SRTP protection profile id

  ~PeerLink()
  {
    if (in) srtp_dealloc(in);
    if (out) srtp_dealloc(out);
    if (ssl) SSL_free(ssl);
    if (fd >= 0) close(fd);
  }
};

class SfuDtlsTest : public ::testing::Test {
protected:
  void StartServer(unsigned consent_seconds = 10, unsigned idle_seconds = 30, unsigned rtcp_seconds = 0,
                   unsigned slab_packets = 1024)
  {
    port_ = free_udp_port();
    SfuServerSpec spec = {};
    spec.router_count = 1;
    spec.bind_host = "127.0.0.1";
    spec.base_port = port_;
    spec.recv_batch = 8;
    spec.send_batch = 8;
    spec.slab_packets = slab_packets;
    spec.peer_capacity = 8;
    spec.queue_depth = 64;
    spec.poll_ms = 5;
    spec.command_pool = 32;
    spec.peers_idle_timeout_seconds = idle_seconds;
    spec.ice_consent_timeout_seconds = consent_seconds;
    spec.rtcp_interval_seconds = rtcp_seconds;
    spec.nack_retransmit_per_second = nack_retransmit_per_second_;

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

  /* The shard's own credentials: a browser signs its checks with these, so the
   * test client reads them from the same transport document signalling would. */
  std::string transport_field(const char *name)
  {
    BufferDescriptor reply;
    BufferDescriptorInit(&reply, 1024);
    SfuServerDescribeTransport(server_, 0, &reply);
    const std::string document(reply.data ? reply.data : "");
    BufferDescriptorRelease(&reply);

    const std::string key = std::string("\"") + name + "\":\"";
    const size_t      at = document.find(key);
    if (at == std::string::npos) return {};
    const size_t begin = at + key.size();
    const size_t end = document.find('"', begin);
    return document.substr(begin, end - begin);
  }

  long counter(const char *name)
  {
    const std::string document = describe();
    const std::string key = std::string("\"") + name + "\":";
    const size_t      at = document.find(key);
    if (at == std::string::npos) return -1;
    return std::stol(document.substr(at + key.size()));
  }

  bool wait_until(const std::function<bool()> &predicate, int timeout_ms = 8000)
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (predicate()) return true;
      sleep_ms(25);
    }
    return predicate();
  }

  /** Run the STUN half and return a socket now bound to the peer's tuple. */
  int nominate(const char *ufrag, const char *password, const uint8_t *fingerprint)
  {
    if (!SfuServerRegisterPeer(server_, 0, 1, ufrag, password, fingerprint)) {
      fprintf(stderr, "[attach] %s: register rejected\n", ufrag);
      return -1;
    }
    if (!wait_until([&] { return counter("ufrag_count") >= 1; })) {
      fprintf(stderr, "[attach] %s: ufrag never appeared (count=%ld)\n", ufrag, counter("ufrag_count"));
      return -1;
    }

    const int fd = client_socket(port_);
    if (fd < 0) return -1;
    /* Registration is asynchronous: retry the check until the shard has
     * applied it, rather than racing the command queue. */
    const std::string shard_ufrag = transport_field("ice_ufrag");
    const std::string shard_pwd = transport_field("ice_pwd");
    bool exchanged = false;
    for (int attempt = 0; attempt < 10 && !exchanged; attempt++) {
      exchanged = ice_exchange(fd, build_binding_request(shard_ufrag + ":" + std::string(ufrag), shard_pwd), shard_pwd);
      if (!exchanged) sleep_ms(100);
    }
    if (!exchanged) {
      fprintf(stderr, "[attach] %s: no ICE response (ufrag_count=%ld peers_dropped=%ld commands_dropped=%ld unknown=%ld)\n",
              ufrag, counter("ufrag_count"), counter("peers_dropped"), counter("commands_dropped"),
              counter("ice_unknown_ufrag"));
      close(fd);
      return -1;
    }
    if (!wait_until([&] { return counter("ice_nominated") >= 1; })) {
      fprintf(stderr, "[attach] %s: not nominated (nominated=%ld)\n", ufrag, counter("ice_nominated"));
      close(fd);
      return -1;
    }
    return fd;
  }

  bool attach_peer(PeerLink *link, const char *ufrag, const char *password, ClientIdentity *identity)
  {
    link->fd = nominate(ufrag, password, identity->fingerprint);
    if (link->fd < 0) {
      fprintf(stderr, "[attach] %s: nomination failed\n", ufrag);
      return false;
    }

    SSL_CTX *context = SSL_CTX_new(DTLS_client_method());
    if (!context) return false;
    SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(context, DTLS1_2_VERSION);
    SSL_CTX_set_tlsext_use_srtp(context, "SRTP_AES128_CM_SHA1_80");
    if (SSL_CTX_use_certificate(context, identity->certificate) != 1 ||
        SSL_CTX_use_PrivateKey(context, identity->key) != 1) {
      SSL_CTX_free(context);
      return false;
    }
    SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);

    SSL *ssl = SSL_new(context);
    if (!ssl) {
      SSL_CTX_free(context);
      return false;
    }
    BIO *bio = BIO_new_dgram(link->fd, BIO_NOCLOSE);
    if (!bio) {
      SSL_free(ssl);
      SSL_CTX_free(context);
      return false;
    }
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    peer.sin_port = htons(port_);
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peer);
    SSL_set_bio(ssl, bio, bio);
    SSL_set_connect_state(ssl);
    if (SSL_connect(ssl) != 1) {
      unsigned long error = ERR_get_error();
      char          text[256];
      ERR_error_string_n(error, text, sizeof(text));
      fprintf(stderr, "[attach] %s: handshake failed: %s\n", ufrag, text);
      SSL_free(ssl);
      SSL_CTX_free(context);
      return false;
    }

    uint8_t material[60];
    const bool exported =
        SSL_export_keying_material(ssl, material, sizeof(material), "EXTRACTOR-dtls_srtp", 19, nullptr, 0, 0) == 1;
    link->ssl = ssl;
    if (!exported) {
      SSL_free(ssl);
      link->ssl = nullptr;
      SSL_CTX_free(context);
      return false;
    }
    const SRTP_PROTECTION_PROFILE *negotiated = SSL_get_selected_srtp_profile(ssl);
    link->profile = negotiated ? negotiated->id : 0;
    link->in = make_session(ssrc_any_inbound, material + 16, material + 46, 14);
    link->out = make_session(ssrc_any_outbound, material, material + 32, 14);
    SSL_CTX_free(context); /* the SSL keeps its own reference */
    return link->in && link->out;
  }

  unsigned     nack_retransmit_per_second_ = 250;
  uint16_t     port_ = 0;
  SfuShardSet *set_ = nullptr;
  SfuServer *   server_ = nullptr;
};

TEST_F(SfuDtlsTest, ClientHandshakeCompletesAndNegotiatesSrtp)
{
  StartServer();
  ClientIdentity identity;
  ASSERT_TRUE(identity.create());

  const int fd = nominate("dtlsA", "passwordA", identity.fingerprint);
  ASSERT_GE(fd, 0);

  SSL_CTX *context = SSL_CTX_new(DTLS_client_method());
  ASSERT_NE(context, nullptr);
  SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(context, DTLS1_2_VERSION);
  SSL_CTX_set_tlsext_use_srtp(context, "SRTP_AES128_CM_SHA1_80");
  ASSERT_EQ(SSL_CTX_use_certificate(context, identity.certificate), 1);
  ASSERT_EQ(SSL_CTX_use_PrivateKey(context, identity.key), 1);
  SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);

  SSL *ssl = SSL_new(context);
  ASSERT_NE(ssl, nullptr);
  BIO *bio = BIO_new_dgram(fd, BIO_NOCLOSE);
  ASSERT_NE(bio, nullptr);
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer.sin_port = htons(port_);
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peer);
  SSL_set_bio(ssl, bio, bio);
  SSL_set_mtu(ssl, 1200);
  SSL_set_connect_state(ssl);

  const int result = SSL_connect(ssl);
  if (result != 1) {
    unsigned long error = ERR_get_error();
    char          text[256];
    ERR_error_string_n(error, text, sizeof(text));
    ADD_FAILURE() << "SSL_connect failed: " << text << " ssl_error=" << SSL_get_error(ssl, result)
                  << " errno=" << errno << " counters: " << describe();
  }
  const SRTP_PROTECTION_PROFILE *profile = SSL_get_selected_srtp_profile(ssl);
  ASSERT_NE(profile, nullptr);
  EXPECT_EQ(profile->id == SRTP_AES128_CM_SHA1_80 || profile->id == SRTP_AEAD_AES_128_GCM, true);

  uint8_t key_material[60];
  EXPECT_EQ(SSL_export_keying_material(ssl, key_material, sizeof(key_material), "EXTRACTOR-dtls_srtp",
                                       std::strlen("EXTRACTOR-dtls_srtp"), nullptr, 0, 0),
            1);

  ASSERT_TRUE(wait_until([&] { return counter("dtls_handshakes_completed") == 1; }));
  EXPECT_EQ(counter("dtls_sessions"), 1);
  EXPECT_EQ(counter("dtls_fingerprint_mismatches"), 0);

  EXPECT_GE(SSL_shutdown(ssl), 0);
  EXPECT_TRUE(wait_until([&] { return counter("dtls_sessions") == 0; }));

  SSL_free(ssl);
  SSL_CTX_free(context);
  close(fd);
}

TEST_F(SfuDtlsTest, FingerprintMismatchIsRejected)
{
  StartServer();
  ClientIdentity identity;
  ASSERT_TRUE(identity.create());

  uint8_t wrong[32];
  std::memcpy(wrong, identity.fingerprint, sizeof(wrong));
  wrong[0] ^= 0xFF;

  const int fd = nominate("dtlsB", "passwordB", wrong);
  ASSERT_GE(fd, 0);

  SSL_CTX *context = SSL_CTX_new(DTLS_client_method());
  ASSERT_NE(context, nullptr);
  SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(context, DTLS1_2_VERSION);
  SSL_CTX_set_tlsext_use_srtp(context, "SRTP_AES128_CM_SHA1_80");
  ASSERT_EQ(SSL_CTX_use_certificate(context, identity.certificate), 1);
  ASSERT_EQ(SSL_CTX_use_PrivateKey(context, identity.key), 1);
  SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);

  SSL *ssl = SSL_new(context);
  ASSERT_NE(ssl, nullptr);
  BIO *bio = BIO_new_dgram(fd, BIO_NOCLOSE);
  ASSERT_NE(bio, nullptr);
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer.sin_port = htons(port_);
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peer);
  SSL_set_bio(ssl, bio, bio);
  SSL_set_mtu(ssl, 1200);
  SSL_set_connect_state(ssl);

  (void)SSL_connect(ssl); /* may complete at the TLS level before the server reacts */

  ASSERT_TRUE(wait_until([&] { return counter("dtls_fingerprint_mismatches") >= 1; }));
  EXPECT_EQ(counter("dtls_sessions"), 0);

  SSL_free(ssl);
  SSL_CTX_free(context);
  close(fd);
}


TEST_F(SfuDtlsTest, EchoedRtpRoundTripsThroughBothSrtpSessions)
{
  StartServer();
  ClientIdentity identity;
  ASSERT_TRUE(identity.create());

  const int fd = nominate("dtlsC", "passwordC", identity.fingerprint);
  ASSERT_GE(fd, 0);

  SSL_CTX *context = SSL_CTX_new(DTLS_client_method());
  ASSERT_NE(context, nullptr);
  SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(context, DTLS1_2_VERSION);
  SSL_CTX_set_tlsext_use_srtp(context, "SRTP_AES128_CM_SHA1_80");
  ASSERT_EQ(SSL_CTX_use_certificate(context, identity.certificate), 1);
  ASSERT_EQ(SSL_CTX_use_PrivateKey(context, identity.key), 1);
  SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);

  SSL *ssl = SSL_new(context);
  ASSERT_NE(ssl, nullptr);
  BIO *bio = BIO_new_dgram(fd, BIO_NOCLOSE);
  ASSERT_NE(bio, nullptr);
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer.sin_port = htons(port_);
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peer);
  SSL_set_bio(ssl, bio, bio);
  SSL_set_connect_state(ssl);
  ASSERT_EQ(SSL_connect(ssl), 1);

  /* RFC 5764 layout: client key | server key | client salt | server salt. */
  uint8_t material[60];
  ASSERT_EQ(SSL_export_keying_material(ssl, material, sizeof(material), "EXTRACTOR-dtls_srtp",
                                       std::strlen("EXTRACTOR-dtls_srtp"), nullptr, 0, 0),
            1);
  srtp_t outbound = make_session(ssrc_any_outbound, material, material + 32, 14);
  srtp_t inbound = make_session(ssrc_any_inbound, material + 16, material + 46, 14);
  ASSERT_NE(outbound, nullptr);
  ASSERT_NE(inbound, nullptr);

  uint8_t plain[12 + 60];
  std::memset(plain, 0, sizeof(plain));
  plain[0] = 0x80;
  plain[1] = 111;
  plain[2] = 0x12;
  plain[3] = 0x34;
  for (size_t index = 12; index < sizeof(plain); index++) plain[index] = static_cast<uint8_t>(index);

  uint8_t protected_packet[12 + 60 + 64];
  size_t  protected_length = sizeof(protected_packet);
  ASSERT_EQ(srtp_protect(outbound, plain, sizeof(plain), protected_packet, &protected_length, 0),
            srtp_err_status_ok);
  ASSERT_GT(protected_length, sizeof(plain));
  ASSERT_EQ(send(fd, protected_packet, protected_length, 0), static_cast<ssize_t>(protected_length));

  ASSERT_TRUE(wait_until([&] { return counter("srtp_unprotect_ok") >= 1; })) << describe();
  ASSERT_TRUE(wait_until([&] { return counter("media_echoed") >= 1; })) << describe();
  EXPECT_EQ(counter("srtp_sessions"), 1);
  EXPECT_EQ(counter("srtp_unprotect_failures"), 0);

  uint8_t echoed[12 + 60 + 64];
  const ssize_t received = recv(fd, echoed, sizeof(echoed), 0);
  ASSERT_GT(received, 0);
  uint8_t recovered[12 + 60 + 64];
  size_t  recovered_length = sizeof(recovered);
  ASSERT_EQ(srtp_unprotect(inbound, echoed, static_cast<size_t>(received), recovered, &recovered_length),
            srtp_err_status_ok);
  ASSERT_EQ(recovered_length, sizeof(plain));
  EXPECT_EQ(std::memcmp(recovered, plain, sizeof(plain)), 0);

  srtp_dealloc(inbound);
  srtp_dealloc(outbound);
  SSL_free(ssl);
  SSL_CTX_free(context);
  close(fd);
}


TEST_F(SfuDtlsTest, LeaveReleasesDtlsAndSrtpState)
{
  StartServer();
  ClientIdentity identity;
  ASSERT_TRUE(identity.create());

  const int fd = nominate("dtlsD", "passwordD", identity.fingerprint);
  ASSERT_GE(fd, 0);

  SSL_CTX *context = SSL_CTX_new(DTLS_client_method());
  ASSERT_NE(context, nullptr);
  SSL_CTX_set_min_proto_version(context, DTLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(context, DTLS1_2_VERSION);
  SSL_CTX_set_tlsext_use_srtp(context, "SRTP_AES128_CM_SHA1_80");
  ASSERT_EQ(SSL_CTX_use_certificate(context, identity.certificate), 1);
  ASSERT_EQ(SSL_CTX_use_PrivateKey(context, identity.key), 1);
  SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);

  SSL *ssl = SSL_new(context);
  ASSERT_NE(ssl, nullptr);
  BIO *bio = BIO_new_dgram(fd, BIO_NOCLOSE);
  ASSERT_NE(bio, nullptr);
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer.sin_port = htons(port_);
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peer);
  SSL_set_bio(ssl, bio, bio);
  SSL_set_connect_state(ssl);
  ASSERT_EQ(SSL_connect(ssl), 1);

  ASSERT_TRUE(wait_until([&] { return counter("srtp_sessions") == 1; }));
  ASSERT_TRUE(SfuServerLeavePeer(server_, 0, "dtlsD"));
  ASSERT_TRUE(wait_until([&] { return counter("peers_released") == 1; }));
  EXPECT_EQ(counter("peer_count"), 0);
  EXPECT_EQ(counter("ufrag_count"), 0);
  EXPECT_EQ(counter("dtls_sessions"), 0);
  EXPECT_EQ(counter("srtp_sessions"), 0);

  SSL_free(ssl);
  SSL_CTX_free(context);
  close(fd);
}


TEST_F(SfuDtlsTest, RoomFanOutRewritesSsrcAndSequence)
{
  StartServer(10, 30);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "fanPub", "pwdPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "fanSub", "pwdSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9001));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9001, "fanPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9001, "fanSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; })) << describe();

  const uint32_t publisher_ssrc = 0x11111111u;
  const uint32_t subscriber_ssrc = 0x22222222u;
  const uint16_t seq_offset = 0x1000u;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9001, "fanPub", publisher_ssrc, SFU_TRACK_AUDIO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9001, "fanSub", publisher_ssrc, subscriber_ssrc, seq_offset));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));

  uint8_t plain[12 + 40];
  std::memset(plain, 0, sizeof(plain));
  plain[0] = 0x80;
  plain[1] = 111;
  for (size_t index = 12; index < sizeof(plain); index++) plain[index] = static_cast<uint8_t>(index);

  for (uint16_t sequence = 1; sequence <= 3; sequence++) {
    plain[2] = static_cast<uint8_t>(sequence >> 8); /* RTP numbers are network order */
    plain[3] = static_cast<uint8_t>(sequence);
    const uint32_t wire_publisher_ssrc = htonl(publisher_ssrc);
    std::memcpy(plain + 8, &wire_publisher_ssrc, sizeof(wire_publisher_ssrc));

    uint8_t protected_packet[12 + 40 + 64];
    size_t  protected_length = sizeof(protected_packet);
    ASSERT_EQ(srtp_protect(publisher.out, plain, sizeof(plain), protected_packet, &protected_length, 0),
              srtp_err_status_ok);
    ASSERT_EQ(send(publisher.fd, protected_packet, protected_length, 0), static_cast<ssize_t>(protected_length));

    uint8_t received[12 + 40 + 64];
    const ssize_t received_length = recv(subscriber.fd, received, sizeof(received), 0);
    ASSERT_GT(received_length, 0);

    uint8_t rewritten[12 + 40 + 64];
    size_t  rewritten_length = sizeof(rewritten);
    ASSERT_EQ(srtp_unprotect(subscriber.in, received, static_cast<size_t>(received_length), rewritten,
                             &rewritten_length),
              srtp_err_status_ok);
    ASSERT_EQ(rewritten_length, sizeof(plain));

    uint32_t forwarded_ssrc = 0;
    std::memcpy(&forwarded_ssrc, rewritten + 8, sizeof(forwarded_ssrc));
    forwarded_ssrc = ntohl(forwarded_ssrc);
    const uint16_t forwarded_sequence = static_cast<uint16_t>((rewritten[2] << 8) | rewritten[3]);
    EXPECT_EQ(forwarded_ssrc, subscriber_ssrc);
    EXPECT_EQ(forwarded_sequence, static_cast<uint16_t>(sequence + seq_offset));
    EXPECT_EQ(std::memcmp(rewritten + 12, plain + 12, sizeof(plain) - 12), 0);
  }

  ASSERT_TRUE(wait_until([&] { return counter("media_fanned_out") >= 3; }));
  EXPECT_EQ(counter("fanout_skipped"), 0);
}

TEST_F(SfuDtlsTest, RtcpIsAuthenticatedThenParsed)
{
  StartServer(10, 30);
  ClientIdentity identity;
  ASSERT_TRUE(identity.create());
  PeerLink peer;
  ASSERT_TRUE(attach_peer(&peer, "rtcpA", "pwdRtcp", &identity));
  ASSERT_TRUE(wait_until([&] { return counter("srtp_sessions") == 1; }));

  /* A sender report as a muxed peer sends it: PT 200 on the wire is 72. */
  uint8_t report[28] = {0};
  report[0] = 0x80;
  report[1] = 200;
  report[3] = 6; /* (28 / 4) - 1 */
  const uint32_t ssrc = 0x0BADCAFEu;
  const uint32_t wire_ssrc = htonl(ssrc);
  std::memcpy(report + 4, &wire_ssrc, sizeof(wire_ssrc));

  /* Plaintext RTCP after the handshake is not a parse failure, it is an auth
   * failure: the shard must not read it. */
  ASSERT_EQ(send(peer.fd, report, sizeof(report), 0), static_cast<ssize_t>(sizeof(report)));
  ASSERT_TRUE(wait_until([&] { return counter("rtcp_auth_fail") >= 1; })) << describe();
  EXPECT_EQ(counter("sr_received"), 0);

  /* The same report protected with the client-write keys is parsed and the SR
   * is cached against the publisher track. */
  uint8_t protected_rtcp[sizeof(report) + 64];
  size_t  protected_length = sizeof(protected_rtcp);
  ASSERT_EQ(srtp_protect_rtcp(peer.out, report, sizeof(report), protected_rtcp, &protected_length, 0),
            srtp_err_status_ok);
  ASSERT_EQ(send(peer.fd, protected_rtcp, protected_length, 0), static_cast<ssize_t>(protected_length));
  ASSERT_TRUE(wait_until([&] { return counter("sr_received") >= 1; })) << describe();
  EXPECT_GE(counter("rtcp_packets_parsed"), 1);
  EXPECT_EQ(counter("rtcp_malformed"), 0);
  EXPECT_GE(counter("rx_rtcp"), 2);

  /* The same SRTCP index again is a replay, not a second report: the policy's
   * window is what makes the two distinguishable. */
  ASSERT_EQ(send(peer.fd, protected_rtcp, protected_length, 0), static_cast<ssize_t>(protected_length));
  ASSERT_TRUE(wait_until([&] { return counter("rtcp_replay") >= 1; })) << describe();
  EXPECT_EQ(counter("sr_received"), 1);
}

TEST_F(SfuDtlsTest, SrtcpTrailerAndCapacityFollowTheProfile)
{
  EXPECT_EQ(SfuSrtcpTrailerLength(0x0001), 14u); /* AES-CM-80: index + 10-byte tag */
  EXPECT_EQ(SfuSrtcpTrailerLength(0x0007), 20u); /* AEAD GCM: index + 16-byte tag */
  EXPECT_EQ(SfuSrtcpTrailerLength(0x0002), 0u);  /* unsupported */

  StartServer(10, 30);
  ClientIdentity identity;
  ASSERT_TRUE(identity.create());
  PeerLink peer;
  ASSERT_TRUE(attach_peer(&peer, "rtcpB", "pwdRtcpB", &identity));
  ASSERT_TRUE(wait_until([&] { return counter("srtp_sessions") == 1; }));

  uint8_t report[28] = {0};
  report[0] = 0x80;
  report[1] = 200;
  report[3] = 6;
  const uint16_t profile = peer.profile;
  const size_t   trailer = SfuSrtcpTrailerLength(profile);
  ASSERT_GT(trailer, 0u);

  uint8_t buffer[28 + 32] = {0};
  std::memcpy(buffer, report, sizeof(report));

  size_t length = sizeof(report);
  EXPECT_FALSE(SfuSrtcpProtect(peer.out, profile, buffer, sizeof(report) + trailer - 1, &length));
  EXPECT_EQ(length, sizeof(report)) << "a refused protect must not change the length";

  ASSERT_TRUE(SfuSrtcpProtect(peer.out, profile, buffer, sizeof(buffer), &length));
  EXPECT_EQ(length, sizeof(report) + trailer);
}

TEST_F(SfuDtlsTest, SenderReportDescribesTheOutboundStream)
{
  StartServer(10, 30);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "srPub", "pwdSrPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "srSub", "pwdSrSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9002));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9002, "srPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9002, "srSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; })) << describe();

  const uint32_t publisher_ssrc = 0x0A0B0C0Du;
  const uint32_t subscriber_ssrc = 0x0E0F1011u;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9002, "srPub", publisher_ssrc, SFU_TRACK_AUDIO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9002, "srSub", publisher_ssrc, subscriber_ssrc, 0));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));

  /* One CSRC, an extension with one data word, 20 payload octets, and padding. */
  uint8_t rtp[12 + 4 + 8 + 20 + 4] = {0};
  rtp[0] = 0xB1;
  rtp[1] = 111;
  rtp[3] = 1;
  const uint32_t rtp_ssrc = htonl(publisher_ssrc);
  std::memcpy(rtp + 8, &rtp_ssrc, sizeof(rtp_ssrc));
  rtp[16] = 0xBE;
  rtp[17] = 0xDE;
  rtp[19] = 1;
  rtp[sizeof(rtp) - 1] = 4;
  uint8_t protected_rtp[sizeof(rtp) + 64];
  size_t rtp_length = sizeof(protected_rtp);
  ASSERT_EQ(srtp_protect(publisher.out, rtp, sizeof(rtp), protected_rtp, &rtp_length, 0), srtp_err_status_ok);
  ASSERT_EQ(send(publisher.fd, protected_rtp, rtp_length, 0), static_cast<ssize_t>(rtp_length));
  ASSERT_TRUE(wait_until([&] { return counter("media_fanned_out") == 1; })) << describe();
  uint8_t forwarded_rtp[192];
  ASSERT_GT(recv(subscriber.fd, forwarded_rtp, sizeof(forwarded_rtp), 0), 0);

  /* A sender report with one report block and unrelated upstream totals. */
  uint8_t report[52] = {0};
  report[0] = 0x81; /* V=2, RC=1 */
  report[1] = 200;  /* SR, the standard RTCP payload type */
  report[3] = 12;   /* (52 / 4) - 1 */
  const uint32_t wire_publisher_ssrc = htonl(publisher_ssrc);
  std::memcpy(report + 4, &wire_publisher_ssrc, sizeof(wire_publisher_ssrc));
  const uint32_t ntp_seconds = 0x01020304u;
  const uint32_t ntp_fraction = 0x05060708u;
  const uint32_t rtp_timestamp = 0x00001000u;
  std::memcpy(report + 8, &ntp_seconds, sizeof(ntp_seconds));
  std::memcpy(report + 12, &ntp_fraction, sizeof(ntp_fraction));
  const uint32_t wire_timestamp = htonl(rtp_timestamp);
  std::memcpy(report + 16, &wire_timestamp, sizeof(wire_timestamp));
  report[23] = 99;
  report[27] = 88;

  uint8_t protected_rtcp[sizeof(report) + 64];
  size_t  protected_length = sizeof(protected_rtcp);
  ASSERT_EQ(srtp_protect_rtcp(publisher.out, report, sizeof(report), protected_rtcp, &protected_length, 0),
            srtp_err_status_ok);
  ASSERT_EQ(send(publisher.fd, protected_rtcp, protected_length, 0), static_cast<ssize_t>(protected_length));
  ASSERT_TRUE(wait_until([&] { return counter("sr_relayed") >= 1; })) << describe();

  timeval timeout{2, 0};
  setsockopt(subscriber.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  uint8_t relayed[192];
  const ssize_t received = recv(subscriber.fd, relayed, sizeof(relayed), 0);
  ASSERT_GT(received, 0);

  uint8_t plain[192];
  size_t  plain_length = sizeof(plain);
  ASSERT_EQ(srtp_unprotect_rtcp(subscriber.in, relayed, static_cast<size_t>(received), plain, &plain_length),
            srtp_err_status_ok);
  ASSERT_GE(plain_length, 38u) << "the compound must include the SDES chunk and CNAME header";
  EXPECT_EQ(plain[0] & 0x1fu, 0u) << "RC must be zero after stripping";
  EXPECT_EQ(plain[1], 200u);

  uint32_t forwarded_ssrc = 0;
  uint32_t forwarded_timestamp = 0;
  std::memcpy(&forwarded_ssrc, plain + 4, sizeof(forwarded_ssrc));
  forwarded_ssrc = ntohl(forwarded_ssrc);
  std::memcpy(&forwarded_timestamp, plain + 16, sizeof(forwarded_timestamp));
  forwarded_timestamp = ntohl(forwarded_timestamp);
  EXPECT_EQ(forwarded_ssrc, subscriber_ssrc);
  EXPECT_EQ(forwarded_timestamp, rtp_timestamp) << "ts_offset is zero in this stage";
  EXPECT_EQ(std::memcmp(plain + 8, report + 8, 8), 0) << "NTP is the publisher's capture clock";
  EXPECT_EQ(plain[28], 0x81) << "an SDES chunk follows the relayed report";
  EXPECT_EQ(plain[29], 202u);
  uint32_t packet_count = 0;
  uint32_t octet_count = 0;
  uint32_t sdes_ssrc = 0;
  std::memcpy(&packet_count, plain + 20, sizeof(packet_count));
  std::memcpy(&octet_count, plain + 24, sizeof(octet_count));
  std::memcpy(&sdes_ssrc, plain + 32, sizeof(sdes_ssrc));
  EXPECT_EQ(ntohl(packet_count), 1u);
  EXPECT_EQ(ntohl(octet_count), 20u);
  EXPECT_EQ(ntohl(sdes_ssrc), subscriber_ssrc);
  EXPECT_EQ(plain[36], 1u); /* CNAME item */
  EXPECT_GT(plain[37], 0u);
  EXPECT_GE(counter("sr_without_sdes"), 1);
}

TEST_F(SfuDtlsTest, ReceiverReportIsSentForAPublishedTrack)
{
  StartServer(10, 30, 1); /* one-second RR cadence, so the test is not slow */
  ClientIdentity identity;
  ASSERT_TRUE(identity.create());
  PeerLink publisher;
  ASSERT_TRUE(attach_peer(&publisher, "rrPub", "pwdRrPub", &identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9100));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9100, "rrPub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 1; }));

  const uint32_t publisher_ssrc = 0x0C0D0E0Fu;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9100, "rrPub", publisher_ssrc, SFU_TRACK_VIDEO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));

  uint8_t plain[12 + 20] = {0};
  plain[0] = 0x80;
  plain[1] = 96;
  const uint16_t sequence = 7;
  plain[2] = 0;
  plain[3] = sequence; /* network order, as an endpoint sends it */
  const uint32_t wire_publisher_ssrc = htonl(publisher_ssrc);
  std::memcpy(plain + 8, &wire_publisher_ssrc, sizeof(wire_publisher_ssrc));

  uint8_t protected_packet[sizeof(plain) + 64];
  size_t  protected_length = sizeof(protected_packet);
  ASSERT_EQ(srtp_protect(publisher.out, plain, sizeof(plain), protected_packet, &protected_length, 0),
            srtp_err_status_ok);
  ASSERT_EQ(send(publisher.fd, protected_packet, protected_length, 0), static_cast<ssize_t>(protected_length));
  ASSERT_TRUE(wait_until([&] { return counter("srtp_unprotect_ok") >= 1; })) << describe();

  ASSERT_TRUE(wait_until([&] { return counter("rr_sent") >= 1; })) << describe();

  timeval timeout{4, 0};
  setsockopt(publisher.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  uint8_t report[192];
  const ssize_t received = recv(publisher.fd, report, sizeof(report), 0);
  ASSERT_GT(received, 0);

  uint8_t plain_report[192];
  size_t  plain_length = sizeof(plain_report);
  ASSERT_EQ(srtp_unprotect_rtcp(publisher.in, report, static_cast<size_t>(received), plain_report, &plain_length),
            srtp_err_status_ok);
  ASSERT_GE(plain_length, 32u) << "header, sender SSRC and one report block";
  EXPECT_EQ(plain_report[0] & 0x1fu, 1u) << "RC must announce one report block";
  EXPECT_EQ(plain_report[1], 201u) << "RR 201";

  uint32_t reported_ssrc = 0;
  std::memcpy(&reported_ssrc, plain_report + 8, sizeof(reported_ssrc));
  EXPECT_EQ(ntohl(reported_ssrc), publisher_ssrc);
  EXPECT_EQ(plain_report[32], 0x81) << "an SDES chunk follows the report block";
  EXPECT_EQ(plain_report[33], 202u);
  EXPECT_EQ(plain_report[12], 0u) << "nothing lost in a one-packet interval";
  const uint32_t highest = ((uint32_t)plain_report[16] << 24) | ((uint32_t)plain_report[17] << 16) |
                           ((uint32_t)plain_report[18] << 8) | plain_report[19];
  EXPECT_EQ(highest, static_cast<uint32_t>(sequence));
  EXPECT_EQ(counter("sr_missing"), 0);
}

TEST_F(SfuDtlsTest, PliIsForwardedToThePublisherAndThrottled)
{
  StartServer(10, 30, 1);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "pliPub", "pwdPliPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "pliSub", "pwdPliSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9200));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9200, "pliPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9200, "pliSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; }));

  const uint32_t publisher_ssrc = 0x0A1B2C3Du;
  const uint32_t subscriber_ssrc = 0x0D3C2B1Au;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9200, "pliPub", publisher_ssrc, SFU_TRACK_VIDEO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9200, "pliSub", publisher_ssrc, subscriber_ssrc, 0));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));

  /* The binding itself asks for a keyframe, before the subscriber sends one. */
  ASSERT_TRUE(wait_until([&] { return counter("pli_on_join") == 1; })) << describe();
  ASSERT_TRUE(wait_until([&] { return counter("pli_forwarded") == 1; })) << describe();

  auto send_pli = [&](uint32_t media_ssrc) {
    uint8_t pli[12] = {0};
    pli[0] = 0x81; /* V=2, FMT 1 */
    pli[1] = 206;  /* PSFB, the standard RTCP payload type */
    pli[3] = 2;    /* 12 bytes */
    const uint32_t wire_media_ssrc = htonl(media_ssrc);
    std::memcpy(pli + 8, &wire_media_ssrc, sizeof(wire_media_ssrc));
    uint8_t protected_pli[sizeof(pli) + 64];
    size_t  protected_length = sizeof(protected_pli);
    EXPECT_EQ(srtp_protect_rtcp(subscriber.out, pli, sizeof(pli), protected_pli, &protected_length, 0),
              srtp_err_status_ok);
    EXPECT_EQ(send(subscriber.fd, protected_pli, protected_length, 0), static_cast<ssize_t>(protected_length));
  };

  timeval timeout{2, 0};
  setsockopt(publisher.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  uint8_t forwarded[192];
  const ssize_t forwarded_length = recv(publisher.fd, forwarded, sizeof(forwarded), 0);
  ASSERT_GT(forwarded_length, 0);

  uint8_t plain[192];
  size_t  plain_length = sizeof(plain);
  ASSERT_EQ(srtp_unprotect_rtcp(publisher.in, forwarded, static_cast<size_t>(forwarded_length), plain, &plain_length),
            srtp_err_status_ok);
  /* A compound: an empty receiver report, the PLI, then SDES. */
  ASSERT_GE(plain_length, 20u);
  EXPECT_EQ(plain[1], 201u) << "the compound opens with a receiver report";
  EXPECT_EQ(plain[8] & 0x1fu, 1u) << "FMT 1 is PLI";
  EXPECT_EQ(plain[9], 206u) << "PLI is PSFB 206";
  uint32_t target_ssrc = 0;
  std::memcpy(&target_ssrc, plain + 16, sizeof(target_ssrc));
  target_ssrc = ntohl(target_ssrc);
  EXPECT_EQ(target_ssrc, publisher_ssrc) << "the publisher sees its own SSRC, not the outbound one";
  EXPECT_EQ(plain[20], 0x81) << "an SDES chunk follows";
  EXPECT_EQ(plain[21], 202u) << "SDES carries the CNAME a compound requires";

  /* A subscriber request inside the join's window is suppressed, not forwarded. */
  send_pli(subscriber_ssrc);
  ASSERT_TRUE(wait_until([&] { return counter("pli_suppressed") >= 1; })) << describe();

  /* An outbound SSRC that belongs to no subscription is dropped. */
  send_pli(0xDEADBEEFu);
  ASSERT_TRUE(wait_until([&] { return counter("pli_dropped") >= 1; })) << describe();
  EXPECT_EQ(counter("pli_forwarded"), 1);
}

TEST_F(SfuDtlsTest, KeyframeRequestOnJoinObeysTheThrottleWindow)
{
  StartServer(10, 30, 0);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "joinPub", "pwdJoinPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "joinSub", "pwdJoinSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9300));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9300, "joinPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9300, "joinSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; }));

  const uint32_t publisher_ssrc = 0x1A2B3C4Du;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9300, "joinPub", publisher_ssrc, SFU_TRACK_VIDEO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));

  /* The first binding on the track opens the window: one PLI, counted as a join. */
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9300, "joinSub", publisher_ssrc, 0x11110001u, 0));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));
  ASSERT_TRUE(wait_until([&] { return counter("pli_on_join") == 1; })) << describe();
  ASSERT_TRUE(wait_until([&] { return counter("pli_forwarded") == 1; })) << describe();

  /* Re-stating the same binding is neither a join nor a rebind: no request. */
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9300, "joinSub", publisher_ssrc, 0x11110001u, 0));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 2; }));
  EXPECT_EQ(counter("pli_on_join"), 1);
  EXPECT_EQ(counter("pli_forwarded"), 1);

  /* A rebind inside the window is suppressed and must not extend it. */
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9300, "joinSub", publisher_ssrc, 0x11110002u, 0));
  ASSERT_TRUE(wait_until([&] { return counter("pli_on_join") == 2; })) << describe();
  ASSERT_TRUE(wait_until([&] { return counter("pli_suppressed") == 1; })) << describe();
  EXPECT_EQ(counter("pli_forwarded"), 1);

  /* Past the window the next rebind emits its own request. */
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9300, "joinSub", publisher_ssrc, 0x11110003u, 0));
  ASSERT_TRUE(wait_until([&] { return counter("pli_on_join") == 3; })) << describe();
  ASSERT_TRUE(wait_until([&] { return counter("pli_forwarded") == 2; })) << describe();
  EXPECT_EQ(counter("pli_suppressed"), 1);
}

TEST_F(SfuDtlsTest, MissingSenderReportIsCountedOnceAndNeverSynthesised)
{
  /* A one-second report cadence so the sweep runs often, but the missing-SR
   * threshold itself is the engine's five seconds: the wait is the point. */
  StartServer(10, 30, 1);
  ClientIdentity identity;
  ASSERT_TRUE(identity.create());
  PeerLink publisher;
  ASSERT_TRUE(attach_peer(&publisher, "srNone", "pwdSrNone", &identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9300));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9300, "srNone"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 1; }));

  const uint32_t publisher_ssrc = 0x0F1E2D3Cu;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9300, "srNone", publisher_ssrc, SFU_TRACK_AUDIO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));

  /* Media without any sender report: this is the publisher that never sends one. */
  uint8_t plain[12 + 20] = {0};
  plain[0] = 0x80;
  plain[1] = 111;
  plain[3] = 1; /* sequence 1, network order */
  const uint32_t wire_publisher_ssrc = htonl(publisher_ssrc);
  std::memcpy(plain + 8, &wire_publisher_ssrc, sizeof(wire_publisher_ssrc));

  uint8_t protected_packet[sizeof(plain) + 64];
  size_t  protected_length = sizeof(protected_packet);
  ASSERT_EQ(srtp_protect(publisher.out, plain, sizeof(plain), protected_packet, &protected_length, 0),
            srtp_err_status_ok);
  ASSERT_EQ(send(publisher.fd, protected_packet, protected_length, 0), static_cast<ssize_t>(protected_length));
  ASSERT_TRUE(wait_until([&] { return counter("srtp_unprotect_ok") >= 1; })) << describe();

  /* Reports still go out; it is the timing they cannot supply. */
  ASSERT_TRUE(wait_until([&] { return counter("rr_sent") >= 1; })) << describe();
  EXPECT_EQ(counter("sr_received"), 0);

  /* Counted once, after the threshold, and only once however many sweeps pass. */
  ASSERT_TRUE(wait_until([&] { return counter("sr_missing") >= 1; }, 12000)) << describe();
  EXPECT_EQ(counter("sr_missing"), 1);
  EXPECT_EQ(counter("sr_relayed"), 0) << "nothing is synthesised for subscribers";
  EXPECT_EQ(counter("sr_without_sdes"), 0);
}

/** Publish one plaintext RTP packet from @p publisher and let the shard fan it out. */
void publish_rtp(PeerLink *publisher, uint32_t ssrc, uint16_t sequence, uint8_t pt = 111)
{
  uint8_t plain[12 + 40];
  std::memset(plain, 0, sizeof(plain));
  plain[0] = 0x80;
  plain[1] = pt;
  plain[2] = static_cast<uint8_t>(sequence >> 8);
  plain[3] = static_cast<uint8_t>(sequence);
  const uint32_t wire_ssrc = htonl(ssrc);
  std::memcpy(plain + 8, &wire_ssrc, sizeof(wire_ssrc));
  for (size_t index = 12; index < sizeof(plain); index++) plain[index] = static_cast<uint8_t>(index + sequence);

  uint8_t protected_packet[sizeof(plain) + 64];
  size_t  protected_length = sizeof(protected_packet);
  EXPECT_EQ(srtp_protect(publisher->out, plain, sizeof(plain), protected_packet, &protected_length, 0),
            srtp_err_status_ok);
  EXPECT_EQ(send(publisher->fd, protected_packet, protected_length, 0), static_cast<ssize_t>(protected_length));
}

/** Send one SRTCP NACK for @p media_ssrc naming @p pid plus @p blp. */
void send_nack(PeerLink *subscriber, uint32_t media_ssrc, uint16_t pid, uint16_t blp)
{
  uint8_t report[16] = {0};
  report[0] = 0x81; /* V=2, FMT 1 (generic NACK) */
  report[1] = 205;  /* RTPFB */
  report[3] = 3;    /* (16 / 4) - 1 */
  const uint32_t wire_media_ssrc = htonl(media_ssrc);
  std::memcpy(report + 8, &wire_media_ssrc, sizeof(wire_media_ssrc));
  report[12] = static_cast<uint8_t>(pid >> 8);
  report[13] = static_cast<uint8_t>(pid);
  report[14] = static_cast<uint8_t>(blp >> 8);
  report[15] = static_cast<uint8_t>(blp);

  uint8_t protected_rtcp[sizeof(report) + 64];
  size_t  protected_length = sizeof(protected_rtcp);
  ASSERT_EQ(srtp_protect_rtcp(subscriber->out, report, sizeof(report), protected_rtcp, &protected_length, 0),
            srtp_err_status_ok);
  ASSERT_EQ(send(subscriber->fd, protected_rtcp, protected_length, 0), static_cast<ssize_t>(protected_length));
}

/** Receive one SRTP packet from @p peer and return its plaintext. */
std::vector<uint8_t> receive_rtp(PeerLink *peer)
{
  uint8_t       received[12 + 40 + 64];
  const ssize_t received_length = recv(peer->fd, received, sizeof(received), 0);
  if (received_length <= 0) return {};
  uint8_t plain[12 + 40 + 64];
  size_t  plain_length = sizeof(plain);
  if (srtp_unprotect(peer->in, received, static_cast<size_t>(received_length), plain, &plain_length) !=
      srtp_err_status_ok)
    return {};
  return std::vector<uint8_t>(plain, plain + plain_length);
}

TEST_F(SfuDtlsTest, NackRetransmitsRetainedPacketUnderTheSubscriberSsrc)
{
  StartServer(10, 30);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "nackPub", "pwdPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "nackSub", "pwdSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9101));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9101, "nackPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9101, "nackSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; })) << describe();

  const uint32_t publisher_ssrc = 0x33333333u;
  const uint32_t out_ssrc = 0x44444444u;
  const uint16_t seq_offset = 0x0500u;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9101, "nackPub", publisher_ssrc, SFU_TRACK_VIDEO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9101, "nackSub", publisher_ssrc, out_ssrc, seq_offset));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));

  for (uint16_t sequence = 1; sequence <= 3; sequence++) {
    publish_rtp(&publisher, publisher_ssrc, sequence);
    if (sequence == 2u) {
      /* Simulate the loss: the datagram arrives but the client never feeds it
       * to its replay window, so the retransmit is a fresh sequence to it. */
      uint8_t dropped[12 + 40 + 64];
      ASSERT_GT(recv(subscriber.fd, dropped, sizeof(dropped), 0), 0);
      continue;
    }
    ASSERT_FALSE(receive_rtp(&subscriber).empty());
  }
  ASSERT_TRUE(wait_until([&] { return counter("media_fanned_out") >= 3; }));

  /* The subscriber "lost" the second packet and asks for the sequence it
   * received: the answer carries its own SSRC and its offset sequence. */
  send_nack(&subscriber, out_ssrc, static_cast<uint16_t>(2u + seq_offset), 0);
  ASSERT_TRUE(wait_until([&] { return counter("nack_retransmits") >= 1; })) << describe();

  const std::vector<uint8_t> resent = receive_rtp(&subscriber);
  ASSERT_EQ(resent.size(), 12u + 40u);
  uint32_t resent_ssrc = 0;
  std::memcpy(&resent_ssrc, resent.data() + 8, sizeof(resent_ssrc));
  EXPECT_EQ(resent_ssrc, out_ssrc);
  EXPECT_EQ(static_cast<uint16_t>((resent[2] << 8) | resent[3]), static_cast<uint16_t>(2u + seq_offset));
  EXPECT_EQ(resent[1], 111);
  EXPECT_EQ(counter("nack_misses"), 0);
  EXPECT_EQ(counter("nack_retention_refused"), 0);
}

TEST_F(SfuDtlsTest, NackGuardsAreCounted)
{
  StartServer(10, 30);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "guardPub", "pwdPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "guardSub", "pwdSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9102));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9102, "guardPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9102, "guardSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; }));

  const uint32_t publisher_ssrc = 0x55555555u;
  const uint32_t out_ssrc = 0x66666666u;
  const uint16_t seq_offset = 0x0200u;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9102, "guardPub", publisher_ssrc, SFU_TRACK_AUDIO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9102, "guardSub", publisher_ssrc, out_ssrc, seq_offset));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));

  for (uint16_t sequence = 1; sequence <= 2; sequence++) {
    publish_rtp(&publisher, publisher_ssrc, sequence);
    ASSERT_FALSE(receive_rtp(&subscriber).empty());
  }

  /* An answered sequence, then the same one again inside the cooldown. */
  send_nack(&subscriber, out_ssrc, static_cast<uint16_t>(1u + seq_offset), 0);
  ASSERT_TRUE(wait_until([&] { return counter("nack_retransmits") >= 1; })) << describe();
  send_nack(&subscriber, out_ssrc, static_cast<uint16_t>(1u + seq_offset), 0);
  ASSERT_TRUE(wait_until([&] { return counter("nack_rate_limited") >= 1; })) << describe();

  send_nack(&subscriber, out_ssrc, static_cast<uint16_t>(0x7FFFu), 0);
  ASSERT_TRUE(wait_until([&] { return counter("nack_misses") >= 1; })) << describe();

  /* An SSRC the shard never handed out is dropped, not guessed at. */
  send_nack(&subscriber, 0x0BADF00Du, 1, 0);
  ASSERT_TRUE(wait_until([&] { return counter("nack_dropped") >= 1; })) << describe();
  EXPECT_EQ(counter("nack_retransmits"), 1);
}

TEST_F(SfuDtlsTest, NackMissOnAVideoTrackAsksForAKeyframe)
{
  StartServer(10, 30);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "lossPub", "pwdLossPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "lossSub", "pwdLossSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9103));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9103, "lossPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9103, "lossSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; }));

  const uint32_t publisher_ssrc = 0x77770001u;
  const uint32_t out_ssrc = 0x77770002u;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9103, "lossPub", publisher_ssrc, SFU_TRACK_VIDEO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9103, "lossSub", publisher_ssrc, out_ssrc, 0));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));
  ASSERT_TRUE(wait_until([&] { return counter("pli_forwarded") == 1; })) << describe();

  /* Nothing has been published, so the ring cannot answer: the miss asks for a
   * keyframe, and the join's request is still inside the window so it coalesces. */
  send_nack(&subscriber, out_ssrc, 0x4000, 0);
  ASSERT_TRUE(wait_until([&] { return counter("nack_misses") == 1; })) << describe();
  ASSERT_TRUE(wait_until([&] { return counter("pli_on_loss") == 1; })) << describe();
  ASSERT_TRUE(wait_until([&] { return counter("pli_suppressed") >= 1; })) << describe();
  EXPECT_EQ(counter("pli_forwarded"), 1);
}

TEST_F(SfuDtlsTest, AudioJoinsAndLossesNeverAskForAKeyframe)
{
  StartServer(10, 30);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "audPub", "pwdAudPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "audSub", "pwdAudSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9104));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9104, "audPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9104, "audSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; }));

  const uint32_t publisher_ssrc = 0x88880001u;
  const uint32_t out_ssrc = 0x88880002u;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9104, "audPub", publisher_ssrc, SFU_TRACK_AUDIO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9104, "audSub", publisher_ssrc, out_ssrc, 0));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));

  /* Audio frames are independent: neither the join nor a retention miss asks. */
  send_nack(&subscriber, out_ssrc, 0x4000, 0);
  ASSERT_TRUE(wait_until([&] { return counter("nack_misses") == 1; })) << describe();
  EXPECT_EQ(counter("pli_on_join"), 0);
  EXPECT_EQ(counter("pli_on_loss"), 0);
  EXPECT_EQ(counter("pli_forwarded"), 0);
}

TEST_F(SfuDtlsTest, NackBudgetZeroDisablesRetransmission)
{
  nack_retransmit_per_second_ = 0;
  StartServer(10, 30);
  ClientIdentity publisher_identity;
  ClientIdentity subscriber_identity;
  ASSERT_TRUE(publisher_identity.create());
  ASSERT_TRUE(subscriber_identity.create());

  PeerLink publisher;
  PeerLink subscriber;
  ASSERT_TRUE(attach_peer(&publisher, "zeroPub", "pwdPub", &publisher_identity));
  ASSERT_TRUE(attach_peer(&subscriber, "zeroSub", "pwdSub", &subscriber_identity));

  ASSERT_TRUE(SfuServerCreateRoom(server_, 0, 9103));
  ASSERT_TRUE(wait_until([&] { return counter("rooms") == 1; }));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9103, "zeroPub"));
  ASSERT_TRUE(SfuServerJoinRoom(server_, 0, 9103, "zeroSub"));
  ASSERT_TRUE(wait_until([&] { return counter("room_joins") == 2; }));

  const uint32_t publisher_ssrc = 0x77777777u;
  const uint32_t out_ssrc = 0x88888888u;
  ASSERT_TRUE(SfuServerAddTrack(server_, 0, 9103, "zeroPub", publisher_ssrc, SFU_TRACK_AUDIO, 0));
  ASSERT_TRUE(wait_until([&] { return counter("tracks_added") == 1; }));
  ASSERT_TRUE(SfuServerSubscribe(server_, 0, 9103, "zeroSub", publisher_ssrc, out_ssrc, 0));
  ASSERT_TRUE(wait_until([&] { return counter("subscriptions_added") == 1; }));

  publish_rtp(&publisher, publisher_ssrc, 1);
  ASSERT_FALSE(receive_rtp(&subscriber).empty());

  send_nack(&subscriber, out_ssrc, 1, 0);
  ASSERT_TRUE(wait_until([&] { return counter("nack_dropped") >= 1; })) << describe();
  EXPECT_EQ(counter("nack_retransmits"), 0);
  EXPECT_EQ(counter("nack_retransmit_per_second"), 0);
}

}  // namespace
