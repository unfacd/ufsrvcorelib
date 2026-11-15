# ufsrvscriptlib — MySQL/MariaDB Native Module Design

## Version Management

| Service Name | Date Created | Created By |
|---|---|---|
| ufsrvscriptlib | 2026-09-05 | devops |

| Version | Date Modified | Modified By | Description |
|---|---|---|---|
| 1.0.0 | 2026-09-05 | devops | Initial design — native MySQL/MariaDB Lua module for the scripting engine |
| 1.1.0 | 2026-09-05 | devops | Encrypted-at-rest mysql password (vendored ufsrv_config_secret bridge) + secret_key_path |
| 1.2.0 | 2026-09-06 | devops | Add ssl_verify config + connect arg (Connector/C 3.4 now requires TLS+verify by default) |

## 1. Purpose

Give `--eval-script` scripts read/write access to ufsrv's MariaDB backend
(`ufsrv-db`) through a native C Lua module, in the same way the `redis` module
exposes the Redis backends. The module is a faithful port of LuaSQL's MySQL
driver (`ls_mysql.c`, MIT — see §9 Provenance), adapted to the `ufsrvscriptlib`
interface contract established by the Redis module.

The reference implementation to model on is the Redis module
(`../../src/lua_bindings/redis/ufsrvscriptlib_redis.c`) and the
contract in `../../src/lua_bindings/README.md`.

## 2. Dependency — system MariaDB Connector/C

Unlike Redis (whose `hiredis` had to be vendored because it is not a system
dependency), the MySQL client library **already ships under `/opt`** — the same
place Lua 5.4 lives, and it is already consumed by `uflib`
(`uflib/db/db_sql.h` includes `<mariadb/mysql.h>`):

| Artifact | Path |
|---|---|
| Headers | `/opt/include/mariadb/mysql.h` (et al.) |
| Library | `/opt/lib/mariadb/libmariadb.so.3`, `libmariadbclient.a` / `libmariadb.a` (static) |
| Tool | `/opt/bin/mariadb_config` (client `10.8.8`, Connector/C `3.4.9`) |

**Decision: link the system library — no vendoring.** The module `find_path` /
`find_library`s it under `/opt`, exactly as the engine already does for Lua.

**Link the shared `libmariadb.so`**, mirroring `ufnetcorelib`
(`find_library(MARIADB_LIB mariadb PATHS /opt/lib /opt/lib/mariadb)`). The
static `libmariadb.a` is built *with* OpenSSL, so a static link would drag in
`-lssl`/`-lcrypto` (and zlib) transitively — an unneeded complication. CMake's
default build RPATH records the directory of the linked shared object
(`/opt/lib/mariadb`) in the consumer's `RUNPATH` — `ufsrvwebsock` already shows
`RUNPATH: /opt/lib:/opt/lib/mariadb` — so the shared lib resolves at run time
without touching `ld.so.conf`.

## 3. Object model — two tiers (connection, result)

LuaSQL's driver has **three** tiers: environment → connection → cursor. We drop
the **environment** tier. In LuaSQL it exists only to (a) own
`mysql_library_init`/`mysql_library_end` and (b) provide the `connect` factory.
Both are wrong for a shared engine state:

- MariaDB Connector/C auto-initialises on first `mysql_init()`.
- `mysql_library_end()` is **process-global** teardown — calling it on every
  environment's `__gc` would invalidate every other live connection.

So `connect()` lives directly on the module table, mirroring
`redis.connect(host, port)`:

```lua
local c = mysql.connect(backend.mysql.host, backend.mysql.port,
                        backend.mysql.user, backend.mysql.password,
                        backend.mysql.database)
local r = c:query("SELECT id, name FROM users LIMIT 10")   -- result userdata
local row = r:fetch()                                       -- { id = 1, name = "x" }
local all = r:fetchall()                                    -- { {…}, {…}, … }
c:query("INSERT INTO users (name) VALUES ('x')")            -- 1 (affected rows)
c:close()
```

