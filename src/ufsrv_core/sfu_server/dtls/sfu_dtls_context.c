/**
 * @file sfu_dtls_context.c
 * @brief One SSL_CTX for the process: certificate, SRTP profiles, policy.
 *
 * The certificate is generated here rather than loaded from disk unless the
 * caller supplies paths: a media server's DTLS certificate only has to match
 * the fingerprint it advertises in SDP, so there is no CA, no chain and no
 * reason to touch the filesystem. ECDSA P-256 keeps the handshake small, which
 * matters when it is carried over ICE-consented UDP paths.
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

#include <string.h>
#include <syslog.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <srtp3/srtp.h>

#include "sfu_dtls_context.h"

/* Preferred first: browsers offer AEAD, and libsrtp is checked for it when the
 * SRTP layer is initialised. */
#define SFU_DTLS_SRTP_PROFILES "SRTP_AEAD_AES_128_GCM:SRTP_AES128_CM_SHA1_80"
#define SFU_DTLS_SRTP_FALLBACK "SRTP_AES128_CM_SHA1_80"

struct SfuDtlsContext
{
  SSL_CTX * ssl_ctx;
  EVP_PKEY *private_key;
  X509 *    certificate;
  uint8_t   fingerprint[SFU_DTLS_FINGERPRINT_SIZE];
  bool      has_fingerprint;
  uint8_t   cookie_secret[32];
};

/** DTLS-SRTP authenticates with the SDP fingerprint, not a certificate chain. */
static int
sVerifyAcceptAll(int preverify_ok, X509_STORE_CTX *store)
{
  (void)preverify_ok;
  (void)store;
  return 1;
}

static X509 *
sGenerateCertificate(EVP_PKEY **key_out)
{
  EVP_PKEY *key = EVP_EC_gen("P-256");
  if (!key) return NULL;

  X509 *certificate = X509_new();
  if (!certificate) {
    EVP_PKEY_free(key);
    return NULL;
  }

  X509_set_version(certificate, 2); /* X509v3 */
  ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
  X509_gmtime_adj(X509_getm_notBefore(certificate), 0);
  X509_gmtime_adj(X509_getm_notAfter(certificate), 60L * 60L * 24L * 365L);
  X509_set_pubkey(certificate, key);

  X509_NAME *name = X509_get_subject_name(certificate);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)"ufsrvsfu", -1, -1, 0);
  X509_set_issuer_name(certificate, name);

  if (!X509_sign(certificate, key, EVP_sha256())) {
    X509_free(certificate);
    EVP_PKEY_free(key);
    return NULL;
  }
  *key_out = key;
  return certificate;
}

static bool
sComputeFingerprint(X509 *certificate, uint8_t out[SFU_DTLS_FINGERPRINT_SIZE])
{
  unsigned char *der        = NULL;
  const int      der_length = i2d_X509(certificate, &der);
  if (der_length <= 0 || !der) return false;

  unsigned int digest_length = 0;
  unsigned char digest[EVP_MAX_MD_SIZE];
  const bool ok = EVP_Digest(der, (size_t)der_length, digest, &digest_length, EVP_sha256(), NULL) == 1 && digest_length
    == SFU_DTLS_FINGERPRINT_SIZE;
  if (ok) memcpy(out, digest, SFU_DTLS_FINGERPRINT_SIZE);
  OPENSSL_free(der);
  return ok;
}

