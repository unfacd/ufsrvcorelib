# packages/ufsrvscriptlib — reusable, class-agnostic Lua scripting engine for ufsrv.
#
# Self-contained module following UF_SAMPLE_SANDBOX_REPO_STRUCTURE.md:
# public headers under include/ufsrvscriptlib/, private implementation under src/.
#
# The core engine is dependency-clean: it links only against the system Lua 5.4
# (liblua under /opt) and uflib (solely for the PUBLIC_API visibility macro).
# The native Lua bindings (src/lua_bindings/) pull in their own backends: redis
# links the vendored hiredis; mysql links the system MariaDB Connector/C (shared
# libmariadb.so under /opt); json vendors the MIT lua-cjson core (no new link
# dependency). There is no ufsrvapi/ufsrvwebsock/curl coupling.
# Built as a static library.

find_package(uflib REQUIRED)

find_package(Lua 5.4 REQUIRED)
set(UFSRVSCRIPTLIB_LUA_INCLUDE_DIR ${LUA_INCLUDE_DIR})
set(UFSRVSCRIPTLIB_LUA_LIBRARY ${LUA_LIBRARIES})

find_package(MariaDB REQUIRED)
set(UFSRVSCRIPTLIB_MARIADB_INCLUDE_DIR ${MariaDB_INCLUDE_DIRS})
set(UFSRVSCRIPTLIB_MARIADB_LIBRARY ${MariaDB_LIBRARIES})

# ── lua-protobuf companions — embed protoc.lua / serpent.lua at configure time ──
# The pure-Lua companions ship as pristine vendored files; xxd -i turns each into
# a byte-array header (protoc_lua[]/protoc_lua_len, serpent_lua[]/… ) that the
# seam #includes, so the binary stays self-contained and needs no runtime path.
#
# Generated during configure, into the build tree and privately: a configure
# alone then leaves a tree the CodeChecker analyser can parse, since it does not
# build. The .lua inputs are CONFIGURE_DEPENDS, so a vendor bump re-runs
# configure and the bytes are rebuilt — coarse regeneration is the contract this
# rule depends on. Nothing here is committed, installed or visible to a consumer.
find_program(UFSRVSCRIPTLIB_XXD xxd)
if(NOT UFSRVSCRIPTLIB_XXD)
    message(FATAL_ERROR "xxd (vim-common) is required to embed the lua-protobuf companions")
endif()

set(UFSRVSCRIPTLIB_PROTOBUF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/lua_bindings/protobuf)
set(UFSRVSCRIPTLIB_PROTOBUF_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/lua_bindings/protobuf)

file(MAKE_DIRECTORY ${UFSRVSCRIPTLIB_PROTOBUF_GEN_DIR})

foreach(companion IN ITEMS protoc serpent)
    execute_process(
        COMMAND ${UFSRVSCRIPTLIB_XXD} -i -n ${companion}_lua
                ${UFSRVSCRIPTLIB_PROTOBUF_DIR}/lua_protobuf/${companion}.lua
                ${UFSRVSCRIPTLIB_PROTOBUF_GEN_DIR}/${companion}_lua.h
        COMMAND_ERROR_IS_FATAL ANY)
    list(APPEND UFSRVSCRIPTLIB_PROTOBUF_GEN_HEADERS
        ${UFSRVSCRIPTLIB_PROTOBUF_GEN_DIR}/${companion}_lua.h)
endforeach()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${UFSRVSCRIPTLIB_PROTOBUF_DIR}/lua_protobuf/protoc.lua
    ${UFSRVSCRIPTLIB_PROTOBUF_DIR}/lua_protobuf/serpent.lua)

