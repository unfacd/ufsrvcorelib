/**
 * @file rest_worker_thread.c
 * @brief One REST worker: accept on its own socket, reset its RestSession, read.
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
#include <errno.h>
#include <stdint.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <json-c/linkhash.h>
#include <json-c/json_object.h>
#include <uflib/net/socket_type.h>
#include <uflib/recycler/instance_type.h>
#include <ufsrvcorelib/ufsrv_core/http/request.h>
#include <ufsrvcorelib/ufsrv_core/protocol/protocol.h>
#include <ufsrvcorelib/ufsrv_core/session/session_io.h>
#include <ufsrvcorelib/ufsrv_core/session/sesstion_stat_flags.h>
#include <ufsrvcorelib/ufsrv_core/session/ufsrv_session_type.h>
#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>
#include <ufsrvcorelib/ufsrv_core/ufsrvresult_type.h>

#include "rest_server_priv.h"
#include "ufsrv_core/http/dictionary.h"

enum { REST_WORKER_POLL_MS = 200, REST_TLS_IO_TIMEOUT_SECONDS = 10 };

static void
sResetExchange(RestSession *session)
{
  HttpSession *http = &session->http;

  if (HttpSessionGetRequest(http)->headers) onion_request_clean(HttpSessionGetRequest(http));
  HttpSessionClearResponse(http);
  HttpSessionClearJson(http);
  HttpSessionCloseSendFile(http);
}

static void
sNotePeer(UfsrvSession *session, int fd, const struct sockaddr_storage *peer, const char *bind_host, unsigned port)
{
  session->ssptr->sock = fd;
  session->ssptr->port = port;
  if (bind_host) snprintf(session->ssptr->address, sizeof(session->ssptr->address), "%s", bind_host);

  if (peer->ss_family == AF_INET) {
    const struct sockaddr_in *address = (const struct sockaddr_in *)peer;
    session->ssptr->hport = ntohs(address->sin_port);
    inet_ntop(AF_INET, &address->sin_addr, session->ssptr->haddress, sizeof(session->ssptr->haddress));
  } else if (peer->ss_family == AF_INET6) {
    const struct sockaddr_in6 *address = (const struct sockaddr_in6 *)peer;
    session->ssptr->hport = ntohs(address->sin6_port);
    inet_ntop(AF_INET6, &address->sin6_addr, session->ssptr->haddress, sizeof(session->ssptr->haddress));
  }

  SESNSTATUS_SET(session->stat, SESNSTATUS_CONNECTED);
}

static void
sServiceConnection(UfsrvRestWorkerContext *worker, int fd, const struct sockaddr_storage *peer)
{
  UfsrvRestServer *server = worker->server;
  RestSession *session = worker->session;
  UfsrvSession *ufsrv = &session->ufsrv_session;

  pthread_mutex_lock(&server->connection_mutex);
  if (atomic_load(&server->stop)) {
    pthread_mutex_unlock(&server->connection_mutex);
    close(fd);
    return;
  }
  worker->active_fd = fd;
  pthread_mutex_unlock(&server->connection_mutex);

  SSL *tls = NULL;
  if (server->tls) {
    const struct timeval timeout = {.tv_sec = REST_TLS_IO_TIMEOUT_SECONDS};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) != 0) goto finish;

    tls = SSL_new(server->tls);
    if (!tls || SSL_set_fd(tls, fd) != 1 || SSL_accept(tls) != 1) goto finish;
  }

  sResetExchange(session);
  sNotePeer(ufsrv, fd, peer, server->bind_host, server->port);
  SESNSTATUS_SET(ufsrv->stat, SESNSTATUS_WORKER_OWNS_SOCKET);
  if (tls) {
    ufsrv->session_crypto.ssl = tls;
    SESNSTATUS_SET(ufsrv->stat, SESNSTATUS_SECURE);
  }

  while (!atomic_load(&server->stop)) {
    UFSRVResult *result = ProtocolInvokeMsgCallback(server->protocol->protocol_id, &worker->holder,
                                                    UFSRV_SESSION_INSOCKMSG_TRANS_PTR(ufsrv), SOCKMSG_READSOCKET, 0);
    if (!result || result->result_type != RESULT_TYPE_SUCCESS ||
        SESNSTATUS_IS_SET(ufsrv->stat, SESNSTATUS_SUSPENDED | SESNSTATUS_IOERROR)) break;
    if (result->result_code == RESCODE_IO_FRAGMENTATION) continue;
    if (result->result_code == RESCODE_IO_MSGPARSED && (intptr_t)result->result_user_data == OCS_KEEP_ALIVE) {
      sResetExchange(session);
      continue;
    }
    break;
  }

  if (tls && !atomic_load(&server->stop) &&
      !SESNSTATUS_IS_SET(ufsrv->stat, SESNSTATUS_SUSPENDED | SESNSTATUS_IOERROR)) SSL_shutdown(tls);
  ufsrv->session_crypto.ssl = NULL;
  ufsrv->stat = 0;
  ufsrv->ssptr->sock = -1;

finish:
  SSL_free(tls);
  pthread_mutex_lock(&server->connection_mutex);
  worker->active_fd = -1;
  close(fd);
  pthread_mutex_unlock(&server->connection_mutex);
}

void *
RestWorkerThread(void *arg)
{
  UfsrvRestWorkerContext *worker = arg;
  UfsrvRestServer *server = worker->server;

  SetInstance(&worker->holder, &worker->session->ufsrv_session);

  if (server->tls) {
    // OpenSSL's socket BIO can raise SIGPIPE if the peer has closed.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signals, NULL);
  }

  char proc_name[16];
  strncpy(proc_name, "ufRestWorker", sizeof(proc_name) - 1);
  proc_name[sizeof(proc_name) - 1] = 0;
  prctl(PR_SET_NAME, (unsigned long)proc_name);
  UfsrvCpuAffinityPin(UFSRV_THREAD_WORKER, (int)worker->index);

  struct pollfd poll_fd = {.fd = worker->listen_fd, .events = POLLIN};
  while (!atomic_load(&server->stop)) {
    int ready = poll(&poll_fd, 1, REST_WORKER_POLL_MS);
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0) continue;
    if (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;

    struct sockaddr_storage peer;
    socklen_t peer_length = sizeof(peer);
    int fd = accept(worker->listen_fd, (struct sockaddr *)&peer, &peer_length);
    if (fd < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
      break;
    }

    sServiceConnection(worker, fd, &peer);
  }

  return NULL;
}