SfuDtlsContext *
SfuDtlsContextCreate(const char *certificate_path, const char *key_path)
{
  SfuDtlsContext *context = calloc(1, sizeof(*context));
  if (!context) return NULL;
  /* libsrtp needs its process-wide initialisation before the first session is created; the DTLS context is created exactly once, before any shard. */
  if (srtp_init() != srtp_err_status_ok) goto fail;

  context->ssl_ctx = SSL_CTX_new(DTLS_server_method());
  if (!context->ssl_ctx) goto fail;
  /* Cookie callbacks are installed per shard because they need the peer address; they find this context (and its secret) through the SSL_CTX. */
  SSL_CTX_set_app_data(context->ssl_ctx, context);
  if (RAND_bytes(context->cookie_secret, sizeof(context->cookie_secret)) != 1) goto fail;

  if (SSL_CTX_set_min_proto_version(context->ssl_ctx, DTLS1_2_VERSION) != 1 || SSL_CTX_set_max_proto_version(
    context->ssl_ctx, DTLS1_2_VERSION) != 1) {
    goto fail;
  }

  /* A datagram may carry more than one DTLS record; read-ahead lets OpenSSL consume them from the memory BIO in one call. */
  SSL_CTX_set_read_ahead(context->ssl_ctx, 1);
  SSL_CTX_set_options(context->ssl_ctx, SSL_OP_COOKIE_EXCHANGE);
  SSL_CTX_set_verify(context->ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, sVerifyAcceptAll);

  if (SSL_CTX_set_tlsext_use_srtp(context->ssl_ctx, SFU_DTLS_SRTP_PROFILES) != 0) {
    /* An OpenSSL built without the AEAD profile still negotiates the mandatory one; only a context that supports neither is fatal. */
    if (SSL_CTX_set_tlsext_use_srtp(context->ssl_ctx, SFU_DTLS_SRTP_FALLBACK) != 0) goto fail;
  }

  if (certificate_path && key_path) {
    if (SSL_CTX_use_certificate_chain_file(context->ssl_ctx, certificate_path) != 1) goto fail;
    if (SSL_CTX_use_PrivateKey_file(context->ssl_ctx, key_path, SSL_FILETYPE_PEM) != 1) goto fail;
    context->certificate = SSL_CTX_get0_certificate(context->ssl_ctx);
    if (context->certificate) X509_up_ref(context->certificate);
    context->private_key = SSL_CTX_get0_privatekey(context->ssl_ctx);
    if (context->private_key) EVP_PKEY_up_ref(context->private_key);
  }
  else {
    context->certificate = sGenerateCertificate(&context->private_key);
    if (!context->certificate) goto fail;
    if (SSL_CTX_use_certificate(context->ssl_ctx, context->certificate) != 1) goto fail;
    if (SSL_CTX_use_PrivateKey(context->ssl_ctx, context->private_key) != 1) goto fail;
  }

  if (SSL_CTX_check_private_key(context->ssl_ctx) != 1) goto fail;
  if (!context->certificate) context->certificate = SSL_CTX_get0_certificate(context->ssl_ctx);
  if (!context->certificate || !sComputeFingerprint(context->certificate, context->fingerprint)) goto fail;
  context->has_fingerprint = true;
  return context;

fail:
  syslog(LOG_ERR, "%s: could not initialise the DTLS context", __func__);
  SfuDtlsContextDestroy(context);
  return NULL;
}

void
SfuDtlsContextDestroy(SfuDtlsContext *context)
{
  if (!context) return;
  if (context->certificate) X509_free(context->certificate);
  if (context->private_key) EVP_PKEY_free(context->private_key);
  if (context->ssl_ctx) SSL_CTX_free(context->ssl_ctx);
  (void)srtp_shutdown();
  free(context);
}

SSL_CTX *
SfuDtlsContextSslCtx(const SfuDtlsContext *context)
{
  return context ? context->ssl_ctx : NULL;
}

bool
SfuDtlsContextCookieSecret(const SfuDtlsContext *context, uint8_t out[32])
{
  if (!context || !out) return false;
  memcpy(out, context->cookie_secret, sizeof(context->cookie_secret));
  return true;
}

bool
SfuDtlsContextFingerprint(const SfuDtlsContext *context, uint8_t out[SFU_DTLS_FINGERPRINT_SIZE])
{
  if (!context || !context->has_fingerprint || !out) return false;
  memcpy(out, context->fingerprint, SFU_DTLS_FINGERPRINT_SIZE);
  return true;
}
