/**
 * @file rest_server.h
 * @brief Stateless REST workers. One SO_REUSEPORT thread per worker, no delegator.
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

#ifndef UFSRVCORELIB_REST_SERVER_H
#define UFSRVCORELIB_REST_SERVER_H

#include <stddef.h>

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <ufsrv_core/protocol/protocol_type.h>
#include <ufsrv_core/rest_server/rest_session_type.h>

typedef struct UfsrvRestServer UfsrvRestServer;

/**
 * Build one worker's session: the composed HttpSession plus the UfsrvSession
 * it hangs off. The json object is allocated here once and emptied between
 * requests. False leaves nothing allocated.
 */
PUBLIC_API bool RestSessionInit(RestSession *session, const Protocol *protocol, size_t blocksz,
                                size_t max_post_size, size_t max_file_size);

/** Release a session built by RestSessionInit. NULL is safe. */
PUBLIC_API void RestSessionDeinit(RestSession *session);

/**
 * Application seam. One RestSession per worker, allocated by protocol init
 * and kept for the life of that thread. The worker resets the request,
 * response, and json between connections. bind_host and sessions outlive
 * the server.
 */
typedef struct UfsrvRestServerSpecs {
  size_t worker_count;       ///< SO_REUSEPORT threads. One is enough.
  const char *bind_host;     ///< Numeric host.
  unsigned port;             ///< 0 lets the kernel pick the port.
  const Protocol *protocol;  ///< msg_callback reads the accepted socket.
  RestSession *sessions;     ///< worker_count objects. Index i belongs to worker i.
} UfsrvRestServerSpecs;

/**
 * Bind one listen socket per worker. No thread is created.
 * NULL when the spec is unusable or a socket cannot be bound; nothing is left allocated.
 *
 * @code{.c}
 * UfsrvRestServerSpecs specs = {
 *   .worker_count = 2, .bind_host = "127.0.0.1", .port = 19702,
 *   .protocol = protocol, .sessions = sessions,
 * };
 * UfsrvRestServer *server = UfsrvRestServerOnboard(&specs);
 * UfsrvRestServerStart(server);
 * @endcode
 */
PUBLIC_API UfsrvRestServer *UfsrvRestServerOnboard(const UfsrvRestServerSpecs *specs);

/**
 * Onboard an HTTPS listener. The certificate chain is read from a PEM file
 * and the private key is parsed from unencrypted PEM in memory. Both are
 * loaded during this call. NULL indicates credential or listener failure.
 */
PUBLIC_API UfsrvRestServer *UfsrvRestServerOnboardTls(const UfsrvRestServerSpecs *specs,
                                                   const char *certificate_file, const char *private_key_pem);

/** Start the worker threads. False when a thread cannot be created; threads already started are joined. */
PUBLIC_API bool UfsrvRestServerStart(UfsrvRestServer *server);

/** Wake the workers, join them, and release the server. NULL is safe. */
PUBLIC_API void UfsrvRestServerStop(UfsrvRestServer *server);

/** The port the sockets bound. 0 when server is NULL. */
PUBLIC_API unsigned UfsrvRestServerPort(const UfsrvRestServer *server);

#endif
