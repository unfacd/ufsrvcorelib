# ufsrvscriptlib — Protocol Buffers Native Module Design

## Version Management

| Service Name | Date Created | Created By |
|---|---|---|
| ufsrvscriptlib | 2026-09-07 | devops |

| Version | Date Modified | Modified By | Description |
|---|---|---|---|
| 1.0.0 | 2026-09-07 | devops | Initial design — native protobuf codec Lua module (lua-protobuf) for the scripting engine |
| 1.1.0 | 2026-09-07 | devops | Refine the require bridge — load companions in a private _ENV (raw _G view + pb-only require) instead of a transient global shim |
| 1.2.0 | 2026-09-07 | devops | Record the exact baseline commit and per-TU build flags; document the vendor-bump procedure (§12) |

## 1. Purpose

Give `--eval-script` scripts a protobuf **encode/decode** primitive over the
wire format, in the same way `redis` exposes the Redis backends, `mysql` the
MariaDB backend, and `json` a JSON codec. The module is a thin interface seam
over **lua-protobuf** (`pb.c` + `pb.h` + `protoc.lua` + `serpent.lua`, MIT — see
§12 Provenance), adapted to the `ufsrvscriptlib` interface contract established by
the Redis module and codified in
`../../src/lua_bindings/README.md`.

The distinguishing capability lua-protobuf adds over the other modules is
**schema-driven (de)serialisation**:

```lua
-- a schema compiled once, then reused
protoc:load [[ message Person {
   optional string name = 1;
   optional int32  age  = 2;
} ]]

local bytes = assert(pb.encode("Person", { name = "ilse", age = 18 }))
local data  = assert(pb.decode("Person", bytes))
```

It is **not** a backend: there is no `connect()` and no `backend.*.*` constant.
It is a codec-plus-schema-registry, closer in shape to `json` than to
`redis`/`mysql`, but stateful (it holds loaded message descriptors) where `json`
is stateless.

## 2. Architecture — uniform seam over an imported core

The governing constraint is unchanged from the previous modules: **every native
module presents the same interface** — one
`UfsrvScript<Module>Register(lua_State *)` entry, one (or more) global table(s),
one error convention, one layout — while the **lower implementation layer is
imported as-is**, so a future vendor bump drops in by overwriting files, not by
re-merging our edits.

lua-protobuf ships two halves:

| Half | Files | Nature |
|---|---|---|
| C core | `pb.c` (2,158 lines), `pb.h` (1,800 lines) | the wire-format engine + the `pb` Lua module |
| Lua companions | `protoc.lua` (1,193 lines), `serpent.lua` (135 lines) | pure-Lua: `.proto`→binary-schema compiler and table pretty-printer |

The C core is deliberately **single-translation-unit**: `pb.c` begins
`#define PB_STATIC_API` before `#include "pb.h"`, which turns every internal
`pb_*` API in `pb.h` into `static`. The only external symbols the TU exports are
the six Lua openers:

```c
LUALIB_API int luaopen_pb(lua_State *L);         /* the pb module              */
LUALIB_API int luaopen_pb_buffer(lua_State *L);  /* pb.buffer                  */
LUALIB_API int luaopen_pb_slice(lua_State *L);   /* pb.slice                   */
LUALIB_API int luaopen_pb_io(lua_State *L);      /* pb.io                      */
LUALIB_API int luaopen_pb_conv(lua_State *L);    /* pb.conv                    */
LUALIB_API int luaopen_pb_unsafe(lua_State *L);  /* pb.unsafe                  */
```

`luaopen_pb` alone returns only the **core** table (encode/decode/load/types/
fields/type/field/typefmt/enum/defaults/hook/encode_hook/tohex/fromhex/result/
option/state/pack/unpack) plus the `pb.State` metatable; the five submodules are
separate openers that the standard distribution loads through `require
"pb.buffer"` etc. Our seam therefore composes them by hand (see §7.1).

