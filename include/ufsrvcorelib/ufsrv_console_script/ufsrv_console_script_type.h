/**
 * @file ufsrv_console_script_type.h
 * @brief public command-console handler descriptor type
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

#ifndef UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_TYPE_H
#define UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_TYPE_H

#include <stdbool.h>

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>

typedef void ScriptHandlerUserData;

/*! Command-console method bits.
 *
 *  `UFSRV_CONSOLE_METHOD_EVAL` is the standalone `EVAL <lua>` METHOD token only —
 *  it is *not* a per-node bit; the `->eval` pipeline is implicit on all methods.
 */
typedef enum UfsrvConsoleMethod
{
  UFSRV_CONSOLE_METHOD_GET  = 1u << 0, ///< GET
  UFSRV_CONSOLE_METHOD_DEL  = 1u << 1, ///< DEL
  UFSRV_CONSOLE_METHOD_PUT  = 1u << 2, ///< PUT
  UFSRV_CONSOLE_METHOD_EVAL = 1u << 3  ///< standalone EVAL METHOD token (not a node bit)
} UfsrvConsoleMethod;

typedef struct ScriptHandlerContext
{
  const char *stemmed_path;
  ScriptHandlerUserData *user_data;
  BufferDescriptor *buffer;
} ScriptHandlerContext;

/*! Handler descriptor a module registers for a command path.
 *
 *  Compared with `memcmp` for last-wins / no-op detection, so zero-initialise
 *  before filling: alignment padding and `stemmed_path` must be deterministic.
 */
typedef struct UfsrvConsoleScriptHandler
{
  BufferDescriptor *(*on_get)(ScriptHandlerContext *ctx); ///< GET callback (returns this node's JSON)
  BufferDescriptor *(*on_del)(ScriptHandlerContext *ctx); ///< DEL callback
  BufferDescriptor *(*on_put)(ScriptHandlerContext *ctx); ///< PUT callback
  unsigned int methods; ///< UfsrvConsoleMethod bitmask (GET|DEL|PUT)
  ScriptHandlerUserData *user_data; ///< user data injected into handler as is

  struct
  {
    bool is_stemmed;  ///< short circuit path matching at stem, and pass the rest to the handler
  } options;
} UfsrvConsoleScriptHandler;

#endif /* UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_TYPE_H */
