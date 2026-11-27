/**
 * @file rest_server.c
 * @brief Turn UfsrvRestServerSpecs into listen sockets and worker threads.
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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <json-c/json_object.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include "rest_server_priv.h"

static bool
sInitQueue(MessageQueue *queue)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
  int rc = pthread_mutex_init(&queue->mutex, &attr);
  pthread_mutexattr_destroy(&attr);

  return rc == 0;
}

bool
RestSessionInit(RestSession *session, const Protocol *protocol, size_t blocksz,
                size_t max_post_size, size_t max_file_size)
{
  if (!session || !protocol) return false;

  memset(session, 0, sizeof(*session));

  if (!HttpSessionInit(&session->http, sizeof(session->http), max_post_size, max_file_size)) return false;

  HttpSessionSetJson(&session->http, json_object_new_object());
  session->ufsrv_session.ssptr = calloc(1, sizeof(*session->ufsrv_session.ssptr));
  if (!HttpSessionGetJson(&session->http) || !session->ufsrv_session.ssptr) goto return_error;

  if (!sInitQueue(&session->ufsrv_session.message_queue_in)) goto return_error_ssptr;
  if (!sInitQueue(&session->ufsrv_session.message_queue_out)) goto return_error_in_queue;

  session->ufsrv_session.ssptr->sock = -1;
  session->ufsrv_session.ssptr->blocksz = blocksz;
  session->ufsrv_session.protocol_type_data = (ProtocolTypeData *)protocol;
  session->ufsrv_session.protocol_session_data = (ProtocolSessionData *)&session->http;

  return true;

  return_error_in_queue:
  pthread_mutex_destroy(&session->ufsrv_session.message_queue_in.mutex);
  return_error_ssptr:
  free(session->ufsrv_session.ssptr);
  return_error:
  HttpSessionDeinit(&session->http);
  memset(session, 0, sizeof(*session));
  return false;
}

void
RestSessionDeinit(RestSession *session)
{
  if (!session) return;

  pthread_mutex_destroy(&session->ufsrv_session.message_queue_in.mutex);
  pthread_mutex_destroy(&session->ufsrv_session.message_queue_out.mutex);
  free(session->ufsrv_session.ssptr);
  HttpSessionDeinit(&session->http);
  memset(session, 0, sizeof(*session));
}

static void
sCloseFds(UfsrvRestServer *server)
{
  if (!server->listen_fds) return;
  for (size_t i = 0; i < server->worker_count; ++i) {
    if (server->listen_fds[i] < 0) continue;
    close(server->listen_fds[i]);
    server->listen_fds[i] = -1;
  }
}

static void
sWake(UfsrvRestServer *server)
{
  atomic_store(&server->stop, true);
  pthread_mutex_lock(&server->connection_mutex);
  for (size_t i = 0; i < server->worker_count; ++i) {
    if (server->listen_fds[i] >= 0) shutdown(server->listen_fds[i], SHUT_RDWR);
    if (server->worker_contexts[i].active_fd >= 0) shutdown(server->worker_contexts[i].active_fd, SHUT_RDWR);
  }
  pthread_mutex_unlock(&server->connection_mutex);
}

static unsigned
sBoundPort(int fd)
{
  struct sockaddr_storage address;
  socklen_t length = sizeof(address);
  if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) return 0;
  if (address.ss_family == AF_INET) return ntohs(((struct sockaddr_in *)&address)->sin_port);
  if (address.ss_family == AF_INET6) return ntohs(((struct sockaddr_in6 *)&address)->sin6_port);
  return 0;
}

static int
sListen(const char *host, unsigned port)
{
  char service[6];
  snprintf(service, sizeof(service), "%u", port);

  struct addrinfo hints = {
    .ai_family = AF_UNSPEC,
    .ai_socktype = SOCK_STREAM,
    .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV,
  };
  struct addrinfo *addresses = NULL;
  if (getaddrinfo(host, service, &hints, &addresses) != 0) return -1;

  int fd = -1;
  for (const struct addrinfo *address = addresses; address; address = address->ai_next) {
    fd = socket(address->ai_family, SOCK_STREAM | SOCK_CLOEXEC, address->ai_protocol);
    if (fd < 0) continue;

    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled)) != 0 ||
        bind(fd, address->ai_addr, address->ai_addrlen) != 0 ||
        listen(fd, SOMAXCONN) != 0) {
      close(fd);
      fd = -1;
      continue;
    }
    break;
  }

  freeaddrinfo(addresses);
  return fd;
}

static bool
sSpecsUsable(const UfsrvRestServerSpecs *specs)
{
  if (!specs || !specs->worker_count || !specs->bind_host || !*specs->bind_host || specs->port > 65535) return false;
  if (!specs->protocol || !specs->sessions) return false;
  return true;
}

static UfsrvRestServer *
sOnboard(const UfsrvRestServerSpecs *specs, SSL_CTX *tls)
{
  UfsrvRestServer *server = calloc(1, sizeof(*server));
  if (!server || pthread_mutex_init(&server->connection_mutex, NULL) != 0) {
    SSL_CTX_free(tls);
    free(server);
    return NULL;
  }
  server->tls = tls;

  server->protocol = specs->protocol;
  server->bind_host = specs->bind_host;
  server->worker_count = specs->worker_count;
  atomic_init(&server->stop, false);

  server->listen_fds = malloc(specs->worker_count * sizeof(*server->listen_fds));
  if (server->listen_fds) {
    for (size_t i = 0; i < specs->worker_count; ++i) server->listen_fds[i] = -1;
  }
  server->threads = calloc(specs->worker_count, sizeof(*server->threads));
  server->worker_contexts = calloc(specs->worker_count, sizeof(*server->worker_contexts));
  if (!server->listen_fds || !server->threads || !server->worker_contexts) {
    UfsrvRestServerStop(server);
    return NULL;
  }

  unsigned port = specs->port;
  for (size_t i = 0; i < specs->worker_count; ++i) {
    int fd = sListen(specs->bind_host, port);
    if (fd < 0) {
      syslog(LOG_ERR, "%s: could not listen on %s:%u", __func__, specs->bind_host, port);
      UfsrvRestServerStop(server);
      return NULL;
    }
    if (!port) {
      port = sBoundPort(fd);
      if (!port) {
        close(fd);
        UfsrvRestServerStop(server);
        return NULL;
      }
    }
    server->listen_fds[i] = fd;
    server->worker_contexts[i].server = server;
    server->worker_contexts[i].index = i;
    server->worker_contexts[i].listen_fd = fd;
    server->worker_contexts[i].active_fd = -1;
    server->worker_contexts[i].session = &specs->sessions[i];
  }

  server->port = port;
  return server;
}

UfsrvRestServer *
UfsrvRestServerOnboard(const UfsrvRestServerSpecs *specs)
{
  return sSpecsUsable(specs) ? sOnboard(specs, NULL) : NULL;
}

static int
sNoPassword(char *buffer, int size, int writing, void *data)
{
  (void)buffer;
  (void)size;
  (void)writing;
  (void)data;
  return 0;
}

UfsrvRestServer *
UfsrvRestServerOnboardTls(const UfsrvRestServerSpecs *specs, const char *certificate_file, const char *private_key_pem)
{
  if (!sSpecsUsable(specs) || !certificate_file || !*certificate_file || !private_key_pem || !*private_key_pem)
    return NULL;

  ERR_clear_error();
  SSL_CTX *tls = SSL_CTX_new(TLS_server_method());
  BIO *pem = BIO_new_mem_buf(private_key_pem, -1);
  EVP_PKEY *key = pem ? PEM_read_bio_PrivateKey(pem, NULL, sNoPassword, NULL) : NULL;
  bool loaded = tls && key && SSL_CTX_set_min_proto_version(tls, TLS1_2_VERSION) == 1 &&
                SSL_CTX_use_certificate_chain_file(tls, certificate_file) == 1 &&
                SSL_CTX_use_PrivateKey(tls, key) == 1 && SSL_CTX_check_private_key(tls) == 1;
  EVP_PKEY_free(key);
  BIO_free(pem);
  if (!loaded) {
    char error[256];
    ERR_error_string_n(ERR_peek_last_error(), error, sizeof error);
    syslog(LOG_ERR, "%s: could not load REST TLS credentials: %s", __func__, error);
    SSL_CTX_free(tls);
    return NULL;
  }

  return sOnboard(specs, tls);
}

bool
UfsrvRestServerStart(UfsrvRestServer *server)
{
  if (!server || server->started || atomic_load(&server->stop)) return false;

  for (size_t i = 0; i < server->worker_count; ++i) {
    if (pthread_create(&server->threads[i], NULL, RestWorkerThread, &server->worker_contexts[i]) != 0) {
      syslog(LOG_ERR, "%s: could not start worker %zu", __func__, i);
      sWake(server);
      for (size_t joined = 0; joined < i; ++joined) pthread_join(server->threads[joined], NULL);
      sCloseFds(server);
      atomic_store(&server->stop, true);
      return false;
    }
  }

  server->started = true;
  return true;
}

void
UfsrvRestServerStop(UfsrvRestServer *server)
{
  if (!server) return;

  if (server->started) {
    sWake(server);
    for (size_t i = 0; i < server->worker_count; ++i) pthread_join(server->threads[i], NULL);
    server->started = false;
  }

  sCloseFds(server);
  free(server->listen_fds);
  free(server->threads);
  free(server->worker_contexts);
  SSL_CTX_free(server->tls);
  pthread_mutex_destroy(&server->connection_mutex);
  free(server);
}

unsigned
UfsrvRestServerPort(const UfsrvRestServer *server)
{
  return server ? server->port : 0;
}
