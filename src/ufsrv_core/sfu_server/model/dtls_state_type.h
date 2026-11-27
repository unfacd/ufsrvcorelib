/**
 * @file dtls_state_type.h
 * @brief Per-peer DTLS state: the SSL object, its inbound BIO and key material.
 *
 * The SSL object is created lazily on the peer's first DTLS record and freed on
 * release, so a registered peer that never handshakes costs nothing. The
 * outbound BIO is owned by the SSL object; only the inbound half is held here
 * so records can be pushed into it.
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

#ifndef UFSRVCORELIB_DTLS_STATE_TYPE_H
#define UFSRVCORELIB_DTLS_STATE_TYPE_H

#include <stdbool.h>
#include <stdint.h>

#include <uflib/standard_ssl_includes.h>

#define SFU_DTLS_FINGERPRINT_SIZE 32u
#define SFU_DTLS_KEY_MATERIAL_SIZE 60u  /* RFC 5764 §4.2 exporter output */

typedef enum SfuDtlsState {
  SFU_DTLS_NONE = 0,  ///< no SSL object yet
  SFU_DTLS_HANDSHAKING,
  SFU_DTLS_READY,     ///< handshake complete, fingerprint matched, keys exported
  SFU_DTLS_FAILED      ///< terminal for this association; records are ignored
} SfuDtlsState;

typedef struct SfuPeerDtls {
  SSL *       ssl;
  BIO *       inbound;   ///< app -> SSL; the outbound BIO belongs to ssl
  SfuDtlsState state;
  bool        has_remote_fingerprint;
  uint8_t     remote_fingerprint[SFU_DTLS_FINGERPRINT_SIZE];
  uint16_t    srtp_profile;
  uint8_t     key_material[SFU_DTLS_KEY_MATERIAL_SIZE];
  uint64_t    retransmit_at_ms;
} SfuPeerDtls;

#endif /* UFSRVCORELIB_DTLS_STATE_TYPE_H */
