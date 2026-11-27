# ufsrvcorelib

Shared C runtime for ufsrv servers. It provides protocol registration, the base
session and worker runtime, HTTP primitives, and OAuth and JWT modules.
`ufsrvapi` and `ufsrvwebsock` supply their own protocol callbacks and application policy.

## Modules

| Location | Main responsibilities |
|---|---|
| `src/ufsrv_core/protocol` | `Protocol`, lifecycle callbacks and runtime controls. Hosts install a registry with `InitProtocolsForUfsrv()`. |
| `src/ufsrv_core/session`, `delegator_sessions` | `UfsrvSession`, session I/O, readiness dispatch, session and job worker pools, and per-thread context. `InitialiseDelegator()` accepts host-supplied pool, backend and callback specs. |
| `src/ufsrv_core/http` | HTTP request parsing, Onion handler primitives, outbound HTTP and in-place `HttpSession` storage. |
| `src/ufsrv_core/oauth`, `jwt` | OAuth client metadata, grants, token issuance, rotation, introspection and revocation. JWT handles keysets, signing, verification and JWKS. OAuth persists state in MariaDB and uses `accounts.ufsrvuid` for user JWT subjects. |
| `src/ufsrv_core/ufsrv_config`, `configfile_processor`, `cli_configurator` | Schema-driven configuration, generated accessors, Lua config processing and command-line configuration. |
| `src/ufsrv_core/command_console`, `src/ufsrv_console_script` | Command console registration and dispatch, including Lua scripting. |
| `src/ufsrv_core/service`, `cache_backend`, `ratelimit_v2`, `instrumentation`, `system` | File loading, timer scheduling, backend interfaces, rate limiting, metrics and process utilities. |

Public headers live under `include/ufsrvcorelib/`. The [worker design](dev/technical_designs/1.%20UFSRV_DELEGATOR_TO_WORKER_DESIGN.md)
covers pool startup, signalling and thread-local state.

## Dependencies

- `uflib` provides low-level libraries such as the recycler, database helpers, and data structures.
- Corelib links Lua, json-c, libcurl, OpenSSL, MariaDB, jemalloc, systemd and threads.
- Hiredis, csonpath and ufsrvscriptlib are available under `packages/`.
- Netcorelib builds on corelib. Core-only hosts can use the protocol, HTTP and OAuth modules directly.

## Build

```bash
cmake --preset release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build/release
cmake --build build/release --target package   # optional .deb
```

## Consuming

```cmake
find_package(ufsrvcorelib REQUIRED)
target_link_libraries(app PRIVATE ufsrvcorelib::ufsrvcorelib)
```

or

```bash
pkg-config --cflags --libs ufsrvcorelib
```

The install tree carries the headers, `config_ufsrvcorelib.h`, `ufsrvcorelib.pc`, and the
CMake package files. Both `libufsrvcorelib.a` and `libufsrvcorelib.so` are built.

## Versioning

`MAJOR.MINOR.PATCH` is the release identity. `UFSRVCORELIB_INTERNAL` is a gate counter
that moves during development and is reset to zero at release; it is not part of the tag.

## License

Not yet declared in this tree — there is no `LICENSE` file.
