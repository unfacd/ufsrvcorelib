/**
 * @file sfu_shard_dtls.c
 * @brief Drive one peer's DTLS handshake over a memory BIO pair.
 *
 * OpenSSL never sees the socket: the shard writes received datagrams into the
 * peer's inbound BIO and reads complete records back out of the outbound BIO,
 * sending one datagram per record. The handshake is pumped until OpenSSL asks
 * for more input; DTLS records from one flight are split on the record header
 * so they leave as separate datagrams, which is what the peer's stack expects.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include "config_ufsrvcorelib.h"
#endif

#include <arpa/inet.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/srtp.h>
#include <openssl/x509.h>

#include "sfu_server_priv.h"
#include "dtls/sfu_dtls_context.h"
#include "dtls/sfu_shard_dtls.h"
#include "srtp/sfu_shard_srtp.h"
#include "events/sfu_shard_events.h"

#define SFU_DTLS_MTU 1200
#define SFU_DTLS_FLUSH_BUFFER 4096u

static SSL_CTX *
sSslCtx(SfuShard *shard)
{
  return SfuDtlsContextSslCtx(shard->dtls_context);
}

static void
sFail(SfuShard *shard, SfuPeer *peer)
{
  shard->spec->counters->dtls_failures++;
  SfuShardDtlsReset(shard, peer);
  peer->dtls.state = SFU_DTLS_FAILED;
}

/** Send one DTLS datagram per record; a flight is several records, not one. */
static void
sFlush(SfuShard *shard, SfuPeer *peer, const struct sockaddr_storage *destination, socklen_t destination_length)
{
  BIO *out = SSL_get_wbio(peer->dtls.ssl);
  if (!out) return;

  unsigned char buffer[SFU_DTLS_FLUSH_BUFFER];
  while (BIO_ctrl_pending(out) > 0) {
    const int received = BIO_read(out, buffer, sizeof(buffer));
    if (received <= 0) break;

    size_t offset = 0;
    while (offset + 13u <= (size_t)received) {
      uint16_t record_length;
      memcpy(&record_length, buffer + offset + 11u, sizeof(record_length));
      const size_t total = 13u + ntohs(record_length);
      if (offset + total > (size_t)received) break;

      if (sendto(shard->fd, buffer + offset, total, 0, (const struct sockaddr *)destination, destination_length) >= 0) {
        shard->spec->counters->tx_datagrams++;
        shard->spec->counters->dtls_records_out++;
      } else {
        shard->spec->counters->drops_send++;
      }
      offset += total;
    }
  }
}


/**
 * The cookie binds the handshake to the peer's address, so an off-path spoofed
 * ClientHello cannot make the shard allocate handshake state. OpenSSL's default
 * callback asks the BIO for the peer address, which a memory BIO cannot answer;
 * these callbacks take it from the peer record instead and key the cookie with
 * the context's secret.
 */
static size_t
sCookieInput(const SfuPeer *peer, unsigned char *out)
{
  if (peer->address.ss_family == AF_INET) {
    const struct sockaddr_in *address = (const struct sockaddr_in *)&peer->address;
    out[0] = 4;
    memcpy(out + 1, &address->sin_addr, sizeof(address->sin_addr));
    memcpy(out + 1 + sizeof(address->sin_addr), &address->sin_port, sizeof(address->sin_port));
    return 1 + sizeof(address->sin_addr) + sizeof(address->sin_port);
  }
  const struct sockaddr_in6 *address = (const struct sockaddr_in6 *)&peer->address;
  out[0] = 6;
  memcpy(out + 1, &address->sin6_addr, sizeof(address->sin6_addr));
  memcpy(out + 1 + sizeof(address->sin6_addr), &address->sin6_port, sizeof(address->sin6_port));
  return 1 + sizeof(address->sin6_addr) + sizeof(address->sin6_port);
}

static bool
sCookieFor(SSL *ssl, unsigned char cookie[32])
{
  SfuPeer *peer = SSL_get_app_data(ssl);
  SfuDtlsContext *context = SSL_CTX_get_app_data(SSL_get_SSL_CTX(ssl));
  if (!peer || !context) return false;

  uint8_t secret[32];
  if (!SfuDtlsContextCookieSecret(context, secret)) return false;

  unsigned char input[1 + 16 + 2];
  const size_t  length = sCookieInput(peer, input);

  unsigned int  digest_length = 0;
  return HMAC(EVP_sha256(), secret, sizeof(secret), input, length, cookie, &digest_length) != NULL &&
         digest_length == 32;
}

