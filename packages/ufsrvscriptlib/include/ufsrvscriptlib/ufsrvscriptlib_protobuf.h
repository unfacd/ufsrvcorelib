/**
 * @file ufsrvscriptlib_protobuf.h
 * @brief Native Lua bindings for Protocol Buffers — the ufsrvscriptlib interface
 *        seam over the vendored lua-protobuf core.
 *
 * Declares UfsrvScriptProtobufRegister(), which builds the `pb`, `protoc` and
 * `serpent` globals over the vendored lua-protobuf core (pb.c/pb.h + the pure-Lua
 * companions, MIT — see
 * dev/technical_designs/UFSRVCORELIB_UFSRVSCRIPT_PROTOBUF_DESIGN.md). Protobuf
 * is a codec-plus-schema-registry, so — like json — there is no
 * UfsrvScriptProtobuf* wrapper API to expose: the seam composes the vendored
 * openers, re-homes them under the uniform globals, and loads the two pure-Lua
 * companions in a private _ENV so their `require "pb"` resolves without opening
 * the sandbox `package` library.
 *
 * This adds no new link dependency: the vendored core links nothing beyond the
 * Lua API and libc (see §3 of the design). The `pb.unsafe` and `pb.io`
 * submodules are deliberately not exposed (see §10 of the design).
 */
#ifndef UFSRVSCRIPTLIB_PROTOBUF_H
#define UFSRVSCRIPTLIB_PROTOBUF_H

#include <uflib/uflib_defs.h>

#include <lua.h>

/**
 * @brief Build the `pb`, `protoc` and `serpent` globals on the given lua_State.
 *
 * Call after UfsrvScriptBootstrap() and before running any script. The module
 * tables are set via lua_setglobal (C-level), which strict.lua exempts; the
 * sandbox `package`/`require` library is left closed.
 */
PUBLIC_API void UfsrvScriptProtobufRegister(lua_State *L);

#endif /* UFSRVSCRIPTLIB_PROTOBUF_H */