add_library(ufsrvscriptlib STATIC
    src/ufsrvscriptlib_lua.c
    src/ufsrvscriptlib_eval.c
    src/lua_bindings/redis/ufsrvscriptlib_redis.c
    src/lua_bindings/mysql/ufsrvscriptlib_mysql.c
    src/lua_bindings/json/ufsrvscriptlib_json.c
    src/lua_bindings/json/lua_cjson/lua_cjson.c
    src/lua_bindings/json/lua_cjson/strbuf.c
    src/lua_bindings/json/lua_cjson/fpconv.c
    src/lua_bindings/protobuf/ufsrvscriptlib_protobuf.c
    src/lua_bindings/protobuf/lua_protobuf/pb.c
    ${UFSRVSCRIPTLIB_PROTOBUF_GEN_HEADERS}
)

# A static archive is linked into ufsrvcorelib.so when BUILD_SHARED_LIBS is on,
# which requires position-independent objects — the same reason the other
# vendored archives set this. Without it the shared link fails with
# "relocation R_X86_64_PC32 ... can not be used when making a shared object".
set_target_properties(ufsrvscriptlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(ufsrvscriptlib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${UFSRVSCRIPTLIB_LUA_INCLUDE_DIR}
        ${UFSRVSCRIPTLIB_MARIADB_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${UFSRVSCRIPTLIB_PROTOBUF_GEN_DIR}
)

# The public header includes <lua.h> (the binding function type) and
# <uflib/uflib_defs.h> (PUBLIC_API); link both PUBLIC so consumers inherit the
# include dirs and the Lua archive transitively.
target_link_libraries(ufsrvscriptlib PUBLIC uflib::uflib ${UFSRVSCRIPTLIB_LUA_LIBRARY} m hiredis ${UFSRVSCRIPTLIB_MARIADB_LIBRARY})

# lua-protobuf's pb.c uses assert() for internal invariant checks (not OOM — its
# OOM path already raises luaL_error). Compile it with NDEBUG for this TU only so
# a never-should-happen assert can't abort() the host process; this is a build
# flag, not a vendored-source edit (design §11).
#
# lua-protobuf (vendored, uneditable) trips several benign UBSan checks that we
# cannot fix in place, so disable just those for this TU — ASan (memory safety)
# and the remaining UBSan checks stay active. Mirroring hiredis's
# -Wno-macro-redefined: a vendored-code build flag, not a source edit.
#   -Wimplicit-fallthrough              : intentional /* FALLTHROUGH */ into default:
#   -fno-sanitize=signed-integer-overflow : Knuth multiplicative hash wraps (pbT_hash)
#   -fno-sanitize=pointer-overflow        : pb_lslice(NULL,0) computes NULL+0 on a bad-load path
set_source_files_properties(
    src/lua_bindings/protobuf/lua_protobuf/pb.c
    PROPERTIES
        COMPILE_DEFINITIONS NDEBUG
        COMPILE_OPTIONS "-Wno-implicit-fallthrough;-fno-sanitize=signed-integer-overflow;-fno-sanitize=pointer-overflow")

add_library(ufsrvscriptlib::ufsrvscriptlib ALIAS ufsrvscriptlib)

# The host repo supplies ufsrv_build_options; guard so this package also
# configures cleanly when consumed standalone (where it is unavailable).
if(COMMAND ufsrv_apply_build_options)
    ufsrv_apply_build_options(ufsrvscriptlib)
endif()

# ── Build-time version.c generation (mirrors the ufsrvcli pipeline) ────────
# version.c.sh writes version.c into the package build dir with a fresh
# compilation timestamp on every build. The target name is fixed (not
# PROJECT_NAME-derived) so it does not collide with the host's ufsrvcli_version
# when this package is built as a subdirectory, nor change when the standalone
# bundle is configured as `ufsrvscriptlib`.
add_custom_target(ufsrvscriptlib_version
    COMMAND chmod 755 ${CMAKE_CURRENT_SOURCE_DIR}/src/version.c.sh
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/src/version.c.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.c
    COMMENT "Generating ufsrvscriptlib version.c with a fresh compilation timestamp"
    VERBATIM
)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/version.c PROPERTIES GENERATED TRUE)
target_sources(ufsrvscriptlib PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.c)
add_dependencies(ufsrvscriptlib ufsrvscriptlib_version)