static int
sCookieGenerate(SSL *ssl, unsigned char *cookie, unsigned int *cookie_len)
{
  if (!sCookieFor(ssl, cookie)) return 0;
  *cookie_len = 32;
  return 1;
}

static int
sCookieVerify(SSL *ssl, const unsigned char *cookie, unsigned int cookie_len)
{
  unsigned char expected[32];
  if (cookie_len != 32 || !sCookieFor(ssl, expected)) return 0;
  return CRYPTO_memcmp(cookie, expected, sizeof(expected)) == 0;
}

static bool
sStart(SfuShard *shard, SfuPeer *peer)
{
  SSL_CTX *context = sSslCtx(shard);
  if (!context) return false;

  SSL *ssl = SSL_new(context);
  if (!ssl) return false;

  BIO *inbound = BIO_new(BIO_s_mem());
  BIO *outbound = BIO_new(BIO_s_mem());
  if (!inbound || !outbound) {
    if (inbound) BIO_free(inbound);
    if (outbound) BIO_free(outbound);
    SSL_free(ssl);
    return false;
  }

  SSL_set_bio(ssl, inbound, outbound); /* ssl owns both from here */
  SSL_CTX_set_cookie_generate_cb(context, sCookieGenerate);
  SSL_CTX_set_cookie_verify_cb(context, sCookieVerify);
  SSL_set_app_data(ssl, peer);
  SSL_set_accept_state(ssl);
  SSL_set_mtu(ssl, SFU_DTLS_MTU);

  peer->dtls.ssl = ssl;
  peer->dtls.inbound = inbound;
  peer->dtls.state = SFU_DTLS_HANDSHAKING;
  peer->dtls.retransmit_at_ms = 0;
  shard->spec->counters->dtls_handshakes_started++;
  return true;
}

static bool
sFingerprintOf(X509 *certificate, uint8_t out[SFU_DTLS_FINGERPRINT_SIZE])
{
  unsigned char *der = NULL;
  const int      length = i2d_X509(certificate, &der);
  if (length <= 0 || !der) return false;

  unsigned int  digest_length = 0;
  unsigned char digest[EVP_MAX_MD_SIZE];
  const bool    ok = EVP_Digest(der, (size_t)length, digest, &digest_length, EVP_sha256(), NULL) == 1 &&
                  digest_length == SFU_DTLS_FINGERPRINT_SIZE;
  if (ok) memcpy(out, digest, SFU_DTLS_FINGERPRINT_SIZE);
  OPENSSL_free(der);
  return ok;
}

/** The handshake is complete: authenticate by fingerprint, then export keys. */
static bool
sComplete(SfuShard *shard, SfuPeer *peer)
{
  SSL *ssl = peer->dtls.ssl;

  X509 *certificate = SSL_get1_peer_certificate(ssl);
  if (!certificate) return false;

  uint8_t fingerprint[SFU_DTLS_FINGERPRINT_SIZE];
  const bool have_fingerprint = sFingerprintOf(certificate, fingerprint);
  X509_free(certificate);
  if (!have_fingerprint) return false;

  if (!peer->dtls.has_remote_fingerprint ||
      CRYPTO_memcmp(fingerprint, peer->dtls.remote_fingerprint, SFU_DTLS_FINGERPRINT_SIZE) != 0) {
    shard->spec->counters->dtls_fingerprint_mismatches++;
    return false;
  }

  const SRTP_PROTECTION_PROFILE *profile = SSL_get_selected_srtp_profile(ssl);
  if (!profile) return false; /* no DTLS-SRTP: nothing to hand the media layer */

  if (SSL_export_keying_material(ssl, peer->dtls.key_material, SFU_DTLS_KEY_MATERIAL_SIZE,
                                 "EXTRACTOR-dtls_srtp", strlen("EXTRACTOR-dtls_srtp"), NULL, 0, 0) != 1) {
    return false;
  }

  peer->dtls.srtp_profile = profile->id;
  if (!SfuShardSrtpStart(shard, peer)) return false;

  peer->dtls.state = SFU_DTLS_READY;
  peer->dtls.retransmit_at_ms = 0;
  shard->spec->counters->dtls_handshakes_completed++;
  shard->spec->counters->dtls_sessions++;
  SfuShardEventPeerConnected(shard, peer);
  return true;
}

