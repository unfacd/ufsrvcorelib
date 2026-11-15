/**
 * @file ufsrv_console_script_priv.h
 * @brief internal command-console EVAL interface (not installed)
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

#ifndef UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_PRIV_H
#define UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_PRIV_H

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>

//Internal EVAL entry points — implemented in ufsrv_console_script_eval.c,
//called by the resolver in ufsrv_console_script.c. Not part of the public API.

//Run a standalone Lua script (the EVAL method): @p script is the rest of the line.
BufferDescriptor *UfsrvConsoleScriptEvalLua(const char *script, BufferDescriptor *buffer);

//Evaluate a "jsonpath? ->eval(lua_body)" pipeline against @p node_output.
//@p buffer is truncated and rewritten with the Lua result.
BufferDescriptor *UfsrvConsoleScriptEvalPipeline(const char *pipeline, const char *node_output, BufferDescriptor *buffer);

//Select @p jsonpath from @p node_output (no Lua). @p buffer is truncated and
//rewritten with the selected value, or an {"error":...} on a bad path.
BufferDescriptor *UfsrvConsoleScriptSelectPath(const char *jsonpath, const char *node_output, BufferDescriptor *buffer);

#endif /* UFSRVCORELIB_UFSRV_CONSOLE_SCRIPT_PRIV_H */
