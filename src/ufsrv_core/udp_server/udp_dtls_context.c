/**
 * @file udp_dtls_context.c
 * @brief DTLS credentials and stateless address validation cookies.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <sys/socket.h>

#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <ufsrv_core/udp_server/udp_dtls_context.h>

enum { DTLS_COOKIE_LENGTH = 32 };

static CRYPTO_ONCE cookie_index_once = CRYPTO_ONCE_STATIC_INIT;
static int cookie_key_index = -1;

static void
FreeCookieKey(void *parent, void *key, CRYPTO_EX_DATA *data, int index, long argument, void *pointer)
{
  (void)parent;
  (void)data;
  (void)index;
  (void)argument;
  (void)pointer;
  OPENSSL_clear_free(key, DTLS_COOKIE_LENGTH);
}

static void
InitCookieKeyIndex(void)
{
  cookie_key_index = SSL_CTX_get_ex_new_index(0, NULL, NULL, NULL, FreeCookieKey);
}

static int
GenerateCookie(SSL *ssl, unsigned char *cookie, unsigned int *length)
{
  const unsigned char *key = SSL_CTX_get_ex_data(SSL_get_SSL_CTX(ssl), cookie_key_index);
  if (!key) return 0;

  BIO_ADDR *peer = BIO_ADDR_new();
  if (!peer) return 0;
  if (BIO_dgram_get_peer(SSL_get_rbio(ssl), peer) <= 0) {
    BIO_ADDR_free(peer);
    return 0;
  }

  // HMAC the address family, address bytes, and network-order port.
  unsigned char identity[1 + 16 + 2];
  size_t address_length = 16;
  const int family = BIO_ADDR_family(peer);
  const unsigned short port = BIO_ADDR_rawport(peer);
  int has_address = BIO_ADDR_rawaddress(peer, identity + 1, &address_length);
  BIO_ADDR_free(peer);
  if (!has_address || !((family == AF_INET && address_length == 4) ||
                        (family == AF_INET6 && address_length == 16))) return 0;

  identity[0] = family == AF_INET ? 4 : 6;
  identity[1 + address_length] = ((const unsigned char *)&port)[0];
  identity[2 + address_length] = ((const unsigned char *)&port)[1];
  return HMAC(EVP_sha256(), key, DTLS_COOKIE_LENGTH, identity, 3 + address_length, cookie, length) != NULL;
}

static int
VerifyCookie(SSL *ssl, const unsigned char *cookie, unsigned int length)
{
  if (length != DTLS_COOKIE_LENGTH) return 0;
  unsigned char expected[DTLS_COOKIE_LENGTH];
  unsigned int expected_length = 0;
  return GenerateCookie(ssl, expected, &expected_length) && expected_length == length &&
         CRYPTO_memcmp(cookie, expected, length) == 0;
}

SSL_CTX *
UdpDtlsServerContextCreate(const char *chain_file, const char *key_file)
{
  if (!chain_file || !*chain_file || !key_file || !*key_file ||
      !CRYPTO_THREAD_run_once(&cookie_index_once, InitCookieKeyIndex) || cookie_key_index < 0) return NULL;

  SSL_CTX *ctx = SSL_CTX_new(DTLS_server_method());
  if (!ctx) return NULL;

  if (!SSL_CTX_set_min_proto_version(ctx, DTLS1_2_VERSION) ||
      SSL_CTX_use_certificate_chain_file(ctx, chain_file) != 1 ||
      SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1 ||
      SSL_CTX_check_private_key(ctx) != 1) goto fail;

  unsigned char *key = OPENSSL_malloc(DTLS_COOKIE_LENGTH);
  if (!key) goto fail;
  if (RAND_bytes(key, DTLS_COOKIE_LENGTH) != 1 || !SSL_CTX_set_ex_data(ctx, cookie_key_index, key)) {
    OPENSSL_clear_free(key, DTLS_COOKIE_LENGTH);
    goto fail;
  }

  SSL_CTX_set_cookie_generate_cb(ctx, GenerateCookie);
  SSL_CTX_set_cookie_verify_cb(ctx, VerifyCookie);
  return ctx;

fail:
  SSL_CTX_free(ctx);
  return NULL;
}
