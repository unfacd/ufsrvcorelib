/**
 * @file sfu_dtls_context.h
 * @brief The process-wide DTLS context and the certificate it hands out.
 *
 * Created by the main thread before any shard starts and destroyed after every
 * shard has been joined, so shard threads only ever read it. The certificate is
 * generated once at startup unless a certificate/key pair is configured; its
 * SHA-256 fingerprint is what the signalling layer stamps into SDP answers, and
 * what peers verify against our certificate.
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

#ifndef UFSRVCORELIB_SFU_DTLS_CONTEXT_H
#define UFSRVCORELIB_SFU_DTLS_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include <uflib/standard_ssl_includes.h>

#include "model/dtls_state_type.h"

typedef struct SfuDtlsContext SfuDtlsContext;

/**
 * Build the context: DTLS 1.2 only, SRTP profiles, peer certificate requested,
 * read-ahead enabled. A NULL certificate or key path generates a self-signed
 * ECDSA P-256 pair for this process.
 */
SfuDtlsContext *SfuDtlsContextCreate(const char *certificate_path, const char *key_path);

/** Release the context, its certificate and its key. NULL is safe. */
void SfuDtlsContextDestroy(SfuDtlsContext *context);

/** The OpenSSL context a peer's SSL object is created from. NULL when @p context is NULL. */
SSL_CTX *SfuDtlsContextSslCtx(const SfuDtlsContext *context);

/** SHA-256 of the DER certificate, the value SDP advertises. False when unavailable. */
bool SfuDtlsContextFingerprint(const SfuDtlsContext *context, uint8_t out[SFU_DTLS_FINGERPRINT_SIZE]);

/**
 * The per-context secret the DTLS cookie is keyed with. The cookie callbacks
 * live beside the peer (they need its address), so they retrieve the secret
 * through the SSL's context.
 */
bool SfuDtlsContextCookieSecret(const SfuDtlsContext *context, uint8_t out[32]);

#endif /* UFSRVCORELIB_SFU_DTLS_CONTEXT_H */
