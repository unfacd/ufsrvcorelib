# Change Log — ufsrvcorelib

## Release-261127 — 2.0.0
`0ba9647` → `6f4f8f1` · 2026-11-15 → 2026-11-27

### New
- **SFU media server** — SFU in a box: nearly full implementation of typical lightweight server with built-in signalling mode
- **STUN/TURN** — STUN/TURN in a box:  very functional implementation
- **HTTP and REST** — the HTTP server layer moved here from `ufnetcorelib`, with inbound parsing and session state
- **Configuration** — Lua lookups walk dotted paths into nested tables, typed listener collections, unsigned 32-bit range validators, schema nodes accept a `validate` callback, and new literals cover `sfu`, `rest_server`, `turn` and `rooms`
- **JWT and OAuth** — the runtime moved into core, Ed25519 public keys are extracted from PEM, and stored revocation state is enforced on introspection
- **Core absorption** — the delegator transport, the session partition type, workers, cache, the rate limiter, listener signal setup and the console transport now live here

### Fixes

### Deprecated
- (none)

### Removed

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
