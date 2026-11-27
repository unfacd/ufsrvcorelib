/**
 * @file session_delegator_command_handler.h
 * @brief Delegator-side route table and dispatch for the /internals/delegator stem.
 *
 * The console resolves the stem and calls the delegator through the command
 * channel. The body that runs on the delegator thread hands the command here,
 * where a ResourceRouter registry matches it against the routes declared in
 * data_session_delegator_command_handler_table.h.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufnetcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFNETCORELIB_SESSION_DELEGATOR_COMMAND_HANDLER_H
#define UFNETCORELIB_SESSION_DELEGATOR_COMMAND_HANDLER_H

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>
#include <ufsrvcorelib/ufsrv_console_script/ufsrv_console_script_type.h>
#include <ufsrvcorelib_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the delegator's route registry and load its table.
 *
 * Idempotent: a second call leaves the existing registry in place.
 *
 * @return 0 when the registry exists and every route was registered.
 * @return -1 when allocation failed or a table row was rejected; no registry is left behind.
 *
 * @code{.c}
 * if (SessionDelegatorCommandHandlerInit() != 0)
 *   syslog(LOG_ERR, "delegator command routes unavailable");
 * @endcode
 */
PUBLIC_API int SessionDelegatorCommandHandlerInit(void);

/**
 * @brief Release the delegator's route registry.
 *
 * The delegator thread runs for the process lifetime, so this exists for
 * shutdown paths and for tests. A second call, or a call before Init, is a no-op.
 *
 * @code{.c}
 * SessionDelegatorCommandHandlerDestroy();
 * @endcode
 */
PUBLIC_API void SessionDelegatorCommandHandlerDestroy(void);

/**
 * @brief Dispatch one command received on the delegator channel.
 *
 * Runs on the delegator thread. The reply is appended to @p reply as JSON: a
 * matched route writes its own document, otherwise the reply carries an
 * @c error field naming why no route ran.
 *
 * @param[in]     method  Verb as received from the console: GET, DEL or PUT.
 * @param[in]     path    Tail after the /internals/delegator stem. May be empty.
 * @param[in,out] reply   The command's reply buffer. Left empty when an argument is NULL.
 *
 * @code{.c}
 * sExecuteConsoleDelegatorCommand(const char *method, const char *path, BufferDescriptor *reply)
 * {
 *   SessionDelegatorCommandHandlerExecute(method, path, reply);
 * }
 * @endcode
 */
PUBLIC_API void SessionDelegatorCommandHandlerExecute(const char *method, const char *path, BufferDescriptor *reply);

/**
 * @brief Fill a console handler descriptor for the /internals/delegator stem.
 *
 * The console-side adapter. Each callback posts the command's tail onto the
 * delegator channel and blocks until the delegator thread has appended its
 * reply, so @c SessionDelegatorCommandHandlerExecute runs on the delegator
 * thread, never on the console thread.
 *
 * @param[in,out] handler_provided  Descriptor to fill. When NULL, one is allocated.
 *
 * @return The filled descriptor, or NULL when allocation failed.
 *
 * @code{.c}
 * UfsrvConsoleScriptHandler h = {0};
 * UfsrvConsoleScriptRegister("/internals/delegator", "Session Delegator signaling",
 *                            SessionDelegatorCommandProvideHandler(&h));
 * @endcode
 */
PUBLIC_API UfsrvConsoleScriptHandler *SessionDelegatorCommandProvideHandler(UfsrvConsoleScriptHandler *handler_provided);

#ifdef __cplusplus
}
#endif

#endif
