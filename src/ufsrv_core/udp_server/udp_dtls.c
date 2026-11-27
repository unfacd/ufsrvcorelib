/**
 * @file udp_dtls.c
 * @brief Shard-owned DTLS association admission and record processing.

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

#include <openssl/err.h>

#include <ufsrv_core/udp_server/udp_dtls_bio.h>

#include "udp_server_priv.h"

static ssize_t
SendRecord(void *context, const struct sockaddr *peer, socklen_t peer_len, const void *data, size_t length)
{
  UdpShard *shard = context;
  return sendto(shard->fd, data, length, 0, peer, peer_len);
}

static UdpFlow *
AdmitAssociation(UdpServer *server, UdpShard *shard, size_t length)
{
  SSL *ssl = SSL_new(server->dtls_context);
  if (!ssl) return NULL;

  BIO *bio = UdpDtlsBioCreate((const struct sockaddr *)&shard->peer, shard->peer_len, SendRecord, shard);
  if (!bio) {
    SSL_free(ssl);
    return NULL;
  }
  SSL_set_bio(ssl, bio, bio);

  BIO_ADDR *peer = BIO_ADDR_new();
  ERR_clear_error();
  const bool verified = peer && UdpDtlsBioFeed(bio, shard->input, length) && DTLSv1_listen(ssl, peer) > 0;
  BIO_ADDR_free(peer);

  bool retained = false;
  if (verified) {
    pthread_mutex_lock(&shard->mutex);
    retained = atomic_load(&shard->up) && UdpFlowResolve(server, shard, ssl, &shard->flow);
    pthread_mutex_unlock(&shard->mutex);
  }
  if (retained) return &shard->flows[shard->flow.slot];
  SSL_free(ssl);
  return NULL;
}

static bool
WantRead(SSL *ssl, int result)
{
  return SSL_get_error(ssl, result) == SSL_ERROR_WANT_READ;
}

static bool
TouchAssociation(UdpServer *server, UdpShard *shard)
{
  pthread_mutex_lock(&shard->mutex);
  const bool active = UdpFlowTouch(server, shard, shard->flow);
  pthread_mutex_unlock(&shard->mutex);
  return active;
}

void
UdpShardProcessDtls(UdpServer *server, UdpShard *shard, size_t length)
{
  UdpFlow *flow = UdpFlowFindDtls(server, shard);
  if (!flow) {
    flow = AdmitAssociation(server, shard, length);
    if (!flow) return;
  } else if (!UdpDtlsBioFeed(SSL_get_rbio(flow->dtls), shard->input, length)) {
    return;
  }

  SSL *ssl = flow->dtls;
  if (!SSL_is_init_finished(ssl)) {
    ERR_clear_error();
    const int result = SSL_accept(ssl);
    if (result != 1) {
      if (!WantRead(ssl, result)) UdpFlowClose(shard, flow);
      return;
    }
    if (!TouchAssociation(server, shard)) return;
  }

  while (atomic_load(&shard->up)) {
    ERR_clear_error();
    const int bytes = SSL_read(ssl, shard->input, sizeof(shard->input));
    if (bytes <= 0) {
      if (!WantRead(ssl, bytes)) UdpFlowClose(shard, flow);
      return;
    }
    if (!TouchAssociation(server, shard)) return;
    UdpShardDispatch(server, shard, (size_t)bytes);
    if (flow->closed) return;
  }
}

void
UdpShardSendFlow(UdpShard *shard, UdpFlow *flow, const void *payload, size_t length)
{
  if (!flow->dtls) {
    SendRecord(shard, (const struct sockaddr *)&flow->peer, flow->peer_len, payload, length);
    return;
  }
  if (!SSL_is_init_finished(flow->dtls) || length > DTLS_get_data_mtu(flow->dtls)) return;

  ERR_clear_error();
  // An incomplete SSL_write would require retrying the same buffer.
  // This best-effort queue releases each buffer, so retire that association.
  if (SSL_write(flow->dtls, payload, (int)length) != (int)length)
    UdpFlowClose(shard, flow);
}
