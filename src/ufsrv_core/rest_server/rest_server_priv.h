/**
 * @file rest_server_priv.h
 * @brief REST server object shared by onboarding and the worker threads.
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

#ifndef UFSRVCORELIB_REST_SERVER_PRIV_H
#define UFSRVCORELIB_REST_SERVER_PRIV_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <openssl/ssl.h>

#include <ufsrvcorelib/ufsrv_core/rest_server/rest_server.h>

struct UfsrvRestServer {
  const Protocol *protocol;
  const char *bind_host;
  size_t worker_count;
  unsigned port;
  atomic_bool stop;
  bool started;
  SSL_CTX *tls;
  pthread_mutex_t connection_mutex;
  int *listen_fds;
  pthread_t *threads;
  struct UfsrvRestWorkerContext *worker_contexts;
};

typedef struct UfsrvRestWorkerContext {
  UfsrvRestServer *server;
  size_t index;
  int listen_fd;
  int active_fd; ///< Protected by connection_mutex.
  RestSession *session;
  InstanceHolderForSession holder;
} UfsrvRestWorkerContext;

void *RestWorkerThread(void *arg);

#endif
