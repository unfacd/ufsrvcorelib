/**
 * @file udp_dtls_bio.h
 * @brief Packet-preserving BIO for DTLS over a shared UDP socket.
 *

 Copyright (c) 2015-2026 unfacd works

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#ifndef UFSRVCORELIB_UDP_DTLS_BIO_H
#define UFSRVCORELIB_UDP_DTLS_BIO_H

#include <sys/socket.h>

#include <openssl/bio.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>

/**
 * Emit a complete datagram to the fixed peer on the caller's I/O thread.
 * Return its length on success or -1 with errno set on failure.
 */
typedef ssize_t (*UdpDtlsBioSend)(void *context, const struct sockaddr *peer, socklen_t peer_len,
                                 const void *data, size_t length);

/**
 * Create a BIO for one peer. The caller supplies the outbound path and
 * drives OpenSSL's retransmission timers. The BIO has no socket ownership.
 * After SSL_set_bio(), the SSL object owns the BIO.
 *
 * @code{.c}
 * BIO *bio = UdpDtlsBioCreate(peer, peer_len, ShardSendDatagram, shard);
 * if (bio) SSL_set_bio(ssl, bio, bio);
 * @endcode
 */
PUBLIC_API BIO *UdpDtlsBioCreate(const struct sockaddr *peer, socklen_t peer_len,
                                UdpDtlsBioSend send_datagram, void *context);

/**
 * Copy one inbound datagram. Returns 1 on success and 0 if a datagram is
 * already pending, the size is invalid, or allocation fails. Call only on
 * the thread that drives this BIO. BIO_reset() discards pending input.
 *
 * @code{.c}
 * if (UdpDtlsBioFeed(SSL_get_rbio(ssl), packet, packet_len))
 *   SSL_accept(ssl);
 * @endcode
 */
PUBLIC_API int UdpDtlsBioFeed(BIO *bio, const void *packet, size_t length);

#endif