```
protobuf/                       # packages/ufsrvscriptlib/src/lua_bindings/protobuf/
├── ufsrvscriptlib_protobuf.c     # ← OUR seam (uniform interface)
└── lua_protobuf/               # vendored lua-protobuf core (pristine, name-preserved)
    ├── pb.c  pb.h              #   the C core (compiled into ufsrvscriptlib)
    ├── protoc.lua              #   pure-Lua schema compiler (loaded at registration)
    └── serpent.lua             #   pure-Lua pretty-printer (loaded at registration)
# public header: packages/ufsrvscriptlib/include/ufsrvscriptlib/ufsrvscriptlib_protobuf.h
```

## 3. Dependency — none (vendored pure-C core + pure-Lua companions)

lua-protobuf **links no external library**: `pb.c` is pure C over the Lua API,
libc, and `stdio` (`fopen`/`fread`/`fwrite` for the schema-file helpers). Its
includes are `pb.h`, `<lua.h>`, `<lauxlib.h>`, `<stdio.h>`, `<errno.h>`,
`<assert.h>` — nothing outside the engine's existing Lua include path. So, like
`json`, there is **no `find_path`/`find_library` and no vendored static-lib
target**; `pb.c` compiles directly into the `ufsrvscriptlib` static library.

`protoc.lua` and `serpent.lua` are pure-Lua (no C `require` beyond `pb`), and
`serpent` is MIT-licensed in its own right (see §12).

### Exclusions from the vendored bundle

lua-protobuf's repo also carries `test.lua` (test suite) and `luaunit.lua`
(test framework). Both are **excluded** — test-only, not part of the runtime
module. The bundle is the four runtime files above, all MIT.

## 4. Module shape — a codec plus a schema registry

Redis/MySQL are **stateful backend** modules (`connect()` → userdata, result
userdata, `backend.*.*` constants). JSON is a **stateless codec** (two pure
functions + shape sentinels). Protobuf is a third shape:

- **Stateful, but not connected to a backend.** `pb.load()` / `protoc:load()`
  register message descriptors into a `pb.State`, which `pb.encode`/`pb.decode`
  then key on by type name. The state is per-`lua_State` (lua-protobuf stores it
  in the registry via `lpb_lstate`), so it is naturally scoped to one script run
  — the same lifetime model as a redis/mysql connection, but without sockets.
- **The `pb.State` object is itself exposed** (`pb.state()`) and owns an
  idempotent `__gc` (`Lpb_delete`) and a `setdefault` method, matching the
  "idempotent `__gc`, guard against `:close()`-then-GC" contract in the README.

### Public surface (scripts see four globals)

| Global | Source | Role |
|---|---|---|
| `pb` | `pb.c` (C) | encode/decode/types/fields/… + `buffer`/`slice`/`conv`/`io`/`unsafe` sub-tables |
| `protoc` | `protoc.lua` | `protoc.new()` / `protoc:load(schema_text)` → compile `.proto` text into `pb` |
| `serpent` | `serpent.lua` | `serpent.block(t)` / `serpent.line(t)` — debug pretty-print of decoded tables |

`serpent` is optional (debug-only); it is included for parity with the upstream
examples and costs ~135 lines of pure Lua. See §10 for the `pb.io`/`pb.unsafe`
gating decision.

## 5. Core API — encode/decode/load

The functions scripts actually call (from the upstream README, table §"pb Module"):

| Call | Returns | Notes |
|---|---|---|
| `pb.load(binary_schema)` | `boolean, integer` | load a compiled schema (`.pb` / FileDescriptorSet bytes); **returns** success + read offset — the one loader that reports rather than raises |
| `pb.loadfile(path)` | `boolean, integer` | as `pb.load` but reads the schema file |
| `pb.encode(type, table[, buffer])` | `string` / `buffer` | encode a message; raises `lua_error` on failure |
| `pb.decode(type, data[, table])` | `table` | decode bytes to a Lua table; raises on failure |
| `pb.types()` / `pb.fields(type)` | table | introspect loaded schema |
| `pb.type(type)` / `pb.field(type, name)` | table | per-message / per-field descriptor |
| `pb.enum(type, name[, val])` / `pb.defaults(type)` | … | enum and default handling |
| `pb.tohex(s)` / `pb.fromhex(h)` | `string` | hex debugging helpers |
| `pb.option(flag[, val])` | … | toggle `encode_default_values`, `encode_order`, hooks, etc. |

