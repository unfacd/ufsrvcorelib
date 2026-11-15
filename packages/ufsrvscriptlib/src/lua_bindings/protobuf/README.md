# ufsrvscriptlib — Protocol Buffers native module (`lua_bindings/protobuf`)

A thin interface seam (`ufsrvscriptlib_protobuf.c` / `.h`) over the **vendored
lua-protobuf** core, kept in its own `lua_protobuf/` subdirectory. Protobuf is a
codec-plus-schema-registry, so the seam composes the vendored openers into the
`pb` global and loads the two pure-Lua companions (`protoc`, `serpent`) in a
private `_ENV` (see
`../../../dev/technical_designs/UFSRVCORELIB_UFSRVSCRIPT_PROTOBUF_DESIGN.md`).

## Provenance (hard dependency)

| Item | Value |
|---|---|
| Upstream project | [lua-protobuf](https://github.com/starwing/lua-protobuf) |
| Version | `0.5.3-5-gbbe744f` (5 commits past tag `0.5.3`) |
| Baseline commit | `bbe744fd25738cccbda973e8fbccdf32c0fb4f9d` — `fix: buffer.pack("c") with zero-length slice` |
| License | MIT — `Copyright (c) 2018 Xavier Wang` |
| Vendored files | `lua_protobuf/pb.c`, `pb.h`, `protoc.lua`, `serpent.lua` |
| Companion origin | `serpent.lua` — [pkulchenko/serpent](https://github.com/pkulchenko/serpent) `0.30`, MIT, `Copyright (c) 2012-17 Paul Kulchenko` |
| Excluded | `test.lua`, `luaunit.lua` (test-only) |
| Copied on | 2026-09-07 |

These are third-party sources: **do not edit them in place**.

## Update procedure (vendor bump)

All customizations live in the seam and the build — **not** in the vendored
files — so a bump is: copy the four files, rebuild, re-run the tests.

```bash
# 1. Fetch the baseline to vendor and record its SHA in the table above.
git clone https://github.com/starwing/lua-protobuf.git /tmp/lua-protobuf
git -C /tmp/lua-protobuf checkout <commit-sha>

# 2. Copy the four runtime files verbatim (test.lua/luaunit.lua are excluded).
cp /tmp/lua-protobuf/{pb.c,pb.h,protoc.lua,serpent.lua} \
   packages/ufsrvscriptlib/src/lua_bindings/protobuf/lua_protobuf/

# 3. Rebuild + re-run the tests — they catch API drift (renamed openers, a moved
#    pb.loadfile, or new UB patterns that need a per-TU flag). The rebuild
#    re-runs configure by itself: the two .lua files are CONFIGURE_DEPENDS, and
#    the byte arrays are generated during configure, not during the build.
cmake --build --preset asan-ubsan
cmake --build build/asan-ubsan-tests --target ufsrvscriptlib_protobuf_test
./tests/lua/run.sh pb
```

Then re-check the "Customizations" items below against the new baseline:
`luaopen_pb_buffer`/`luaopen_pb_slice`/`luaopen_pb_conv` still exist, `pb.loadfile`
is still a core field (so the nil is still meaningful), and the per-TU flags are
still warranted (upstream may have fixed the hash/slice UB).

## Hard constraints

- **Lua 5.4** only: `pb.c` uses `lua_pushexternalstring` (5.4). The engine pins
  Lua 5.4, so this holds; revisit if the engine's Lua is ever downgraded.
- **Single `lua_State` per process**: lua-protobuf keeps a module-level
  `global_state` for `pb.unsafe.use("global")`. The CLI runs one VM per process,
  so this is safe; it would need rework if concurrent VMs are ever introduced.

## Customizations (re-apply after any vendor bump)

These live in the seam and the build, never in the vendored files:

1. **Submodule composition** (`ufsrvscriptlib_protobuf.c`): `pb.buffer`, `pb.slice`
   and `pb.conv` are attached directly (upstream loads them via
   `require "pb.buffer"` etc.). `pb.io` and `pb.unsafe` are **not** attached, and
   the core `pb.loadfile` is **nil'd** — they re-open a filesystem-write vector,
   a raw-pointer escape and a filesystem-read vector the sandbox deliberately
   closes (design §10).
2. **`require` bridge** (`ufsrvscriptlib_protobuf.c`): `protoc.lua` does
   `pcall(require, "pb")` with no global fallback. The seam loads the companions
   in a private `_ENV` whose `require` resolves only `pb`, and whose `__index`
   rawgets `_G` (so the strict-globals guard doesn't reject the companions'
   own `local io = io`). The sandbox `package` library stays closed.
3. **Per-TU flags for `pb.c` only** (`packages/ufsrvscriptlib/CMakeLists.txt`,
   `set_source_files_properties`):
   - `NDEBUG` — turns the vendored `assert()` invariant checks into no-ops so a
     can't-happen assert can't `abort()` the host. Its OOM path already raises
     `luaL_error` — unaffected.
   - `-Wno-implicit-fallthrough` — pb.c's intentional `/* FALLTHROUGH */` into
     `default:` trips the repo-wide warning (clang reads only the attribute).
   - `-fno-sanitize=signed-integer-overflow` and
     `-fno-sanitize=pointer-overflow` — the Knuth multiplicative hash wraps, and
     `pb_lslice(NULL, 0)` computes `NULL + 0`; both are benign and uneditable.
     ASan (memory safety) and the other UBSan checks stay active.
4. **Companion embedding** (`packages/ufsrvscriptlib/CMakeLists.txt`): `xxd -i`
   converts `protoc.lua`/`serpent.lua` into byte-array headers **during
   configure**, into the build tree and privately, so the binary is
   self-contained (no runtime path lookup) and a configure alone yields a tree
   that parses. The two `.lua` files are `CMAKE_CONFIGURE_DEPENDS`, so a vendor
   bump re-runs configure and rebuilds the bytes; nothing is committed or
   installed.