### 3.1 Connection (`ufsrvscript.mysql.connection`)

Userdata wrapping `MYSQL *`, created by `mysql.connect(host, port, user,
password, database)`.

| Method | Returns |
|---|---|
| `:query(sql)` | **result** userdata (SELECT) or **affected-rows integer** (INSERT/UPDATE/DELETE) |
| `:escape(str)` | escaped string (`mysql_real_escape_string`) |
| `:commit()` | boolean (`!mysql_commit`) |
| `:rollback()` | boolean (`!mysql_rollback`) |
| `:set_autocommit(bool)` | boolean |
| `:last_insert_id()` | integer (`mysql_insert_id`) |
| `:affected_rows()` | integer (`mysql_affected_rows`) |
| `:ping()` | boolean (`mysql_ping == 0`) |
| `:close()` / `__gc` | idempotent `mysql_close` (guard against `:close()` then GC) |
| `__tostring` | `"ufsrvscript.mysql.connection"` |

### 3.2 Result (`ufsrvscript.mysql.result`)

Userdata wrapping `MYSQL_RES *` plus the owning `MYSQL *` (needed for
`mysql_errno` and lengths), created by `:query()` on a SELECT. Column names and
types are captured eagerly at creation from `mysql_fetch_fields`.

| Method | Returns |
|---|---|
| `:fetch()` | next row as a named table `{ colname = value, … }`, or `nil` at end |
| `:fetchall()` | array of named-row tables |
| `:columns()` | array of `{ name = "…", type = "…" }` records (in column order) |
| `:numrows()` | integer (`mysql_num_rows`) |
| `:close()` / `__gc` | idempotent `mysql_free_result` |
| `__tostring` | `"ufsrvscript.mysql.result"` |

## 4. Type-aware value conversion

LuaSQL pushes **every** column value as a string. We instead convert by
`MYSQL_FIELD.type`, matching the Redis module's type-aware reply conversion:

| `MYSQL_TYPE_*` | Lua value |
|---|---|
| TINY / SHORT / LONG / LONGLONG / INT24 / YEAR | integer (`strtoll`) |
| FLOAT / DOUBLE | number (`strtod`) |
| NULL | `nil` |
| TINY_BLOB / MEDIUM_BLOB / LONG_BLOB / BLOB / BIT | binary string (`lua_pushlstring`) |
| DECIMAL / NEWDECIMAL | string (precision-preserving) |
| everything else (STRING, VAR_STRING, ENUM, SET, DATE, TIME, DATETIME, TIMESTAMP, NEWDATE) | string |

A NULL column value becomes `nil`, so the key is simply absent from the named
row table — the standard Lua idiom, and consistent with LuaSQL's own
`pushvalue` (which also pushes `nil` for a NULL cell).

## 5. Error convention

Every failure returns `nil, mysql_error(conn), mysql_errno(conn)` — the exact
analog of the Redis module's `nil, errstr, errno`. Module functions are called
directly by Lua (not through `sDispatch`), so the multi-return form is used
consistently throughout this module, per the contract.

`connect()` failure (e.g. refused port): `mysql_real_connect` returns NULL →
`nil, mysql_error(conn), mysql_errno(conn)`, then `mysql_close(conn)`.

## 6. Module contract

The module follows the `lua_bindings/README.md` contract verbatim:

- Global: `mysql` (set via `lua_setglobal` from C — exempt from strict.lua).
- Entry point: `PUBLIC_API void UfsrvScriptMysqlRegister(lua_State *L);`
- Metatables: `ufsrvscript.mysql.connection`, `ufsrvscript.mysql.result`.
- Public symbols `UfsrvScriptMysql*`; `s`-prefixed `static` helpers.
- Registered by the host after `UfsrvScriptBootstrap()`:
  `UfsrvScriptMysqlRegister(UfsrvScriptLuaState(lua));`

