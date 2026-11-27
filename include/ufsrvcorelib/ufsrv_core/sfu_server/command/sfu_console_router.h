/**
 * @file sfu_console_router.h
 * @brief The SFU server's console sub-router: /server/<route>.
 *
 * The host registers one stemmed console handler at /server and forwards the
 * remainder here, the same way the sessions delegator owns everything past
 * /internals/delegator and resolves it through its own resource router.
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

#ifndef UFSRVCORELIB_SFU_CONSOLE_ROUTER_H
#define UFSRVCORELIB_SFU_CONSOLE_ROUTER_H

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>

#include <ufsrvcorelib/ufsrv_core/sfu_server/sfu_server.h>

/**
 * @brief Build the route registry and bind it to one running server.
 *
 * @param[in] server  Started server. The registry is stored on this instance, so
 *                    each server owns its own routes and none is process-global.
 *
 * @return 0 when the registry is ready, -1 when server is NULL or a route
 *         could not be registered. Calling it twice returns 0.
 */
PUBLIC_API int SfuCommandConsoleAttachResourceRouter(SfuServer *server);

/**
 * @brief Release this instance's registry. NULL-safe, idempotent.
 *
 * SfuServerStop() calls it, so a host that tears the server down does not need
 * to call it separately.
 */
PUBLIC_API void SfuCommandConsoleDetachResourceRouter(SfuServer *server);

/**
 * @brief Resolve one /server/<path> console command.
 *
 * @param[in]     method  "GET", "DEL" or "PUT".
 * @param[in]     path    Remainder past the /server stem, with or without a
 *                        leading or trailing slash.
 * @param[in,out] reply   Console reply buffer. Never NULL in practice.
 *
 * @return reply, with an {"error":...} object when nothing matched.
 *
 * @code{.c}
 * UfsrvConsoleScriptRegister("/server", "sfu server shards", &handler); // is_stemmed = true
 * // handler: SfuConsoleRouterDispatch(server, method, ctx->stemmed_path, ctx->buffer)
 * @endcode
 */
PUBLIC_API BufferDescriptor *SfuCommandConsoleRouteResource(SfuServer *server, const char *method, const char *path,
                                                      BufferDescriptor *reply);

#endif /* UFSRVCORELIB_SFU_CONSOLE_ROUTER_H */
