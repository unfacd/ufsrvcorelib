//
// Created by ayman on 5/09/2026.
//

/**
 * @file ufsrvscriptlib_function_binding_type.h
 * @brief Script-callable binding descriptor for the ufsrvscriptlib engine.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcli source code.
 * Created by ayman on 5/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCLI_UFSRVSCRIPTLIB_FUNCTION_BINDING_TYPE_H
#define UFSRVCLI_UFSRVSCRIPTLIB_FUNCTION_BINDING_TYPE_H

#include <lua.h>

/* A script-callable binding. `func` receives its arguments on the Lua stack
 * (index 1..top) and must return the number of results pushed (0 or 1; a value
 * or a `{ err = ... }` table). */
typedef int (*UfsrvScriptFunctionBindingCallback)(lua_State *lua_state_ptr);

/*! A script-callable operation. Each application declares a static,
 *  NULL-sentinel terminated array of these and passes it to
 *  UfsrvScriptBootstrap(). */
typedef struct UfsrvScriptFunctionBinding {
  const char                *name;   ///< name under the `ufsrv` table, e.g. "register"
  int                        arity;  ///< 0 = any (binding validates), >0 exact, <0 min
  UfsrvScriptFunctionBindingCallback func;   ///< the C implementation of the binding
} UfsrvScriptFunctionBinding;

#endif //UFSRVCLI_UFSRVSCRIPTLIB_FUNCTION_BINDING_TYPE_H
