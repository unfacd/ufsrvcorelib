/**
 * @file udp_dtls_bio.c
 * @brief Datagram BIO backed by caller-provided UDP I/O.
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

#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <string.h>

#include <openssl/crypto.h>

#include <ufsrv_core/udp_server/udp_dtls_bio.h>

enum { DTLS_UDP_DEFAULT_MTU = 1200, DTLS_UDP_MAX_PAYLOAD = 65507 };

typedef struct UdpDtlsBioState
{
  struct sockaddr_storage peer;
  socklen_t peer_len;
  UdpDtlsBioSend send_datagram;
  void *context;
  unsigned char *input;
  size_t input_len;
  long mtu;
  bool peek;
  bool mtu_exceeded;
} UdpDtlsBioState;

static BIO_METHOD *datagram_method;
static int datagram_type;
static CRYPTO_ONCE datagram_method_once = CRYPTO_ONCE_STATIC_INIT;

static void
DiscardDatagram(UdpDtlsBioState *state)
{
  OPENSSL_free(state->input);
  state->input = NULL;
  state->input_len = 0;
}

static int
DestroyDatagramBio(BIO *bio)
{
  UdpDtlsBioState *state = BIO_get_data(bio);
  if (state) {
    DiscardDatagram(state);
    OPENSSL_free(state);
  }
  return 1;
}

static int
ReadDatagram(BIO *bio, char *out, int capacity)
{
  UdpDtlsBioState *state = BIO_get_data(bio);
  BIO_clear_retry_flags(bio);
  if (!out || capacity <= 0) return 0;
  if (!state->input) {
    BIO_set_retry_read(bio);
    return -1;
  }

  size_t copied = state->input_len < (size_t)capacity ? state->input_len : (size_t)capacity;
  memcpy(out, state->input, copied);
  if (!state->peek) DiscardDatagram(state);
  return (int)copied;
}

static int
WriteDatagram(BIO *bio, const char *packet, int length)
{
  UdpDtlsBioState *state = BIO_get_data(bio);
  BIO_clear_retry_flags(bio);
  state->mtu_exceeded = false;
  if (!packet || length <= 0) return 0;

  ssize_t sent = state->send_datagram(state->context, (const struct sockaddr *)&state->peer,
                                      state->peer_len, packet, (size_t)length);
  if (sent == length) return length;
  if (sent < 0) {
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) BIO_set_retry_write(bio);
    if (errno == EMSGSIZE) state->mtu_exceeded = true;
  }
  return -1;
}

static long
GetDatagramPeer(const UdpDtlsBioState *state, BIO_ADDR *peer)
{
  if (!peer) return 0;
  if (state->peer.ss_family == AF_INET) {
    const struct sockaddr_in *address = (const struct sockaddr_in *)&state->peer;
    return BIO_ADDR_rawmake(peer, AF_INET, &address->sin_addr, sizeof(address->sin_addr), address->sin_port)
             ? (long)sizeof(*address) : 0;
  }

  const struct sockaddr_in6 *address = (const struct sockaddr_in6 *)&state->peer;
  return BIO_ADDR_rawmake(peer, AF_INET6, &address->sin6_addr, sizeof(address->sin6_addr), address->sin6_port)
           ? (long)sizeof(*address) : 0;
}

static long
DatagramControl(BIO *bio, int command, long value, void *argument)
{
  UdpDtlsBioState *state = BIO_get_data(bio);
  if (!state) return 0;

  switch (command) {
    case BIO_CTRL_RESET:
      DiscardDatagram(state);
      state->peek = false;
      state->mtu_exceeded = false;
      return 1;
    case BIO_CTRL_PENDING:
      return (long)state->input_len;
    case BIO_CTRL_FLUSH:
    case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:
      return 1;
    case BIO_CTRL_DGRAM_GET_PEER:
      return GetDatagramPeer(state, argument);
    case BIO_CTRL_DGRAM_SET_PEEK_MODE:
      state->peek = value != 0;
      return 1;
    case BIO_CTRL_DGRAM_QUERY_MTU:
    case BIO_CTRL_DGRAM_GET_MTU:
      return state->mtu;
    case BIO_CTRL_DGRAM_SET_MTU:
      if (value <= 0 || value > DTLS_UDP_MAX_PAYLOAD) return 0;
      state->mtu = value;
      return value;
    case BIO_CTRL_DGRAM_GET_FALLBACK_MTU:
      return state->peer.ss_family == AF_INET ? 548 : 1232;
    case BIO_CTRL_DGRAM_GET_MTU_OVERHEAD:
      return state->peer.ss_family == AF_INET ? 28 : 48;
    case BIO_CTRL_DGRAM_MTU_EXCEEDED: {
      const bool exceeded = state->mtu_exceeded;
      state->mtu_exceeded = false;
      return exceeded;
    }
    default:
      return 0;
  }
}

static void
InitDatagramMethod(void)
{
  int index = BIO_get_new_index();
  if (index < 0) return;
  datagram_type = index | BIO_TYPE_SOURCE_SINK;
  BIO_METHOD *method = BIO_meth_new(datagram_type, "ufsrv DTLS datagram");
  if (!method) return;

  if (!BIO_meth_set_read(method, ReadDatagram) || !BIO_meth_set_write(method, WriteDatagram) ||
      !BIO_meth_set_ctrl(method, DatagramControl) || !BIO_meth_set_destroy(method, DestroyDatagramBio)) {
    BIO_meth_free(method);
    return;
  }
  datagram_method = method;
}

BIO *
UdpDtlsBioCreate(const struct sockaddr *peer, socklen_t peer_len, UdpDtlsBioSend send_datagram, void *context)
{
  if (!peer || peer_len < sizeof(peer->sa_family) || !send_datagram) return NULL;
  socklen_t expected_length;
  if (peer->sa_family == AF_INET) expected_length = sizeof(struct sockaddr_in);
  else if (peer->sa_family == AF_INET6) expected_length = sizeof(struct sockaddr_in6);
  else return NULL;
  if (peer_len < expected_length || !CRYPTO_THREAD_run_once(&datagram_method_once, InitDatagramMethod) ||
      !datagram_method) return NULL;

  BIO *bio = BIO_new(datagram_method);
  if (!bio) return NULL;
  UdpDtlsBioState *state = OPENSSL_zalloc(sizeof(*state));
  if (!state) {
    BIO_free(bio);
    return NULL;
  }

  memcpy(&state->peer, peer, expected_length);
  state->peer_len = expected_length;
  state->send_datagram = send_datagram;
  state->context = context;
  state->mtu = DTLS_UDP_DEFAULT_MTU;
  BIO_set_data(bio, state);
  BIO_set_init(bio, 1);
  return bio;
}

int
UdpDtlsBioFeed(BIO *bio, const void *packet, size_t length)
{
  if (!bio || !packet || !length || length > DTLS_UDP_MAX_PAYLOAD ||
      !CRYPTO_THREAD_run_once(&datagram_method_once, InitDatagramMethod) ||
      !datagram_method || BIO_method_type(bio) != datagram_type) return 0;
  UdpDtlsBioState *state = BIO_get_data(bio);
  if (!state || state->input) return 0;
  state->input = OPENSSL_memdup(packet, length);
  if (!state->input) return 0;
  state->input_len = length;
  return 1;
}