The `protoc` companion is the seam's *raison d'être* for self-containment: it
lets a script compile a text `.proto` at run time, so **no external `protoc`
toolchain is needed** at deploy time.

```lua
local p = protoc.new()
p:load [[ message Person { optional string name = 1; optional int32 age = 2; } ]]
```

`protoc` also registers the standard `google.protobuf.*` descriptor messages at
load time (its `Parser.reload()` calls `pb.load(descriptor_pb)`), so
`pb.types()` is populated with the descriptors immediately.

## 6. Error convention — `nil, errmsg` + raise-on-misuse

lua-protobuf's convention differs from the engine's `{ err = "..." }` binding
contract in one important way: it is a **library module, not a bound command**.
Its functions are called directly by script code, never routed through
`ufsrvscript.call`/`pcall`, so they follow upstream Lua-library idiom:

- **`pb.load` / `pb.loadfile` return `(false, offset)`** on failure — the script
  checks the boolean (the only *reporting* loader, per the upstream README:
  "only `pb.load()` returns error on failure, *do check* the result").
- **Everything else raises `lua_error`** on bad input (unknown type, malformed
  data, wrong argument type). The engine's installed error handler enriches
  those with `source:line`, and a script can wrap in `pcall` if it wants to
  recover.

This mirrors `json`'s "seam-only" stance: the module does not route through
`sDispatch`, so it does not need to reshape lua-protobuf's errors into
`{ err = ... }` tables. The seam's own C code (registration shim + `require`
bridge) is what must not leak errors into the caller's stack (§7.1).

## 7. Module contract

### 7.1 The seam (`UfsrvScriptProtobufRegister`)

One entry point, uniform with the others:

```c
PUBLIC_API void UfsrvScriptProtobufRegister(lua_State *L);
```

Its steps:

```c
#include <ufsrvscriptlib/ufsrvscriptlib_protobuf.h>
#include <lauxlib.h>
#include <string.h>

/* forward declarations of the vendored openers (only external symbols of pb.c) */
LUALIB_API int luaopen_pb(lua_State *L);
LUALIB_API int luaopen_pb_buffer(lua_State *L);
LUALIB_API int luaopen_pb_slice(lua_State *L);
LUALIB_API int luaopen_pb_io(lua_State *L);
LUALIB_API int luaopen_pb_conv(lua_State *L);
LUALIB_API int luaopen_pb_unsafe(lua_State *L);

void
UfsrvScriptProtobufRegister(lua_State *L)
{
    /* 1. core pb table */
    luaopen_pb(L);                       /* stack: [pb]                       */

    /* 2. attach the submodules (upstream: require "pb.buffer" etc.) */
    luaopen_pb_buffer(L);  lua_setfield(L, -2, "buffer");
    luaopen_pb_slice(L);   lua_setfield(L, -2, "slice");
    luaopen_pb_conv(L);    lua_setfield(L, -2, "conv");
    /* pb.io and pb.unsafe gated — see §10 */
    lua_setglobal(L, "pb");              /* stack: []                         */

    /* 3. load the pure-Lua companions (protoc needs `pb` reachable via require) */
    sLoadCompanionModules(L);            /* sets globals `protoc` and `serpent` */
}
```

**Step 3 is the only non-trivial part** and is called out in §7.2: the sandbox
does not load `package`, so `require` is absent, but `protoc.lua` obtains `pb`
through `pcall(require, "pb")`.

### 7.2 The `require` bridge — the sandbox gap

