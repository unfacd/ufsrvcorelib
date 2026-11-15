/**
 * @file ufsrvscriptlib_eval.h
 * @brief One-shot and persistent Lua evaluator convenience API.
 *
 * A thin convenience layer over the ufsrvscriptlib engine that answers "evaluate
 * this Lua string and hand me the result" — the shape the server's console EVAL
 * (`UfsrvConsoleScriptEvalLua`) already uses, but backed by the *sandboxed*
 * engine rather than a raw `luaL_openlibs` state.
 *
 * The result is marshalled into a uflib `BufferDescriptor` using the same
 * JSON-ish convention as the server console: the script's single return value
 * is written as `nil` → `"null"`, boolean → `"true"`/`"false"`, a number or
 * string via `lua_tostring`, and any other value (table/function/userdata) as
 * an empty string (`lua_tostring` returns NULL). A load/runtime failure instead
 * writes `{"error":"…"}`. The return format is deliberately minimal and lossy;
 * scripts that need structured output should call the `json`/`serpent` modules
 * themselves.
 *
 * Two usage modes:
 *  - **one-shot** — UfsrvScriptEval() bootstraps a fresh sandboxed VM, runs the
 *    script, marshals the result, and closes the VM. No state persists.
 *  - **persistent** — UfsrvScriptEvalOpen() returns a caller-owned VM kept
 *    across calls; UfsrvScriptEvalRun() loads+evals into it (globals and any
 *    loaded `protoc` schemas persist), and UfsrvScriptLuaClose() tears it down.
 *
 * This adds a uflib BufferDescriptor dependency to the public surface (uflib is
 * already a dependency for PUBLIC_API), isolated here so the core
 * ufsrvscriptlib_lua.h stays buffer-free.
 */
#ifndef UFSRVSCRIPTLIB_EVAL_H
#define UFSRVSCRIPTLIB_EVAL_H

#include <uflib/uflib_defs.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include <ufsrvscriptlib/ufsrvscriptlib_lua.h>

/**
 * @brief Evaluate a Lua script in a fresh sandboxed VM and return its result as
 *        a JSON-ish BufferDescriptor.
 *
 * Bootstraps a VM with the default sandbox policy (strict globals on,
 * `os`/`package` off), loads and runs @p script, marshals the single return
 * value into @p buffer, then closes the VM. On a load/runtime error the buffer
 * holds `{"error":"…"}`.
 *
 * @param[in]  script  NUL-terminated Lua source. NULL is treated as an error.
 * @param[in,out] buffer  Caller-provided BufferDescriptor, or NULL. When NULL a
 *                        fresh descriptor (and its backing store) is allocated
 *                        and returned; the caller frees it with
 *                        BufferDescriptorRelease() and then free(). When
 *                        non-NULL it must be BufferDescriptorInit'd and is
 *                        truncated and reused.
 * @return The BufferDescriptor holding the result, or NULL if no buffer could
 *         be produced (out of memory).
 */
PUBLIC_API BufferDescriptor *
UfsrvScriptEval(const char *script, BufferDescriptor *buffer);

/**
 * @brief Create a persistent, caller-owned sandboxed VM for repeated eval.
 *
 * @param[in] opts  Sandbox policy; NULL selects the defaults (strict globals on,
 *                  `os`/`package` off).
 * @return A UfsrvScriptLua handle for use with UfsrvScriptEvalRun(), or NULL on
 *         allocation failure.
 */
PUBLIC_API UfsrvScriptLua *
UfsrvScriptEvalOpen(const UfsrvScriptLuaOptions *opts);

/**
 * @brief Load and run a Lua script in a persistent VM, returning the marshalled
 *        result.
 *
 * Globals (and any state such as loaded protobuf schemas) persist across calls
 * on the same handle. The VM's stack is left clean after each call.
 *
 * @param[in]  lua     Handle from UfsrvScriptEvalOpen(). NULL is treated as an
 *                     error.
 * @param[in]  script  NUL-terminated Lua source. NULL is treated as an error.
 * @param[in,out] buffer  As in UfsrvScriptEval().
 * @return The BufferDescriptor holding the result, or NULL on failure.
 */
PUBLIC_API BufferDescriptor *
UfsrvScriptEvalRun(UfsrvScriptLua *lua, const char *script, BufferDescriptor *buffer);

#endif /* UFSRVSCRIPTLIB_EVAL_H */
