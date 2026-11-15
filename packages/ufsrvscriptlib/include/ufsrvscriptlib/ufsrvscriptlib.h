/**
 * @file ufsrvscriptlib.h
 * @brief Umbrella header for the ufsrvscriptlib library.
 *
 * ufsrvscriptlib holds the reusable, class-agnostic Lua scripting engine: the
 * bits that let an application expose its operations to Lua scripts. It is a
 * thin layer over the system Lua 5.4 (liblua) and does not depend on any
 * ufsrv service module. The application supplies its own NULL-sentinel
 * UfsrvScriptFunctionBinding array at bootstrap; those bindings live in the host
 * application, not here.
 */
#ifndef UFSRVSCRIPTLIB_H
#define UFSRVSCRIPTLIB_H

#include <uflib/uflib_defs.h>

#include <ufsrvscriptlib/ufsrvscriptlib_lua.h>
#include <ufsrvscriptlib/ufsrvscriptlib_eval.h>
#include <ufsrvscriptlib/ufsrvscriptlib_version.h>

#endif /* UFSRVSCRIPTLIB_H */