Facts established during investigation:

1. The engine (`ufsrvscriptlib_lua.c`) opens only base/table/string/math/debug by
   default; `os` and `package` are loaded **only** when `UfsrvScriptLuaOptions`
   enables them. The CLI wires `UfsrvScriptCliBootstrap(..., NULL)` — the NULL
   opts select the defaults — so `require`/`package` are **absent** and the
   strict.lua global guard is active.
2. `protoc.lua` wraps its whole tail in
   `local has_pb, pb = pcall(require, "pb") do … end` and, crucially, has **no
   global fallback** — if `require "pb"` fails, `pb` is `nil` and the returned
   `Parser` is broken (its `:load()` dereferences `pb`). `protoc` *must* load
   with a working `require "pb"`.
3. `serpent.lua` does **not** `require` anything — it returns a table directly.
4. strict.lua's `__newindex`/`__index` exempt C-level access (`what == "C"`),
   so `lua_setglobal`/`lua_getglobal` from C are permitted (this is exactly why
   redis/mysql/json register globals without tripping the guard).

**Recommended approach — private `_ENV` (sandbox stays closed).** The seam
registers `pb` as a global, then loads each companion in a private `_ENV` that
(a) supplies a `require` resolving only `pb`, and (b) falls back to `_G` via a
**rawget** so the strict-globals guard does not reject the companions' own reads
of globals the sandbox does not load (e.g. `protoc.lua`'s top-level
`local io = io`, which binds `nil` instead of tripping the guard):

```c
static int sProtobufRequire(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    if (strcmp(name, "pb") == 0) { lua_getglobal(L, "pb"); return 1; }
    return luaL_error(L, "module '%s' not found", name);
}

static int sCompanionEnvIndex(lua_State *L)   /* __index: rawget against _G */
{
    lua_pushglobaltable(L);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    lua_remove(L, -2);
    return 1;
}

static int sLoadCompanion(lua_State *L, const char *src, size_t len,
                          const char *chunkname)
{
    if (luaL_loadbuffer(L, src, len, chunkname) != LUA_OK) { /* log + pop */ return 0; }
    lua_newtable(L);                        /* private env             */
    lua_pushcfunction(L, sCompanionEnvIndex);
    lua_setfield(L, -2, "__index");         /* raw _G view             */
    lua_pushcfunction(L, sProtobufRequire);
    lua_setfield(L, -2, "require");         /* pb-only require         */
    lua_setupvalue(L, -2, 1);               /* chunk._ENV = env        */
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) { /* log + pop */ return 0; }
    return 1;                               /* module table on stack   */
}
```

`require` therefore never exists as a global — not even transiently. It lives
only inside each companion's lexical `_ENV`, resolved at C level during
registration and invisible to scripts. The `__index` rawget means the
companions bind any non-whitelisted global they read (e.g. `io`) to `nil`
rather than tripping the guard, so `protoc.lua` loads unmodified.

**Rejected alternative — `is_package_allowed = true`.** Opening `package`
would make `require "pb"` work "for free" and let scripts use upstream
`local pb = require "pb"` verbatim, but it also exposes `package.loadlib`
(dlopen of arbitrary C — a genuine sandbox escape) and `package.searchers`.
That is a security-posture change to the engine's shared CLI sandbox with
blast radius beyond protobuf, so it needs explicit sign-off and is **not**
recommended. The private `_ENV` keeps the shared sandbox exactly as it is
today and confines the `require` capability to the companions' own lexical
environment, in C, invisible to scripts afterwards.

### 7.3 Layout and registration

```
src/lua_bindings/protobuf/ufsrvscriptlib_protobuf.c       # seam (this file)
src/lua_bindings/protobuf/lua_protobuf/pb.c  pb.h       # vendored C core
src/lua_bindings/protobuf/lua_protobuf/protoc.lua       # vendored companion
src/lua_bindings/protobuf/lua_protobuf/serpent.lua      # vendored companion
include/ufsrvscriptlib/ufsrvscriptlib_protobuf.h            # public header
```

