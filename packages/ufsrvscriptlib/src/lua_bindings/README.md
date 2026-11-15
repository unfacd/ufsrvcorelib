# ufsrvscriptlib — native Lua modules (`lua_bindings`)

This directory holds the **reusable native Lua modules** for the ufsrvscriptlib
engine — self-contained C modules that expose a Lua table (and, where needed,
userdata/metatable objects with methods) to scripts, over the engine's flat
function-binding and constant mechanisms.

Authoritative design reference:
`../../dev/technical_designs/UFSRVCORELIB_UFSRVSCRIPT_DESIGN.md` → "Native Lua modules".

## Existing modules

| Module | Global | Object model |
|---|---|---|
| redis | `redis` | `connect(host, port)` → userdata connection with `:command`/`:append_command`/`:get_reply`/`:close` |
| mysql | `mysql` | `connect(host, port, user, password, database, [ssl_verify])` → userdata connection with `:query`/`:escape`/`:commit`/`:rollback`/`…`; result userdata with `:fetch`/`:fetchall`/`:columns`/`:numrows` |
| json | `json` | stateless codec — `encode`/`decode` (+ `null`, `empty_array`, `array_mt`, `new`, settings); no userdata |
| protobuf | `pb`, `protoc`, `serpent` | codec + schema registry — `pb.encode`/`decode`/`load`, `protoc.new()`/`:load(text)`; `pb.State` userdata with `__gc`/`setdefault` |

## Interface contract

A module **MUST**:

1. Expose one entry point — `PUBLIC_API void UfsrvScript<Module>Register(lua_State *L);`
   — that builds the module table and sets it with `lua_setglobal(L, "<module>")`.
2. Be registered by the host after `UfsrvScriptBootstrap()`, via
   `UfsrvScriptLuaState(lua)`:
   ```c
   UfsrvScriptRedisRegister(UfsrvScriptLuaState(lua));
   ```
3. Name metatables `ufsrvscript.<module>.<type>` and free owned resources in an
   idempotent `__gc` metamethod (guard against user `:close()` then GC).
4. Prefix public symbols `UfsrvScript<Module>*`; use `s`-prefixed `static`s.

A module **MUST NOT**:

- Rely on registration order, or share state with other modules.
- Raise errors outside the `{ err = "..." }` / `nil, errstr, errno` conventions.
- Claim another module's global name.

## Object-model idiom (stateful handles)

- `lua_newuserdata` handle + metatable with `__index` → method table and a
  `__gc` that frees the C handle.
- Typed replies as **const objects** (`{ name, type }` table with a metatable),
  with an `unwrap_reply` companion.
- Reach the module table with `lua_getglobal("<module>")`, not the fragile
  caller-upvalue (`lua_upvalueindex`) idiom.

## Layout

```
src/lua_bindings/<name>/ufsrvscriptlib_<name>.c      # implementation
include/ufsrvscriptlib/ufsrvscriptlib_<name>.h          # public header
```

Wire new modules into `packages/ufsrvscriptlib/CMakeLists.txt` (source + any
dependency, e.g. `hiredis`, `mariadb`; `json` vendors a pure-C MIT core with no
external dependency), and register them in the host's
`src/ufsrvscriptlib/ufsrvscriptlib_cli.c`.

The redis module (`redis/ufsrvscriptlib_redis.c`) is the reference implementation;
the mysql module (`mysql/ufsrvscriptlib_mysql.c`) is the second instance; the json
module (`json/ufsrvscriptlib_json.c`) is the third (a stateless codec, seam-only);
the protobuf module (`protobuf/ufsrvscriptlib_protobuf.c`) is the fourth (a codec
+ schema registry whose seam also loads two embedded pure-Lua companions).