### 6.1 Thin wrapper seam (Lua-agnostic)

```c
MYSQL            *UfsrvScriptMysqlConnect(const char *host, const char *user,
                                          const char *pass, const char *db, unsigned int port);
int               UfsrvScriptMysqlQuery(MYSQL *c, const char *sql, size_t len);
MYSQL_RES        *UfsrvScriptMysqlStoreResult(MYSQL *c);
MYSQL_ROW         UfsrvScriptMysqlFetchRow(MYSQL_RES *r);
unsigned long    *UfsrvScriptMysqlFetchLengths(MYSQL_RES *r);
MYSQL_FIELD      *UfsrvScriptMysqlFetchFields(MYSQL_RES *r);
unsigned int      UfsrvScriptMysqlFieldCount(MYSQL *c);
unsigned long long UfsrvScriptMysqlAffectedRows(MYSQL *c);
unsigned long long UfsrvScriptMysqlInsertId(MYSQL *c);
unsigned int      UfsrvScriptMysqlErrno(MYSQL *c);
const char       *UfsrvScriptMysqlError(MYSQL *c);
int               UfsrvScriptMysqlEscape(MYSQL *c, const char *in, size_t n, char **out);
int               UfsrvScriptMysqlPing(MYSQL *c);
int               UfsrvScriptMysqlCommit(MYSQL *c);
int               UfsrvScriptMysqlRollback(MYSQL *c);
int               UfsrvScriptMysqlAutocommit(MYSQL *c, int mode);
void              UfsrvScriptMysqlFreeResult(MYSQL_RES *r);
void              UfsrvScriptMysqlClose(MYSQL *c);
```

### 6.2 Layout

| Concern | Location |
|---|---|
| Implementation | `../../src/lua_bindings/mysql/ufsrvscriptlib_mysql.c` |
| Public header | `../../include/ufsrvscriptlib/ufsrvscriptlib_mysql.h` |
| Build wiring | `../../CMakeLists.txt` (source + `mariadb` link) |
| Host registration | `../../../../src/ufsrvscriptlib/ufsrvscriptlib_cli.c` (`UfsrvScriptCliBootstrap`) |

## 7. Config wiring — `backend.mysql.*` (+ encrypted password)

ufsrv has **one** MariaDB backend (`ufsrv-db` at `127.0.0.1:33061`, database
`ufsrv`, user `ufsrv`), so the config shape is a single backend, not three like
Redis. The redis phase-3 pattern is extended:

- `../../../../etc/ufsrvcli.config.lua`:
  ```lua
  mysql_backend = {
      host     = "127.0.0.1",
      port     = "33061",
      user     = "ufsrv",
      password = "",      -- plaintext (dev), or "enc:v1:<nonce>:<ct>:<tag>"
      database = "ufsrv",
      ssl_verify = false  -- Connector/C 3.4 requires TLS+verify by default
  },
  secret_key_path = "/opt/ufsrv/etc/secrets/ufsrv_secret_key"
  ```
- `ufsrvcli_config.h`: `MysqlBackend { unsigned port; char host[256], user[64],
  password[128], database[64]; int ssl_verify; }` plus a `MysqlBackend mysql;`
  field and a `char secret_key_path[512]` field.
- `ufsrvcli_config.c`: defaults + load from `mysql_backend.*`; the password is
  decrypted when it is `enc:v1:`-prefixed (see §7.1).
- `ufsrvscriptlib_cli.c` (`UfsrvScriptCliBootstrap`): six constants —
  `backend.mysql.host/.user/.password/.database` (STRING), `backend.mysql.port`
  (INTEGER), and `backend.mysql.ssl_verify` (BOOLEAN). `mysql.connect()` takes an
  optional sixth argument `ssl_verify` (default `false`); `false` disables TLS
  peer verification so plaintext servers connect (Connector/C 3.4 enables
  verification by default), `true` restores it for TLS-verified servers.