Wiring, matching `json`:

- `../../CMakeLists.txt`: add
  `src/lua_bindings/protobuf/ufsrvscriptlib_protobuf.c` and
  `src/lua_bindings/protobuf/lua_protobuf/pb.c` to the `ufsrvscriptlib` sources.
  No new `target_link_libraries` dependency (pure C + Lua).
- `../../../../src/ufsrvscriptlib/ufsrvscriptlib_cli.c`: after the redis/mysql/json registrations,
  ```c
  UfsrvScriptProtobufRegister(UfsrvScriptLuaState(lua));
  ```
- No new `backend.*.*` constants and no `UfsrvCliConfig` change — protobuf is
  schema-driven, not backend-driven.

### 7.4 The pure-Lua companions: embed vs. ship

`protoc.lua` (~39 KB) and `serpent.lua` (~8.8 KB) must be available at
registration. Two options:

| | Embed as C string (`kProtocLua`/`kSerpentLua`) | Ship as data files |
|---|---|---|
| Mechanism | build step converts each `.lua` into a `static const char[]` header, `#include`d by the seam | `install(FILES …)` into `share/ufsrvscriptlib/lua/`, `luaL_loadfile` at registration |
| Runtime path fragility | none (self-contained binary) | needs an install-prefix/relative path resolution |
| Binary size | +~48 KB | none |
| Precedent | the engine already embeds `strict.lua`/`err_handler` as C strings | none in this repo |

**Recommended: embed.** The engine's own `strict_lua[]` precedent, plus the
"CLI is a single self-contained binary" goal, favours embedding. The
conversion is a documented build step (a small CMake `file(READ)` +
`string(CONFIGURE)`/`configure_file` producing a header), so the vendored
`.lua` files stay pristine and no human hand-escapes 39 KB of Lua. The
generated header is build output — treated like `version.h`, never edited by
hand (and, per house rules, `*_generated` files are only ever produced by their
generator).

## 8. Lua 5.4 adaptation — none required

`pb.c` already targets 5.1–5.4: its `#if LUA_VERSION_NUM < 502` block provides
`luaL_register`/`luaL_newlib`/`luaL_setfuncs` shims **only** for pre-5.2, and
it branches on `>= 503`/`< 505` for integer-width handling. Under 5.4 it uses
native `luaL_newlib`/`luaL_setfuncs` and `lua_pushexternalstring` (the 5.4
external-string API it uses to hand Lua a pointer to an encoded buffer with a
free callback). No source edits are needed — unlike the redis port (which had
to strip a 5.2 `luaL_register` shim), lua-protobuf is 5.4-native out of the box.

Watch item: it pins to Lua **5.4** (not 5.3) because of
`lua_pushexternalstring`. The engine is committed to 5.4 (CMake finds `/opt`
`lua`), so this is satisfied; record it in the provenance README as a hard
constraint in case the engine's Lua is ever revisited.

## 9. Thread-safety / global state

lua-protobuf keeps the active `pb.State` in the registry per `lua_State`
(`lpb_lstate`), plus a module-level `global_state` used only by
`pb.unsafe.use("global")`. The CLI runs **one** `lua_State` per process
(`UfsrvScriptCliEval` bootstraps a fresh VM each run), so the shared
`global_state` is a non-issue. Note it as a "not multi-VM-safe" caveat in the
provenance README; if the engine is ever extended to run concurrent VMs in one
process, `pb` would need the `global_state` to be per-VM.

## 10. Security posture in the sandbox

Three surfaces need explicit decisions, ranked by risk.

### 10.1 `pb.unsafe` — raw pointer escape (exclude by default)

