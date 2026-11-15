# Change Log — ufsrvcorelib

## Release-261115 — 1.9.24
`4ec9cf8` → `d3e6333` · 2026-07-16 → 2026-11-15

First mirror: the tree as of v1.9.24. There is no previous release to diff against.

### New
- **ufsrv_config** — schema-driven configuration: the Lua catalogue generates field tables, accessors and lifecycle callbacks, over a canonical value union with per-value source tracking, consumer-allocated stores and compiled defaults
- **ufsrv_console_script** — command console with dynamic registration, `EVAL` dispatch, and subtree/stem routing, backed by the sandboxed `ufsrvscriptlib` engine
- **configfile_processor** — Lua-style configuration parsing with slim symbol tables, a resolver and a dumper
- **cli_configurator** — command-line configuration through `--config=<op>`
- **fileloader** — descriptor-driven loading from `files_registry`, with the alias map built from the registry documents
- **CPU affinity** — a three-facet descriptor model, `--cpu-affinity-verify`, and thread pinning


### Fixes
- **ufsrv_config generator** — getter/setter types, STRUCT handling, defaults, version management and the verifier's G07/S02 checks
- **Configuration runtime** — `DistinctArray` setters and array marshalling, bool as a first-class value type, and schema consistency across the core and adjunct catalogues
- **Console and CPU** — jemalloc console handlers, and the CPU-affinity layout and out-of-bounds fixes
- **Build** — lua-protobuf companions generated at configure time, and in-source `uflib` resolution for the generator build

### Deprecated
- (none)

### Removed
- `--validate-config`, replaced by `--config=<op>`
- The orphaned `pthread_key_t` string stub and the defunct `ufsrv_ctx_key` field
- The OAuth metadata literals, the intra class name and the defunct protocol id
- Inverted `ufnetcorelib` includes
