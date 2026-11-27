/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Trimmed copy of coturn's src/apps/common/ns_turn_openssl.h.
 *
 * The codec needs libcrypto only: digests, HMAC, AES-GCM for RFC 7635 tokens
 * and RAND_bytes. The original header also pulls in ssl/bio/bn/dh, which would
 * force every consumer of this package to install the libssl headers for no
 * reason. Keep the same include guard so that including both headers cannot
 * double-declare anything.
 */

#ifndef __NST_OPENSSL_LIB__
#define __NST_OPENSSL_LIB__

#include <openssl/crypto.h>   // IWYU pragma: export
#include <openssl/err.h>      // IWYU pragma: export
#include <openssl/evp.h>      // IWYU pragma: export
#include <openssl/hmac.h>     // IWYU pragma: export
#include <openssl/opensslv.h> // IWYU pragma: export
#include <openssl/rand.h>     // IWYU pragma: export
#include <openssl/sha.h>      // IWYU pragma: export

#endif /* __NST_OPENSSL_LIB__ */
