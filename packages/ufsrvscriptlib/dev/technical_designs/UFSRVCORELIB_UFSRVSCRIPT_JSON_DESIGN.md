# ufsrvscriptlib — JSON Codec Native Module Design

## Version Management

| Service Name | Date Created | Created By |
|---|---|---|
| ufsrvscriptlib | 2026-09-06 | devops |

| Version | Date Modified | Modified By | Description |
|---|---|---|---|
| 1.0.0 | 2026-09-06 | devops | Initial design — native JSON codec Lua module (lua-cjson) for the scripting engine |
| 1.1.0 | 2026-09-06 | devops | Restructure to a clean interface seam over a pristine vendored core (drop-in vendor bumps) |
| 1.2.0 | 2026-09-06 | devops | Add fatal-path control — replace vendored abort() with a Lua-error funnel (no process abort) |
| 1.3.0 | 2026-09-06 | devops | Add int64 overflow guard — decode falls back to a float past int64 (customization #2) |
| 1.4.0 | 2026-09-06 | devops | Move the vendored lua-cjson core into its own lua_cjson/ subdirectory |

## 1. Purpose

Give `--eval-script` scripts a fast, correct JSON **encode/decode** primitive,
in the same way `redis` exposes the Redis backends and `mysql` exposes the
MariaDB backend. The module is a thin interface seam over **lua-cjson**
(`lua_cjson.c`, MIT — see §10 Provenance), adapted to the `ufsrvscriptlib`
interface contract established by the Redis module.

The reference implementation to model on is the Redis module
(`../../src/lua_bindings/redis/ufsrvscriptlib_redis.c`) and the
contract in `../../src/lua_bindings/README.md`.

## 2. Architecture — uniform seam over an imported core

The governing constraint: **every native module presents the same interface**
(one `UfsrvScript<Module>Register(lua_State *)` entry, one global table, one
error convention, one layout), while the **lower implementation layer may be
imported as-is** — so future vendor enhancements drop in by overwriting files,
not by re-merging our edits.

This is the hiredis precedent, applied to a stateless codec: `redis` is a thin
`UfsrvScriptRedis*` seam over a pristine vendored `hiredis/` core. `json` is the
same shape — a thin seam over a pristine vendored lua-cjson core. The only
difference is *what the seam does*: hiredis needs a full API wrapper (connect,
command, …); a codec has no API to wrap, so the seam is a **registration shim**
— it calls the vendored loader and re-homes the result under the uniform
interface (global name + error convention). See §6.

```
json/                        # packages/ufsrvscriptlib/src/lua_bindings/json/
├── ufsrvscriptlib_json.c      # ← OUR seam (uniform interface)
└── lua_cjson/               # vendored lua-cjson core (pristine, name-preserved)
    ├── lua_cjson.c
    ├── strbuf.c  strbuf.h
    └── fpconv.c  fpconv.h
# public header: packages/ufsrvscriptlib/include/ufsrvscriptlib/ufsrvscriptlib_json.h
```

## 3. Dependency — none (vendored pure-C sources)

lua-cjson **links no external library** — it is pure C over the Lua API and
libc; its upstream `rockspec` states *"No dependencies on other libraries."* So
there is no `find_path`/`find_library` and no vendored static-lib target; the
sources compile directly into the `ufsrvscriptlib` static library.

The default upstream build (the `cjson` module in the `rockspec`) is exactly
three translation units plus two headers:

| File | Role |
|---|---|
| `lua_cjson.c` | the module — encode/decode + config object (1,827 lines) |
| `strbuf.c` / `strbuf.h` | dynamic string buffer (encode buffer + decode scratch) |
| `fpconv.c` / `fpconv.h` | locale-safe `strtod()`/`snprintf()` number conversion |

The `dtoa.c` / `g_fmt.c` / `dtoa_config.h` trio is **excluded**: it only builds
under `USE_INTERNAL_FPCONV` (a faster internal double→ASCII path), and carries
David M. Gay's Lucent dtoa license and an endianness/pthread configuration
surface. The default libc path (`fpconv.c`) is portable, has no extra license,
and is what upstream ships by default. We vendor the **five MIT files** above
and nothing else — keeping the bundle single-license (MIT, Mark Pulford).

> **Watch item:** `lua_cjson.c` calls `floor()` (in `lua_array_length`), the only
> `libm` symbol in the bundle. On glibc the engine's Lua link already pulls
> `libm`, so no explicit `-lm` is expected — but confirm the first clean link,
> and add `m` to the link line only if the build complains.

## 4. Module shape — a codec, not a backend

This is the key structural difference from the first two modules. Redis and
MySQL are **stateful backend** modules: `connect(...)` → a connection userdata
with methods, a result userdata, and `backend.*.*` config constants. JSON is a
**stateless codec**: two pure functions over Lua values, plus a small set of
"shape" sentinels. Concretely:

- **No userdata object model.** No `connection`/`result` tiers. The only userdata
  is the module's internal `json_config_t` (see §6.2), which scripts never touch
  directly.
- **No config wiring.** No `MysqlBackend`/`RedisBackend` struct, no
  `UfsrvCliConfig` field, no `backend.json.*` constants, no secret/password. The
  module adds nothing to `../../../../etc/ufsrvcli.config.lua` or the config schema.
- **No API-wrapper seam.** Redis wraps hiredis's C API; MySQL wraps libmariadb's.
  JSON wraps *nothing* — there is no third-party C API to seam over. The
  `UfsrvScript<Module>*` public surface **degenerates to the registration entry
  point** (`UfsrvScriptJsonRegister`), and the seam body is a shim, not a wrapper
  (§6.1).

The script-facing surface:

```lua
local s = json.encode({ hello = "world", n = 1, list = { true, nil } })
-- {"hello":"world","n":1,"list":[true,null]}
local t = json.decode(s)                 -- { hello = "world", n = 1, list = { true, json.null } }
```

## 5. Type-aware value conversion

lua-cjson already does type-aware conversion, and — because this fork supports
Lua 5.3+ — it **preserves the integer/float distinction** our engine (Lua 5.4)
also relies on in the MySQL module.

### 5.1 Encode (Lua → JSON)

| Lua value | JSON output |
|---|---|
| string | JSON string (full escaping, incl. `\uXXXX` for control chars) |
| number (integer, `lua_isinteger`) | JSON integer (via `%LUA_INTEGER_FMT`) |
| number (float) | JSON number (via `fpconv_g_fmt`, precision configurable 1–16) |
| boolean | `true` / `false` |
| table (1..n integer keys) | JSON array |
| table (any other keys) | JSON object |
| `nil` (top level, or via iteration) | `null` (note: a `nil` *value* simply drops the key — Lua semantics) |
| lightuserdata `json.null` | `null` |
| lightuserdata `json.empty_array` | `[]` |
| function / userdata / thread / other lightuserdata | error (or skipped when `encode_skip_unsupported_value_types` is on) |

The last row is a deliberate **synergy** with the other modules: a script that
accidentally does `json.encode(mysql.connect(...))` gets a clean error — the
connection/result userdata are exactly the "unsupported types" cjson refuses to
serialise — rather than leaking C internals.

### 5.2 Decode (JSON → Lua)

| JSON token | Lua value |
|---|---|
| object | table (string keys) |
| array | table (integer keys 1..n); optionally tagged with `array_mt` |
| string | string (with `\uXXXX` surrogate pairs decoded to UTF-8) |
| integer-able number | **integer** (`lua_pushinteger`) |
| fractional / exponent number | float (`lua_pushnumber`) |
| `true` / `false` | boolean |
| `null` | `json.null` lightuserdata — **not** `nil`, because `t[k] = nil` deletes the key |

A JSON integer outside the 64-bit `lua_Integer` range falls back to a **float**
via an `errno == ERANGE` guard (a vendored-core customization — see the
`json/README.md`) rather than silently clamping to `LLONG_MAX`/`LLONG_MIN`.

`json.null` is the one idiom scripts must learn: it is a lightuserdata sentinel
on the module table, compared with identity (`v == json.null`). This preserves
the round-trip of `{ "x": null }` through decode → encode, which a plain `nil`
would not.

## 6. Error convention — `nil, errmsg` (uniform with redis/mysql)

The uniform interface rule is: **operational/data errors return `nil, errmsg`
(never raise); programmer misuse (bad types/arg counts) raises.** That is
exactly how redis (`nil, errstr, errno`) and mysql (`luaL_checkudata`) already
behave; JSON simply has no `errno`, so its multi-return is the two-value
degenerate `nil, errmsg`.

Stock `cjson.encode`/`cjson.decode` **raise** on bad input; a separate
`cjson.safe` submodule wraps them in `pcall` and returns `nil, errmsg`. Rather
than re-implement that wrap ourselves (or edit the vendor core), the seam calls
the vendor's **`luaopen_cjson_safe`** entry — its `encode`/`decode` already
return `nil, errmsg`, its config setters already raise on misuse (via
`luaL_checkinteger`/`luaL_argcheck`, the same idiom as mysql's type checks), and
its `new()` produces another safe instance, keeping the whole surface uniform.
We drop nothing and fork nothing.

```lua
local v, err = json.decode(bad_input)   -- nil, "Expected value but found ... at character 4"
```

`nil` is unambiguous as a first return: `encode` always returns a string and
`decode` returns a value (`null` → `json.null`, never `nil`), so `nil` signals
only an error. Scripts that prefer a raising form write it themselves:

```lua
local v, e = json.decode(s); if not v then error(e, 2) end
```

## 7. Module contract

### 7.1 The seam (registration shim)

The seam is a single small file. It owns the uniform interface contract and
nothing else:

```c
/**
 * @file ufsrvscriptlib_json.c
 * @brief Native Lua bindings for JSON — the ufsrvscriptlib interface seam.
 */
#include <ufsrvscriptlib/ufsrvscriptlib_json.h>

#include <lua.h>

/* The vendored lua-cjson core (lua_cjson.c) exports its loaders; call the
 * "safe" loader so encode/decode follow the uniform nil,err convention, then
 * re-home the table under the uniform `json` global. */
extern int luaopen_cjson_safe(lua_State *L);

PUBLIC_API void
UfsrvScriptJsonRegister(lua_State *L)
{
    luaopen_cjson_safe(L);      /* build the cjson table (nil,err encode/decode) */
    lua_pushliteral(L, "json"); /* align _NAME with the uniform global          */
    lua_setfield(L, -2, "_NAME");
    lua_setglobal(L, "json");   /* re-home under the uniform global name        */
}
```

- Global: `json` (set via `lua_setglobal` from C — exempt from strict.lua).
- Entry point: `PUBLIC_API void UfsrvScriptJsonRegister(lua_State *L);` — the
  identical signature to redis/mysql.
- Registered by the host after `UfsrvScriptBootstrap()`, alongside the others:

  ```c
  UfsrvScriptRedisRegister(UfsrvScriptLuaState(lua));
  UfsrvScriptMysqlRegister(UfsrvScriptLuaState(lua));
  UfsrvScriptJsonRegister(UfsrvScriptLuaState(lua));
  ```

- No metatables of the `ufsrvscript.<module>.<type>` form — there is no
  script-visible userdata object to name.
- `json._NAME == "json"` (set by the seam); `json._VERSION` is left as the
  vendored source reports it (`CJSON_VERSION`) — upstream's own provenance
  marker, not ours to rewrite.

The vendored core is **never hand-edited**: the `json_*`/`strbuf_*`/`fpconv_*`
symbols stay name-preserved for exactly the reason hiredis's internals do — they
are a separate imported layer, and a vendor bump is a straight file overwrite.
There is therefore **no naming exception** to the "`s`-prefixed statics" rule:
the rule applies to code we author (the seam carries only `UfsrvScriptJson*`);
the imported core is out of that scope, the same as `hiredis/`.

### 7.2 The config object (internal)

lua-cjson binds a `json_config_t` userdata (settings + a reusable `strbuf` + the
escape/parse lookup tables) as the **first upvalue of every function** via
`luaL_setfuncs(reg, 1)`, with a `__gc` that frees the buffer and indent string.
This is **not** the fragile caller-upvalue idiom the `README.md` warns against
(that note concerns reaching the *module table* from a *method* via
`lua_upvalueindex`); this is a deliberate closure binding that gives each
instance independent settings and lets `json.new()` spawn a fresh config. It is
kept as-is.

Settings are get/set functions (each returns the current value):

| Function | Default | Notes |
|---|---|---|
| `encode_max_depth(n)` / `decode_max_depth(n)` | `1000` | recursion bound — **security-relevant** (§9) |
| `encode_number_precision(n)` | `14` | 1–16; float output precision |
| `encode_invalid_numbers("off"\|"on"\|"null")` | `"off"` | NaN/Inf handling on encode |
| `decode_invalid_numbers(on\|off)` | `on` | accept NaN/Inf/hex on decode |
| `encode_sparse_array(convert, ratio, safe)` | `off,2,10` | sparse-array policy |
| `encode_keep_buffer(on\|off)` | `on` | reuse a persistent encode buffer |
| `encode_empty_table_as_object(on\|off)` | `on` | `{}` → `{}` vs `[]` |
| `decode_array_with_array_mt(on\|off)` | `off` | tag decoded arrays with `array_mt` |
| `decode_allow_comment(on\|off)` | `off` | permit `//` and `/* */` |
| `encode_escape_forward_slash(on\|off)` | `on` | escape `/` as `\/` |
| `encode_skip_unsupported_value_types(on\|off)` | `off` | drop (not error on) unencodable values |
| `encode_indent(str)` | `""` | pretty-print with the given indent |

Sentinels and instance helpers:

| Field / call | Value |
|---|---|
| `json.null` | lightuserdata `NULL` |
| `json.empty_array` | lightuserdata that encodes as `[]` |
| `json.array_mt` / `json.empty_array_mt` | metatables to tag tables as arrays |
| `json.new()` | a new independent instance (fresh config, safe) |
| `json._NAME` / `json._VERSION` | `"json"` / vendored version string |

### 7.3 Layout

| Concern | Location |
|---|---|
| Our seam (interface) | `../../src/lua_bindings/json/ufsrvscriptlib_json.c` |
| Our public header | `../../include/ufsrvscriptlib/ufsrvscriptlib_json.h` |
| Vendored core (imported, pristine) | `…/json/lua_cjson/` — `lua_cjson.c`, `strbuf.c`, `strbuf.h`, `fpconv.c`, `fpconv.h` |
| Build wiring | `../../CMakeLists.txt` (four sources; no link change) |
| Host registration | `../../../../src/ufsrvscriptlib/ufsrvscriptlib_cli.c` (`UfsrvScriptCliBootstrap`) |

## 8. Lua 5.4 adaptation

The OpenResty fork is already version-guarded for Lua 5.1 through 5.4, so the
vendored core compiles cleanly against 5.4 with **no source changes at all**.
The adaptation is confined to the seam:

1. The engine prunes `require`, so the vendored `luaopen_*` loader's
   "return the table" path is not usable directly; the seam calls
   `luaopen_cjson_safe` and re-homes the table with `lua_setglobal(L, "json")`
   — the redis/mysql load pattern.
2. The already-present `LUA_VERSION_NUM >= 503` integer fast path
   (`lua_isinteger`/`lua_tointeger` in `json_append_number`/`json_next_number_token`)
   activates on 5.4 and gives integer-preserving round-trips (§5.2).

## 9. Security posture in the sandbox

The engine runs untrusted scripts against a whitelisted stdlib; a JSON codec is
a new resource-consumption and correctness surface. lua-cjson already carries
the relevant guards, which we **preserve** (and expose, not hard-disable):

- **Depth limits** — `encode_max_depth`/`decode_max_depth` (default 1000) bound
  the recursion in `json_append_data` / `json_parse_*_context`, backed by
  `lua_checkstack` at each level. They are settable per-instance, so a script can
  raise them — but only to DoS *its own* `--eval-script` state (each invocation
  is a fresh `lua_State`), never a shared/other session.
- **Unencodable values error by default** — `encode_skip_unsupported_value_types`
  is `off`, so functions/userdata (incl. redis/mysql connection handles) raise,
  never leak internals.
- **Strict numbers** — `encode_invalid_numbers` defaults to `off` (NaN/Inf raise
  on encode); `decode_invalid_numbers` defaults to `on` (upstream's
  `strtod`-compatible leniency). Both are left at upstream defaults.
- **UTF-8 is not validated** on encode (bytes pass through) and only BOM
  detection + `\uXXXX` surrogate handling on decode — documented upstream caveat,
  out of scope for this module.

> The one deliberate touch to the vendored core — controlling its `abort()` on
> failure — is designed in §10 (fatal-path control). Everything else stays
> pristine.

## 10. Fatal-path control — no process abort

The vendored core calls `abort()` on every failure path, including genuine OOM
during `encode`/`decode` — a process kill instead of a script error. That is
unacceptable in a sandboxed engine that owns its failure semantics. We customize
the fatal path with a single funnel, leaving all algorithm logic untouched.

### 10.1 The inventory

| File | Site | Reachable? | Nature |
|---|---|---|---|
| `strbuf.c` `die()` | `malloc` in `strbuf_init` | **yes** | OOM |
| `strbuf.c` `die()` | `malloc` in `strbuf_new` | **yes** | OOM |
| `strbuf.c` `die()` | `realloc` in `strbuf_resize` | **yes** | OOM |
| `strbuf.c` `die()` | `len<=0`, `reqsize<len`, `newsize<reqsize` | no | internal invariant (BUG/overflow) |
| `fpconv.c` | `malloc` in `fpconv_strtod` | **yes** | OOM (huge number literal on a comma locale) |
| `fpconv.c` | printf/wide-char check (`fpconv_update_locale`) | no | startup invariant |
| `lua_cjson.c` | `lua_newuserdata` NULL | no | unreachable (`lua_newuserdata` raises) |
| `lua_cjson.c` | `SIZE_MAX/6` overflow guard | no | unreachable (exabyte string) |

Four genuine OOM sites; seven effectively-unreachable invariant checks. We route
**all** of them through one funnel, so no path ever aborts the process.

### 10.2 The funnel

One function in `lua_cjson.c`, plus a captured `lua_State`:

```c
/* The single fatal funnel. strbuf.c / fpconv.c are Lua-agnostic, so instead of
 * threading lua_State* through every hot path, the module captures its state at
 * build time and every fatal path raises on it. Falls back to abort() only if
 * invoked before capture (a can't-happen case — kept as a last resort so a
 * programming error is never silently swallowed). */
static lua_State *g_json_state = NULL;

void lua_cjson_fatal(const char *msg)
{
    if (g_json_state)
        luaL_error(g_json_state, "json: %s", msg);
    else
        abort();
}
```

`g_json_state` is set at the top of `lua_cjson_new()` (before `fpconv_init()`),
so it is captured before any reachable OOM and before the locale check runs. The
engine is single-threaded — each `--eval-script` runs one `lua_State` at a time
— so a single module-local capture is correct (the README's "no shared state"
rule is *between* modules, not within one).

Call-site changes (mechanical, no logic change):

- `strbuf.c`: `die()` formats into a stack buffer and calls
  `lua_cjson_fatal(msg)` instead of `vfprintf(stderr, …)` + `abort()`.
- `fpconv.c`: the two `abort()` sites call `lua_cjson_fatal(...)`.
- `lua_cjson.c`: the two `abort()` sites call `lua_cjson_fatal(...)`.
- Two 1-line `extern void lua_cjson_fatal(const char *);` declarations (in
  `strbuf.c` and `fpconv.c`).

Result: an OOM or invariant violation becomes a normal Lua error
(`json: Out of memory`) that the engine's protected call reports to stderr, the
script fails with exit code 1, and the process stays up — exactly the redis/mysql
failure semantics. The message still reaches stderr through the engine's own
`fprintf(stderr, "script: %s", …)` reporting.

### 10.3 Known trade (accepted)

Upstream's `abort()` "leaks nothing" only because the process dies. Raising a
Lua error longjmps past the in-flight `strbuf` frees, so a genuine OOM can leak
one bounded transient buffer (`json.tmp` on decode, or a local encode buffer)
plus the `lua_State`. That is deliberate and acceptable: OOM is exceptional, the
buffer is ≤ input size, and the CLI exits after reporting the error. A zero-leak
fix would require refactoring `strbuf` to return status codes through ~30 call
sites — disproportionate for an OOM path.

### 10.4 Drop-in preservation

This customization breaks "overwrite the vendor files verbatim." To keep future
vendor bumps easy, the `json/` directory carries a `README.md` recording (a) the
vendored version + provenance and (b) the exact customization — the funnel, the
state capture, and the call-site substitutions — as a step-by-step patch to
re-apply after any upstream update. The customization is one concept and ~7
mechanical edits, so re-application is minutes, not a merge.

Naming: the funnel is `lua_cjson_fatal` (not `UfsrvScriptJson*`) because it
lives inside and is called by the vendored core. It is *our* code but
*vendor-shaped*, and the `README.md` marks it as the one sanctioned touch.

## 11. Implementation phases (authorization gates)

1. **Vendor + seam + build** — copy the five MIT files byte-for-byte into
   `src/lua_bindings/json/`; write `ufsrvscriptlib_json.c`/`.h` (the seam, §7.1);
   add the four sources to `../../CMakeLists.txt`; clean
   `asan-ubsan` build. *(gate)*
2. **Register + smoke** — register in `ufsrvscriptlib_cli.c`; live smoke
   `json.encode`/`json.decode` (incl. a nested object, a `json.null` round-trip,
   and a `nil, errmsg` on bad input) via `--eval-script`. *(gate)*
3. **Adversarial hardening** — encode/decode edge cases under ASan+UBSan+LSan:
   excessive nesting (depth-limit trip), NaN/Inf, integers past 2⁶³−1 (verify no
   silent saturation), sparse arrays, empty table as object vs array, surrogate
   pairs, invalid escapes, trailing garbage after a value, empty input, encode of
   a `mysql` connection userdata (must error), `json.new()` instance isolation,
   `__gc` after a freed config. *(gate)*
4. **Docs** — `lua_bindings/README.md` module row + the main design-doc sync
   (quick-guide tables, pipeline diagram, native-modules layout, Appendix B
   code) + HOWTO/man if a JSON example is warranted + Version Management bumps.
   *(gate)*

## 12. Provenance

Adapted from **lua-cjson** — the OpenResty fork (`openresty/lua-cjson`, tag
`2.1.0.17`) of Mark Pulford's `mpx/lua-cjson` (`lua_cjson.c`, `strbuf.c`,
`fpconv.c`; MIT — `Copyright (c) 2010-2012 Mark Pulford <mark@kyne.com.au>`).
The vendored core is copied **pristine** (only our seam file is authored here);
the seam supplies the `UfsrvScriptJsonRegister` entry, the `json` global, and
the `nil, errmsg` error convention (§6–§7). The `redis` module (lua-hiredis,
BSD-3-Clause) established the "uniform seam over an imported core" pattern this
module follows; the `mysql` module (LuaSQL, MIT) is the second instance. The
internal dtoa path (`dtoa.c`/`g_fmt.c`, David M. Gay/Lucent) is intentionally
not vendored (§3).
