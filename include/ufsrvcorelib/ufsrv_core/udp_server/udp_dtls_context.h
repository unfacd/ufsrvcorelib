/**
 * @file udp_dtls_context.h
 * @brief OpenSSL context for a DTLS listener.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef UFSRVCORELIB_UDP_DTLS_CONTEXT_H
#define UFSRVCORELIB_UDP_DTLS_CONTEXT_H

#include <openssl/ssl.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>

/**
 * Create a DTLS 1.2 or later server context for one local endpoint.
 * Loads the PEM chain and private key, and installs peer-bound cookie
 * callbacks for DTLSv1_listen. The datagram BIO must report its peer.
 * Failure returns NULL and leaves OpenSSL errors available to the caller.
 *
 * @code{.c}
 * SSL_CTX *ctx = UdpDtlsServerContextCreate("chain.pem", "key.pem");
 * if (ctx) {
 *   // Create listener SSL objects from ctx and use DTLSv1_listen().
 *   SSL_CTX_free(ctx);
 * }
 * @endcode
 */
PUBLIC_API SSL_CTX *UdpDtlsServerContextCreate(const char *chain_file, const char *key_file);

#endif