### 7.1 Encrypted password at rest

The `mysql_backend.password` may be stored **encrypted** rather than plaintext,
using the same AES-256-GCM scheme as the ufsrv servers. A value
`enc:v1:<hex_nonce>:<hex_ciphertext>:<hex_tag>` is decrypted at config-load time
with a single application-wide 32-byte key held in a credentials file — never in
the config. Plaintext values pass through unchanged (transparent).

- **Key file**: `secret_key_path` (config value), default
  `/opt/ufsrv/etc/secrets/ufsrv_secret_key`, overridden by the
  `$CREDENTIALS_DIRECTORY/ufsrv_secret_key` systemd credential when set.
- **Encrypt helper**: `ufsrvcorelib/dev/encrypt_plaintext/encrypt_plaintext.py`.
- **Decrypt/read-key**: `../../../../src/ufsrvcli_secret.c` (`UfsrvCliSecretDecrypt`,
  `UfsrvCliSecretReadKey`) — a **vendored port** of ufsrvcorelib's
  `ufsrv_config_secret.c` (`SecretConfigDecrypt`/`SecretConfigReadKeyFromCredential`),
  adding a direct `-lcrypto` link (OpenSSL is already in the runtime graph via
  `libmariadb.so.3`).

> **Pivot note:** ufsrvcli does not (yet) link ufsrvcorelib, so the two secret
> functions are copied rather than imported. ufsrvcorelib is the strategic home
> for these primitives, and the CLI's local config loader is a stopgap destined
> to be replaced by ufsrvcorelib's `configfile_processor`. When that pivot lands,
> `ufsrvcli_secret.c` is dropped and the real ufsrvcorelib secret machinery is
> used — the vendored copy is a throwaway bridge, not a fork to maintain.

**Security notes:**

- Exporting `backend.mysql.password` lets any script read the DB password — the
  same exposure as the config file itself (Redis has no credentials, so this is
  new). Accepted for a local ops CLI; flagged for review.
- The decrypted password is still plaintext in memory (a script must pass it to
  `mysql.connect`); the `enc:v1:` scheme only protects it **at rest** in the
  config file.

**Naming note:** the global and config key are `mysql` (matching the `mysql_*`
API, `mysql.h`, and LuaSQL), even though the server is MariaDB;
`mariadb_backend`/`mariadb` is the server-accurate alternative if preferred.

## 8. Implementation phases (authorization gates)

1. **Dependency + build** — `find_path`/`find_library` (shared `libmariadb.so`)
   in `../../CMakeLists.txt`; stub `ufsrvscriptlib_mysql.c/.h`
   (register an empty `mysql` table + `_CLIENTVERSION` from
   `mysql_get_client_info()`); clean `dev`-preset build. *(gate)*
2. **Module** — full object model + register in `ufsrvscriptlib_cli.c`; smoke
   against live `ufsrv-db` (127.0.0.1:33061). *(gate)*
3. **Config constants** — `MysqlBackend` struct + defaults + load +
   `backend.mysql.*`; vendor `ufsrvcli_secret.c` (AES-256-GCM decrypt) + link
   `-lcrypto`; decrypt `enc:v1:` password via `secret_key_path`; verify config
   override + decrypt round-trip. *(gate)*
4. **Docs** — `lua_bindings/README.md` row + HOWTO §3.1 + man CONFIG FILE +
   design doc + Version Management bumps.

## 9. Provenance

Adapted from **LuaSQL** (`ls_mysql.c`, "LuaSQL, MySQL driver", author Eduardo
Quintao; MIT license — `Copyright (C) 2003-2026 Lunar Modules Project`), with
the adaptations documented in §1–§7: Lua 5.4 API, the environment tier removed,
type-aware value conversion, and the `UfsrvScriptMysql*` wrapper seam. The
`redis` module (`lua-hiredis`, BSD-3-Clause) established the `ufsrvscriptlib`
native-module conventions this module follows.
