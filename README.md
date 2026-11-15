# ufsrvcorelib

Companion foundational C library for the **ufsrv** messaging-server platform. It has no
`main()`: it provides the primitives and composables that ufsrv-class servers — ufsrvapi,
ufsrvwebsock — are assembled from.

## Modules

| Module | Purpose |
|---|---|
| `ufsrv_config` | Schema-driven configuration: a Lua catalogue drives generated field tables, accessors and lifecycle callbacks, with runtime stores, defaults and source tracking |
| `configfile_processor` | Lua-style configuration file parsing — symbol tables, resolution and dumping |
| `ufsrv_console_script` | Command console with dynamic registration and `EVAL` dispatch over a sandboxed Lua engine (`ufsrvscriptlib`) |
| `cli_configurator` | Command-line configuration entry point (`--config=<op>`) |
| `fileloader` | Descriptor-driven file loading, registry documents and alias maps |
| `system`, `instrumentation` | CPU-affinity facets, jemalloc introspection, logger settings and `UFSRV` L2/L3 debug switches |

## Requirements

- C17 toolchain (Clang) and CMake ≥ 3.25
- `uflib` (required)
- Lua 5.4, json-c, jemalloc, systemd, OpenSSL, and a MariaDB client
- `hiredis`, `csonpath` and `ufsrvscriptlib` are vendored under `packages/` and built in-source
  by default; `ufsrvscriptlib` can also be supplied by `find_package()`

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