`pb.unsafe` is named for a reason: `touserdata()` returns a light userdata
pointer to an encoded buffer, `slice(ptr, len)` wraps an arbitrary address+size,
and `decode(ptr, len)` decodes arbitrary memory. Registered verbatim it is a
memory-safety escape in a sandboxed engine. **Recommendation: do not attach
`pb.unsafe`** in the seam (drop the `luaopen_pb_unsafe` line). It is not needed
for encode/decode of schemas and exists upstream only for zero-copy
interop with C embedders.

### 10.2 `pb.io` / `pb.loadfile` — file read/write (gate by decision)

`pb.io` (`read`/`write`/`dump`) and `pb.loadfile` open files through libc
`fopen`/`fwrite` — a *write* vector (`pb.io.dump(msg, "path")` can write any
file the process can write) that the sandbox otherwise lacks (`io` is not
loaded). Two defensible positions:

- **Exclude `pb.io` and `loadfile`** — scripts load schemas from strings
  (`protoc:load`, `pb.load(hex)`); no file surface at all. Most conservative.
- **Include `pb.io`** — useful for `pb.io.dump`/`write` of encoded payloads,
  but it reopens a filesystem vector the sandbox deliberately closed.

**Recommendation: exclude `pb.io` and `loadfile` in the first cut**, matching
the engine's "no filesystem" default posture; re-open behind an explicit
sign-off if a real schema-on-disk use case appears. `protoc.lua`'s own
`compilefile`/`loadfile` methods are inert in the sandbox anyway (`io` is
`nil`), while its `protoc:load(text)` — the core path — is unaffected.

### 10.3 The `require` bridge is private and confined

The `require` shim in §7.2 lives only inside each companion's lexical `_ENV`,
resolved in C during `UfsrvScriptProtobufRegister` and never present as a
global — so scripts cannot reach it, and no cleanup step is needed. This keeps
the shared sandbox unchanged.

### 10.4 `serpent` and the global guard

`serpent` is pure Lua that reads globals it expects to exist (`tostring`,
`type`, `string`, …) and captures `_G` at load time. Because it is loaded as a
`main` chunk during registration — before any script runs, and with the strict
guard already installed — it can only touch the whitelisted globals. It is a
read-only pretty-printer; no escalation.

## 11. Fatal-path control — no process abort

`pb.c` differs from lua-cjson in that its **OOM path already routes to Lua
error**: the `lpb_checkmem` macro is
`ret ? ret : (size_t)luaL_error(L, "out of memory")` — a `longjmp`, not
`abort()`. It does, however, call `assert()` at seven sites (e.g. an
"unknown expected type" defensive branch, `assert(s.p != NULL)`,
`assert(nsize == 0)` in the external-string free callback, `assert(m != lpbE_Raw)`).
These are **invariant checks on unreachable states**, not resource-failure
handlers, so they are materially safer than lua-cjson's OOM `abort()`. Still, an
`assert()` that fires calls `abort()` and takes down the process.

**Recommendation: compile `pb.c` with `NDEBUG` for this TU only**
(`set_source_files_properties(... COMPILE_DEFINITIONS NDEBUG)`), which turns the
invariant asserts into no-ops and leaves the `luaL_error` OOM path intact. This
is a build flag, not a vendored-source edit — consistent with the "never modify
vendored code" rule and the json precedent of handling fatal paths at the seam.
The one assert worth keeping (the external-string `nsize == 0` contract) is
guaranteed by the Lua 5.4 API and does not need the runtime check.

Two additional per-TU build flags (same `set_source_files_properties` call) came
out of the sanitizer smoke test:

- `-Wno-implicit-fallthrough` — pb.c has an intentional `/* FALLTHROUGH */`
  into `default:` that clang does not read from a comment (mirrors hiredis's
  `-Wno-macro-redefined`).
- `-fno-sanitize=signed-integer-overflow` and `-fno-sanitize=pointer-overflow` —
  the Knuth multiplicative hash (`pbT_hash`) wraps signed 64-bit, and
  `pb_lslice(NULL, 0)` computes `NULL + 0` on a bad-load path; both are benign
  and uneditable, and `-fsanitize=undefined` pulls in `pointer-overflow`. ASan
  (memory safety) and the remaining UBSan checks stay active.