static void
sScheduleTimer(SfuPeer *peer, uint64_t now_ms)
{
  struct timeval timeout;
  if (peer->dtls.ssl && peer->dtls.state == SFU_DTLS_HANDSHAKING &&
      DTLSv1_get_timeout(peer->dtls.ssl, &timeout)) {
    peer->dtls.retransmit_at_ms = now_ms + (uint64_t)timeout.tv_sec * 1000u + (uint64_t)timeout.tv_usec / 1000u;
  } else {
    peer->dtls.retransmit_at_ms = 0;
  }
}

static uint64_t
sNowMilliseconds(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000u + (uint64_t)(now.tv_nsec / 1000000);
}

/** Pump the handshake until OpenSSL wants input; flush whatever it wrote. */
static void
sPump(SfuShard *shard, SfuPeer *peer, const struct sockaddr_storage *source, socklen_t source_length)
{
  SSL *ssl = peer->dtls.ssl;
  if (!ssl) return;

  for (int round = 0; round < 4; round++) {
    const int result = SSL_accept(ssl);
    sFlush(shard, peer, source, source_length);

    if (result == 1) break;
    const int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_WRITE) continue;
    if (error == SSL_ERROR_WANT_READ) break;
    unsigned long detail = ERR_get_error();
    char          text[256];
    ERR_error_string_n(detail, text, sizeof(text));
    syslog(LOG_ERR, "%s: DTLS handshake failed: ssl_error=%d %s", __func__, error, text);
    sFail(shard, peer);
    return;
  }

  if (peer->dtls.state == SFU_DTLS_HANDSHAKING && SSL_is_init_finished(ssl)) {
    if (!sComplete(shard, peer)) sFail(shard, peer);
  }
  if (peer->dtls.state == SFU_DTLS_HANDSHAKING) sScheduleTimer(peer, sNowMilliseconds());
}

bool
SfuShardHandleDtls(SfuShard *shard, SfuPeer *peer, SfuPacket *packet, const struct sockaddr_storage *source,
                   socklen_t source_length)
{
  SfuCounters *counters = shard->spec->counters;
  counters->dtls_records_in++;

  const uint8_t *record = packet->data + SFU_PACKET_HEADROOM;
  if (peer->dtls.state == SFU_DTLS_READY) {
    if (packet->length && record[0] == SSL3_RT_ALERT &&
        BIO_write(peer->dtls.inbound, record, packet->length) > 0) {
      unsigned char ignored;
      (void)SSL_read(peer->dtls.ssl, &ignored, sizeof(ignored));
      if (SSL_get_shutdown(peer->dtls.ssl) & SSL_RECEIVED_SHUTDOWN) {
        SfuShardEventPeerDisconnected(shard, peer, SFU_DISC_DTLS_CLOSED);
        SfuShardDtlsReset(shard, peer);
        peer->dtls.state = SFU_DTLS_FAILED;
      }
    }
    return false;
  }
  if (peer->dtls.state == SFU_DTLS_FAILED) return false;
  if (!peer->dtls.ssl && !sStart(shard, peer)) {
    counters->dtls_failures++;
    return false;
  }

  if (BIO_write(peer->dtls.inbound, record, packet->length) <= 0) {
    sFail(shard, peer);
    return false;
  }

  sPump(shard, peer, source, source_length);
  return peer->dtls.state == SFU_DTLS_READY;
}

void
SfuShardDtlsTick(SfuShard *shard, SfuPeer *peer, uint64_t now_ms)
{
  if (peer->dtls.state != SFU_DTLS_HANDSHAKING || !peer->dtls.ssl) return;
  if (!peer->dtls.retransmit_at_ms || now_ms < peer->dtls.retransmit_at_ms) return;

  if (DTLSv1_handle_timeout(peer->dtls.ssl) < 0) {
    sFail(shard, peer);
    return;
  }
  shard->spec->counters->dtls_timeouts++;
  sFlush(shard, peer, &peer->address, peer->address_length);
  sScheduleTimer(peer, now_ms);
}

void
SfuShardDtlsReset(SfuShard *shard, SfuPeer *peer)
{
  if (peer->dtls.state == SFU_DTLS_READY) shard->spec->counters->dtls_sessions--;
  SfuShardSrtpStop(shard, peer);
  if (peer->dtls.ssl) {
    SSL_free(peer->dtls.ssl); /* frees both BIOs */
    peer->dtls.ssl = NULL;
  }
  peer->dtls.inbound = NULL;
  peer->dtls.state = SFU_DTLS_NONE;
  peer->dtls.srtp_profile = 0;
  peer->dtls.retransmit_at_ms = 0;
  memset(peer->dtls.key_material, 0, sizeof(peer->dtls.key_material));
}
