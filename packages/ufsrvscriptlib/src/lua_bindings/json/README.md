# ufsrvscriptlib — JSON native module (`lua_bindings/json`)

A thin interface seam (`ufsrvscriptlib_json.c` / `.h`) over the **vendored
lua-cjson** core, kept in its own `lua_cjson/` subdirectory. JSON is a stateless
codec, so the seam is a registration shim, not an API wrapper (see
`../../../dev/technical_designs/UFSRVCORELIB_UFSRVSCRIPT_JSON_DESIGN.md`).

## Provenance

| Item | Value |
|---|---|
| **Baseline version** | [openresty/lua-cjson](https://github.com/openresty/lua-cjson) tag `2.1.0.17` |
| Original | `mpx/lua-cjson` by Mark Pulford |
| License | MIT — `Copyright (c) 2010-2012 Mark Pulford <mark@kyne.com.au>` |
| Vendored files | `lua_cjson/lua_cjson.c`, `strbuf.c`, `strbuf.h`, `fpconv.c`, `fpconv.h` |
| Source `CJSON_VERSION` | `2.1.0.11` |

`dtoa.c` / `g_fmt.c` / `dtoa_config.h` are intentionally **not** vendored — they
only build under `USE_INTERNAL_FPCONV` (a faster internal double→ASCII path) and
carry David M. Gay's Lucent dtoa license plus an endianness/pthread surface. The
default libc path (`fpconv.c`) is what is vendored, keeping the bundle
single-license (MIT).

## Customization (re-apply after any vendor bump)

Two customizations, ~8 mechanical edits total. Re-apply both after overwriting
the vendored files with a new upstream release.

### Customization #1 — fatal funnel (no `abort()`)

1. **`lua_cjson.c`** — add `static lua_State *g_json_state = NULL;` and
   `void UfsrvScriptJsonFatal(const char *msg)` (raises `luaL_error` on the
   captured state, `abort()` only as a can't-happen fallback). Set
   `g_json_state = l;` in `lua_cjson_new()` immediately before `fpconv_init()`.
2. **`lua_cjson.c`** — replace the two `abort()` calls:
   - `json_create_config`: `if (!cfg) abort();` → `UfsrvScriptJsonFatal("Out of memory");`
   - `json_append_string`: `abort(); /* Overflow check */` → `UfsrvScriptJsonFatal("string length overflow");`
3. **`strbuf.c`** — add `extern void UfsrvScriptJsonFatal(const char *msg);`;
   rewrite `die()` to `vsnprintf` into a stack buffer and call
   `UfsrvScriptJsonFatal(msg)` (instead of `vfprintf(stderr)` + `abort()`).
4. **`fpconv.c`** — add the same `extern`; replace the two `abort()` sites
   (`fpconv_update_locale` printf check, `fpconv_strtod` OOM) with
   `UfsrvScriptJsonFatal(...)`.

Search anchor: `grep -n "abort\|die(\|UfsrvScriptJsonFatal" lua_cjson.c strbuf.c fpconv.c`
should show every fatal path routed through the funnel, with no bare `abort()`.

Known accepted trade (design §10.3): raising on OOM longjmps past the in-flight
`strbuf` frees, so a genuine OOM can leak one bounded buffer — acceptable, since
OOM is exceptional and the CLI exits after reporting the error.

### Customization #2 — int64 overflow guard

`strtoll()` silently clamps to `LLONG_MAX`/`LLONG_MIN` on overflow, and the
upstream `PTRDIFF_MIN`/`PTRDIFF_MAX` guard is dead on 64-bit builds — so a JSON
integer past int64 decoded to a corrupt value. Fix in `lua_cjson.c`'s
`json_next_number_token()`:

- Add `#include <errno.h>`.
- Zero `errno` before `strtoll`, and add `|| errno == ERANGE` to the first
  branch so an overflowing integer falls back to the `fpconv_strtod` double
  path (≈9.22e18) instead of clamping.

Search anchor: `grep -n "ERANGE" lua_cjson.c` should show the one guard.