Record these in the provenance README's "Customization" section so they are
re-applied after any vendor bump.

## 12. Provenance

| Item | Value |
|---|---|
| Upstream project | [lua-protobuf](https://github.com/starwing/lua-protobuf) |
| Version | `0.5.3-5-gbbe744f` (5 commits past tag `0.5.3`) |
| Baseline commit | `bbe744fd25738cccbda973e8fbccdf32c0fb4f9d` |
| License | MIT — `Copyright (c) 2018 Xavier Wang` |
| Vendored files | `pb.c`, `pb.h`, `protoc.lua`, `serpent.lua` |
| Companion origin | `serpent.lua` — [pkulchenko/serpent](https://github.com/pkulchenko/serpent) `0.30`, MIT, `Copyright (c) 2012-17 Paul Kulchenko` |
| Excluded | `test.lua`, `luaunit.lua` (test-only) |
| Copied on | 2026-09-07 |

The seam carries a `../../src/lua_bindings/protobuf/README.md`
recording the provenance (with the exact baseline commit above), the "hard
dependency — do not edit in place" notice, the Lua 5.4 (external-string)
constraint, the single-VM caveat, and the **re-apply-after-bump customizations**:
submodule composition + `pb.loadfile` nil, the `_ENV` `require` bridge, the
per-TU flags (`NDEBUG`, `-Wno-implicit-fallthrough`,
`-fno-sanitize=signed-integer-overflow`, `-fno-sanitize=pointer-overflow`), and
the `xxd -i` companion embedding.

**Vendoring & backporting.** Every customization lives *outside* the vendored
files (in `ufsrvscriptlib_protobuf.c` and `../../../../CMakeLists.txt`), so a vendor bump never
re-merges edits into upstream code: copy `pb.c`/`pb.h`/`protoc.lua`/`serpent.lua`
from the recorded baseline (or a newer reviewed commit), rebuild, and re-run the
gtest + `../../../../tests/lua/pb.lua` suites to catch API drift. The exact commands are in
the README's "Update procedure (vendor bump)" section.

## 13. Implementation phases (authorization gates)

1. **Vendor + build**: copy `pb.c`/`pb.h`/`protoc.lua`/`serpent.lua` into
   `lua_protobuf/`, add `pb.c` to the `ufsrvscriptlib` sources (with per-TU
   `NDEBUG`), provenance README. Build clean under `asan-ubsan` with no
   warnings. *(gate)*
2. **Seam**: `ufsrvscriptlib_protobuf.c` + public header — compose `pb` +
   submodules (minus `unsafe`/`io`), implement the `require` shim, load the
   companions (embed step), register from `ufsrvscriptlib_cli.c`. CLI smoke:
   `ufsrvcli -s script.lua` doing `protoc:load` + `pb.encode` + `pb.decode`.
   *(gate)*
3. **Security review**: confirm `pb.unsafe`/`pb.io` are absent, the shim is
   removed post-registration, and `assert()` is compiled out. Adversarial tests:
   unknown type name, malformed bytes, decode past buffer, wrong arg types,
   `pb.load` on garbage, `__gc` on a `pb.state()` after a script error.
   *(gate)*
4. **Docs**: this design doc + `lua_bindings/README.md` module-table row +
   HOWTO/man updates + Version Management bumps.

## 14. Open items to confirm at implementation

- **Global names** `pb`/`protoc`/`serpent` (upstream verbatim — recommended for
  copy-paste compatibility with lua-protobuf examples) vs. a `protobuf` alias.
- **`pb.io`/`loadfile`** inclusion (recommended: exclude for the first cut —
  see §10.2).
- **Embed vs. ship** the two `.lua` companions (recommended: embed — see §7.4).
- Confirm the first clean link needs no extra `libm` (pb.h's float
  encode/decode use bit tricks, not `<math.h>` — expected no `-lm`).
