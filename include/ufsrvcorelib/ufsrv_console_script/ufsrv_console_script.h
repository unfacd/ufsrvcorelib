/**
 * @file ufsrv_console_script.h
 * @brief command-console dynamic registration + dispatch API
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

#ifndef UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_H
#define UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_H

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <ufsrvcorelib/ufsrv_console_script/ufsrv_console_script_type.h>

/**
 * @brief Register a handler for a command path.
 *
 * Adds (or, on a repeated path, replaces) the handler for @p path. Intermediate
 * group nodes are materialised automatically; group vs leaf is derived from the
 * final tree (a node with children is a group).
 *
 * @param path        absolute, '/' separated, e.g. "/internals/config/runtime"
 * @param description short human description; copied (owned by the library)
 * @param handler     callbacks + method bitmask; copied
 * @return 0 on success, non-zero on error (e.g. malformed path)
 *
 * @code{.c}
 * UfsrvConsoleScriptHandler h = {
 *     .on_get  = runtime_on_get,
 *     .methods = UFSRV_CONSOLE_METHOD_GET,
 * };
 * UfsrvConsoleScriptRegister("/internals/config/runtime", "runtime config", &h);
 * @endcode
 */
PUBLIC_API int
UfsrvConsoleScriptRegister(const char *path, const char *description, const UfsrvConsoleScriptHandler *handler);

/**
 * @brief Resolve and dispatch a `METHOD node_path[:pipeline]` command.
 *
 * Looks up @p path in the registered tree, dispatches GET/DEL/PUT to the node's
 * callback, and EVAL / the `->eval(...)` pipeline to the Lua engine. The
 * `:pipeline` suffix (JSON path + `->eval`) is parsed by the library.
 *
 * @param method "GET", "DEL"/"DELETE", "PUT", or "EVAL" (case-insensitive)
 * @param path   node path, optionally suffixed with ":<pipeline>"
 * @param buffer caller-provided BufferDescriptor; may be NULL
 * @return the JSON BufferDescriptor, or NULL on unknown path / unsupported method
 */
PUBLIC_API BufferDescriptor *
UfsrvConsoleScriptResolve(const char *method, const char *path, BufferDescriptor *buffer);

/**
 * @brief Free the registered command tree.
 *
 * Tears down every registered node (segments, descriptions, and child arrays),
 * resetting the tree to empty so registration can start over. The root is static
 * and is not freed. Not thread-safe with concurrent dispatch/registration.
 */
PUBLIC_API void
UfsrvConsoleScriptDestroy(void);

#endif /* UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_H */
