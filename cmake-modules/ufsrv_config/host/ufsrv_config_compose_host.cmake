#
# cmake-modules/ufsrv_config/host/ufsrv_config_compose_host.cmake
#
# Build-time orchestrator for the ufsrv_config composition pipeline.
# Invoked (via cmake -P) from the ufsrv_config_compose_host custom target:
#
#   cmake -DUFSRV_CONFIG_SOURCE_DIR=... -DUFSRV_CONFIG_BINARY_DIR=... -DUFSRV_CONFIG_C_COMPILER=... \
#         -P cmake-modules/ufsrv_config/host/ufsrv_config_compose_host.cmake
#
# Drives the SHARED composer (compose_ufsrv_config.cmake + join script +
# generator source) fetched from the ufsrvcorelib/ufnetcorelib clones, but at
# BUILD time with execute_process — no add_executable / add_custom_command /
# FetchContent (all configure-time-only).
#
# Sequence:
#   1 clone            git clone ufsrvcorelib + ufnetcorelib
#   2 checkout assets  reuse compose_ufsrv_config_bootstrap() → staging
#   3 compile generator per library (direct compiler call)
#   4 generate         run generator → staging/<lib>/generated/
#   5 compose          reuse compose_ufsrv_config() → scratch
#   6 deep-install     copy_if_different scratch → deep tree (no rename)
#   7 rewrite          rewrite_host.cmake on deep generated + callbacks
#   8 stamp            .composed marker
#

cmake_minimum_required(VERSION 3.20)

foreach(_v IN ITEMS UFSRV_CONFIG_SOURCE_DIR UFSRV_CONFIG_BINARY_DIR UFSRV_CONFIG_C_COMPILER)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "ufsrv_config_compose_host: ${_v} must be passed via -D")
    endif()
endforeach()

# Sibling host modules (merge script, rewrite, verifier) resolve next to this
# file, so a host invoking its own fork keeps using it and a host invoking the
# canonical copy (Option 1) gets the canonical siblings.  A site may override.
if(NOT DEFINED UFSRV_CONFIG_HOST_MODULES_DIR)
    set(UFSRV_CONFIG_HOST_MODULES_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

# ── Site configuration ─────────────────────────────────────────────────────────
# A site file declares the library list, each library's source mode, the
# canonical source for invariant/shim files, and the host's deep-tree
# destinations.  With no site file the defaults below reproduce the original
# single-site behaviour exactly.
if(DEFINED UFSRV_CONFIG_HOST_SITE_FILE)
    if(NOT EXISTS "${UFSRV_CONFIG_HOST_SITE_FILE}")
        message(FATAL_ERROR "ufsrv_config_compose_host: site file not found: ${UFSRV_CONFIG_HOST_SITE_FILE}")
    endif()
    include("${UFSRV_CONFIG_HOST_SITE_FILE}")
    message(STATUS "ufsrv_config_compose_host: site = ${UFSRV_CONFIG_HOST_SITE_FILE}")
endif()

if(NOT DEFINED UFSRV_CONFIG_HOST_LIBRARIES)
    set(UFSRV_CONFIG_HOST_LIBRARIES "ufsrvcorelib;ufnetcorelib")
endif()
set(_LIBRARY_NAMES "${UFSRV_CONFIG_HOST_LIBRARIES}")
if(_LIBRARY_NAMES STREQUAL "")
    message(FATAL_ERROR "ufsrv_config_compose_host: empty library list")
endif()

set(_STAGING        "${UFSRV_CONFIG_SOURCE_DIR}/dev/scripts/ufsrv_config_generator")
set(_UFSRV_CLONE_PREFIX "${UFSRV_CONFIG_BINARY_DIR}/ufsrv_config_staging")
set(_SCRATCH        "${UFSRV_CONFIG_BINARY_DIR}/ufsrv_config_composed")

# Canonical source for the invariant sources + shim.  Defaults to the host's
# in-tree ufsrvcorelib copy; a site that single-sources its libraries points
# this at the same checkout the composer reads (see G16).
if(NOT DEFINED UFSRV_CONFIG_HOST_CANONICAL_SUB)
    set(UFSRV_CONFIG_HOST_CANONICAL_SUB "${UFSRV_CONFIG_SOURCE_DIR}/packages/ufsrvcorelib")
endif()
set(_SUB "${UFSRV_CONFIG_HOST_CANONICAL_SUB}")

# Deep-tree destinations (host layout)
if(NOT DEFINED UFSRV_CONFIG_HOST_PRIVATE_ROOT)
    set(UFSRV_CONFIG_HOST_PRIVATE_ROOT "${UFSRV_CONFIG_SOURCE_DIR}/src/ufsrv_core/ufsrv_config")
endif()
if(NOT DEFINED UFSRV_CONFIG_HOST_PUBLIC_ROOT)
    set(UFSRV_CONFIG_HOST_PUBLIC_ROOT  "${UFSRV_CONFIG_SOURCE_DIR}/src/include/ufsrvcorelib/ufsrv_core/ufsrv_config")
endif()
set(_DPR "${UFSRV_CONFIG_HOST_PRIVATE_ROOT}")
set(_DPU "${UFSRV_CONFIG_HOST_PUBLIC_ROOT}")
set(_GD  "${_DPR}/generated")

# Per-library source defaults.  A site that omits a library's source spec keeps
# the historical behaviour: shallow-clone it from git.ro.
foreach(_lib_name IN LISTS _LIBRARY_NAMES)
    if(NOT DEFINED UFSRV_CONFIG_HOST_SOURCE_${_lib_name})
        set(UFSRV_CONFIG_HOST_SOURCE_${_lib_name} "git:git.ro:${_lib_name}")
    endif()
endforeach()

# ── Idempotency gate ───────────────────────────────────────────────────────────
# No-op when the .composed stamp exists.  Force a recomposition by removing the
# stamp (rm src/ufsrv_core/ufsrv_config/.composed); there is no --force flag.
if(EXISTS "${_DPR}/.composed")
    message(STATUS "ufsrv_config_compose_host: up-to-date (.composed present); remove it to force re-generation")
    return()
endif()

# Lua headers — required by the catalogue (.c.input) via <lua.h>
find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED lua)
# pkg_check_modules exports the plural LUA_INCLUDE_DIRS; build the -I flags in a
# list so an unset/empty value can never degrade to a bare `-I` that would swallow
# the following argument (e.g. the generated catalogue_input.h directory).
set(_LUA_INCLUDE_FLAGS "")
foreach(_lua_inc IN LISTS LUA_INCLUDE_DIRS)
    list(APPEND _LUA_INCLUDE_FLAGS "-I${_lua_inc}")
endforeach()

pkg_check_modules(UFLIB REQUIRED uflib)
set(_UFLIB_INCLUDE_FLAGS "")
foreach(_uflib_inc IN LISTS UFLIB_INCLUDE_DIRS)
    list(APPEND _UFLIB_INCLUDE_FLAGS "-I${_uflib_inc}")
endforeach()

# ── 1. Resolve each library's source root ─────────────────────────────────────
#   local:<path>        → use that checkout directly; no clone, no network
#   git:<repo>[#<ref>]  → shallow clone (FetchContent is configure-time-only and
#                         cannot run under `cmake -P`), refreshed when the
#                         remote tip has advanced.
# Every downstream consumer reads the resolved root, so a library is never
# composed from two different checkouts (G16).
file(MAKE_DIRECTORY "${_UFSRV_CLONE_PREFIX}")
foreach(_lib_name IN LISTS _LIBRARY_NAMES)
    set(_spec "${UFSRV_CONFIG_HOST_SOURCE_${_lib_name}}")

    if(_spec MATCHES "^local:(.+)$")
        set(_lib_root "${CMAKE_MATCH_1}")
        if(NOT IS_DIRECTORY "${_lib_root}")
            message(FATAL_ERROR "ufsrv_config_compose_host: ${_lib_name} local source not found: ${_lib_root}")
        endif()
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_REPO "LOCAL")
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_REF  "")
        message(STATUS "ufsrv_config_compose_host: ${_lib_name} ← local ${_lib_root}")

    elseif(_spec MATCHES "^git:(.+)$")
        set(_git_spec "${CMAKE_MATCH_1}")
        set(_clone_ref "master")
        if(_git_spec MATCHES "^(.+)#(.+)$")
            set(_git_spec "${CMAKE_MATCH_1}")
            set(_clone_ref "${CMAKE_MATCH_2}")
        endif()
        set(_clone_repo "${_git_spec}")
        set(_clone_dir  "${_UFSRV_CLONE_PREFIX}/${_lib_name}_clone")
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_REPO "${_clone_repo}")
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_REF  "${_clone_ref}")

        if(NOT EXISTS "${_clone_dir}/.git")
            execute_process(
                COMMAND git clone --depth=1 "--branch=${_clone_ref}" "${_clone_repo}" "${_clone_dir}"
                RESULT_VARIABLE _res
                TIMEOUT 300)
            if(NOT _res EQUAL 0)
                message(FATAL_ERROR "ufsrv_config_compose_host: clone failed (${_lib_name})")
            endif()
        else()
            # An existing clone may be stale (a prior compose cloned an older tip).
            # Compare the remote tip against the clone HEAD and refresh only when the
            # remote has advanced — this is what keeps regeneration off a fresh clone.
            execute_process(
                COMMAND git ls-remote "${_clone_repo}" "refs/heads/${_clone_ref}"
                OUTPUT_VARIABLE _remote_ref
                RESULT_VARIABLE _ls_res
                TIMEOUT 60)
            execute_process(
                COMMAND git -C "${_clone_dir}" rev-parse HEAD
                OUTPUT_VARIABLE _local_head
                RESULT_VARIABLE _rev_res
                TIMEOUT 30)
            string(SUBSTRING "${_remote_ref}" 0 40 _remote_sha)
            string(STRIP "${_remote_sha}" _remote_sha)
            string(STRIP "${_local_head}" _local_head)

            if(_ls_res EQUAL 0 AND _rev_res EQUAL 0 AND NOT _remote_sha STREQUAL _local_head)
                message(STATUS "ufsrv_config_compose_host: ${_lib_name} remote ahead of clone — refreshing")
                execute_process(
                    COMMAND git -C "${_clone_dir}" fetch --depth=1 origin "${_clone_ref}"
                    RESULT_VARIABLE _fetch_res
                    TIMEOUT 300)
                execute_process(
                    COMMAND git -C "${_clone_dir}" reset --hard FETCH_HEAD
                    RESULT_VARIABLE _reset_res
                    TIMEOUT 60)
                if(NOT _fetch_res EQUAL 0 OR NOT _reset_res EQUAL 0)
                    message(FATAL_ERROR "ufsrv_config_compose_host: clone refresh failed (${_lib_name})")
                endif()
            elseif(NOT _ls_res EQUAL 0 OR NOT _rev_res EQUAL 0)
                message(WARNING "ufsrv_config_compose_host: cannot verify ${_lib_name} clone freshness — using existing clone as-is")
            endif()
        endif()
        set(_lib_root "${_clone_dir}")

    else()
        message(FATAL_ERROR
            "ufsrv_config_compose_host: ${_lib_name} has no valid source spec ('${_spec}');\n"
            "  expected local:<path> or git:<repo>[#<ref>]")
    endif()

    set(UFSRV_CONFIG_HOST_ROOT_${_lib_name} "${_lib_root}")
endforeach()

# The toolchain library owns the generator, verifier, join script and composer.
# It is the first entry in the library list, by convention, and its resolved
# root is the single checkout every toolchain read uses.
list(GET _LIBRARY_NAMES 0 _TOOLCHAIN_LIB)
set(_TOOLCHAIN_ROOT "${UFSRV_CONFIG_HOST_ROOT_${_TOOLCHAIN_LIB}}")
set(_core_clone "${_TOOLCHAIN_ROOT}")

# ── 2. Reuse shared composer (set contract vars BEFORE include) ───────────────
set(UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES "${_LIBRARY_NAMES}")
set(UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO  "LOCAL")
set(UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF   "")
set(UFSRV_CONFIG_COMPOSER_STAGING_DIR     "${_STAGING}")
set(UFSRV_CONFIG_COMPOSER_OUTPUT_DIR      "${_SCRATCH}")
set(UFSRV_CONFIG_COMPOSER_SOURCE_DIR      "${_core_clone}")
# The composer's default join-script path uses CMAKE_CURRENT_LIST_DIR, which inside
# a called function resolves to THIS orchestrator's dir (caller scope) — pin it.
set(UFSRV_CONFIG_COMPOSER_JOIN_SCRIPT     "${_core_clone}/cmake-modules/compose_ufsrv_config_join.py")
set(UFSRV_CONFIG_COMPOSER_LITERALS_HEADER "${_core_clone}/include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h")
foreach(_lib_name IN LISTS _LIBRARY_NAMES)
    # REPO/REF already resolved per library in step 1 (LOCAL for local: sources,
    # the declared repo for git: sources) — do not overwrite them here.
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE)
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE
            "dev/scripts/ufsrv_config_generator/ufsrv_config_generator_fields.c.input")
    endif()
    # Include prefix defaults to the library's own name; a site may override.
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_${_lib_name}_PREFIX)
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_PREFIX "")
    endif()
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_${_lib_name}_INCLUDE_PREFIX)
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_INCLUDE_PREFIX "${_lib_name}")
    endif()
endforeach()

include("${_core_clone}/cmake-modules/compose_ufsrv_config.cmake")

# ── 3. Checkout toolchain assets + catalogues into staging (reuse) ────────────
file(MAKE_DIRECTORY "${_STAGING}")
compose_ufsrv_config_bootstrap()
foreach(_lib_name IN LISTS _LIBRARY_NAMES)
    set(_lib_clone "${UFSRV_CONFIG_HOST_ROOT_${_lib_name}}")
    set(UFSRV_CONFIG_COMPOSER_SOURCE_DIR "${_lib_clone}")
    set(_lib_staging "${_STAGING}/${_lib_name}")
    file(MAKE_DIRECTORY "${_lib_staging}")
    _composer_checkout_file("LOCAL" "" "${UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE}"
        "${_lib_staging}" "${_lib_name}/catalogue")
endforeach()

# ── 4-5. Compile + run generator per library ─────────────────────────────────
set(_GEN_SRC "${_STAGING}/ufsrv_config_generator.c")
foreach(_lib_name IN LISTS _LIBRARY_NAMES)
    set(_lib_clone   "${UFSRV_CONFIG_HOST_ROOT_${_lib_name}}")
    set(_lib_gen     "${_STAGING}/${_lib_name}/generated")
    set(_gen_bin_dir "${UFSRV_CONFIG_BINARY_DIR}/host/${_lib_name}")
    set(_gen_bin     "${UFSRV_CONFIG_BINARY_DIR}/host/ufsrv_config_generator_${_lib_name}")
    file(MAKE_DIRECTORY "${_gen_bin_dir}")

    # catalogue indirection header (embedded at compile time)
    get_filename_component(_cat_fname "${UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE}" NAME)
    file(WRITE "${_gen_bin_dir}/catalogue_input.h"
        "/* Generated by cmake - do not edit. */\n#include \"${_STAGING}/${_lib_name}/${_cat_fname}\"\n")

    # compile (direct call — replicates the old add_executable flags)
    execute_process(
        COMMAND ${UFSRV_CONFIG_C_COMPILER} -fblocks
            "-I${_STAGING}"
            "-I${_core_clone}/include/ufsrvcorelib"
            "-I${_core_clone}/include"
            "-I${_core_clone}/include/ufsrvcorelib/ufsrv_core/ufsrv_config"
            "-I${_core_clone}/include/ufsrvcorelib/ufsrv_core/configfile_processor"
            ${_LUA_INCLUDE_FLAGS}
            ${_UFLIB_INCLUDE_FLAGS}
            "-I${_gen_bin_dir}"
            "-I${_lib_clone}/include"
            "-I${_lib_clone}/include/${_lib_name}"
            "-I${_lib_clone}/src"
            "-I${_lib_clone}/packages"
            "${_GEN_SRC}" -o "${_gen_bin}" -lBlocksRuntime
        RESULT_VARIABLE _res)
    if(NOT _res EQUAL 0)
        message(FATAL_ERROR "ufsrv_config_compose_host: generator compile failed (${_lib_name})")
    endif()

    # run generator (header pass + source pass)
    set(_gen_args "--output-dir=${_lib_gen}"
                  "--repo-dir=${_core_clone}"
                  "--literals-header=${_core_clone}/include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h")
    if(NOT "${UFSRV_CONFIG_COMPOSER_${_lib_name}_INCLUDE_PREFIX}" STREQUAL "")
        list(APPEND _gen_args "--include-prefix=${UFSRV_CONFIG_COMPOSER_${_lib_name}_INCLUDE_PREFIX}")
    endif()
    if(NOT "${UFSRV_CONFIG_COMPOSER_${_lib_name}_PREFIX}" STREQUAL "")
        list(APPEND _gen_args "--prefix=${UFSRV_CONFIG_COMPOSER_${_lib_name}_PREFIX}")
    endif()
    file(MAKE_DIRECTORY "${_lib_gen}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E rm -f
        "${_lib_gen}/ufsrv_config_*" "${_lib_gen}/data_ufsrv_config_*")
    execute_process(COMMAND "${_gen_bin}" ${_gen_args} --header RESULT_VARIABLE _res)
    if(NOT _res EQUAL 0)
        message(FATAL_ERROR "ufsrv_config_compose_host: generator --header failed (${_lib_name})")
    endif()
    execute_process(COMMAND "${_gen_bin}" ${_gen_args} --source RESULT_VARIABLE _res)
    if(NOT _res EQUAL 0)
        message(FATAL_ERROR "ufsrv_config_compose_host: generator --source failed (${_lib_name})")
    endif()
endforeach()

# ── 6. Compose to scratch (reuse) ─────────────────────────────────────────────
file(MAKE_DIRECTORY "${_SCRATCH}")
compose_ufsrv_config()

# ── 7. Deep-install: copy_if_different scratch → deep (no rename) ─────────────
set(_INV ufsrv_config.c ufsrv_config_generic_callbacks.c
         ufsrv_config_generic_callbacks_priv.h)
set(_PUB_HDR ufsrv_config_field_enums_generated.h
             ufsrv_config_accessor_decls_generated.h)
set(_PRIV_GEN data_ufsrv_config_field_tables_generated.c
              data_ufsrv_config_dispatch_table_priv_generated.h
              data_ufsrv_config_literal_to_field_map_priv_generated.h
              ufsrv_config_accessor_defs_generated.c
              ufsrv_config_getters_setters_generated.c
              ufsrv_config_validate_generated.c)

file(MAKE_DIRECTORY "${_GD}")
file(MAKE_DIRECTORY "${_DPU}/generated")

# drop any transitional flat-state ufsrv_config.h
if(EXISTS "${_DPR}/ufsrv_config.h")
    file(REMOVE "${_DPR}/ufsrv_config.h")
endif()

# invariants — verbatim from the canonical checkout (undo composer flat rewrite)
foreach(_f IN LISTS _INV)
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_SUB}/src/ufsrv_core/ufsrv_config/${_f}" "${_DPR}/${_f}"
        RESULT_VARIABLE _inv_copy_res)
    if(NOT _inv_copy_res EQUAL 0)
        message(FATAL_ERROR "ufsrv_config_compose_host: invariant copy failed (${_f}) from ${_SUB}")
    endif()
endforeach()

# stubs_as_string_priv_generated.h — merged from BOTH libraries (each ships its
# own non-standard AsString marshaller declarations).  Emitted under a
# "_generated" name so it never overwrites a hand-maintained
# ufsrv_config_stubs_as_string_priv.h; the consumer reconciles the two.
set(_STUB_AS_STRING_MERGE_SOURCES "")
foreach(_lib_name IN LISTS _LIBRARY_NAMES)
    set(_stub_hdr "${UFSRV_CONFIG_HOST_ROOT_${_lib_name}}/src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_as_string_priv.h")
    if(EXISTS "${_stub_hdr}")
        list(APPEND _STUB_AS_STRING_MERGE_SOURCES "${_stub_hdr}")
    else()
        message(WARNING "ufsrv_config_compose_host: ${_lib_name} has no ufsrv_config_stubs_as_string_priv.h (skipping)")
    endif()
endforeach()

if(_STUB_AS_STRING_MERGE_SOURCES)
    execute_process(
        COMMAND python3
            "${UFSRV_CONFIG_HOST_MODULES_DIR}/ufsrv_config_merge_as_string_stubs_host.py"
            --output "${_DPR}/ufsrv_config_stubs_as_string_priv_generated.h"
            ${_STUB_AS_STRING_MERGE_SOURCES}
        RESULT_VARIABLE _merge_res
        OUTPUT_VARIABLE _merge_out
        ERROR_VARIABLE _merge_err)
    message(STATUS "${_merge_out}")
    if(NOT _merge_res EQUAL 0)
        message(FATAL_ERROR "ufsrv_config_compose_host: stubs_as_string_priv.h merge failed\n${_merge_err}")
    endif()
else()
    message(WARNING "ufsrv_config_compose_host: no ufsrv_config_stubs_as_string_priv.h found in any library")
endif()

# shim — from the canonical checkout
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${_SUB}/include/ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_shim.h"
    "${_DPU}/ufsrv_config_shim.h"
    RESULT_VARIABLE _shim_copy_res)
if(NOT _shim_copy_res EQUAL 0)
    message(FATAL_ERROR "ufsrv_config_compose_host: shim copy failed from ${_SUB}")
endif()

# private generated — scratch top-level → deep private generated/
foreach(_f IN LISTS _PRIV_GEN)
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_SCRATCH}/${_f}" "${_GD}/${_f}")
endforeach()

# public headers — scratch generated/ → deep public generated/
foreach(_f IN LISTS _PUB_HDR)
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_SCRATCH}/generated/${_f}" "${_DPU}/generated/${_f}")
endforeach()

# ── 8. Rewrite includes → LOCAL deep tree ─────────────────────────────────────
set(_REW "${UFSRV_CONFIG_HOST_MODULES_DIR}/ufsrv_config_rewrite_host.cmake")
foreach(_f IN LISTS _PRIV_GEN)
    execute_process(COMMAND ${CMAKE_COMMAND} "-DFILE=${_GD}/${_f}" -P "${_REW}")
endforeach()
execute_process(COMMAND ${CMAKE_COMMAND}
    "-DFILE=${_DPR}/ufsrv_config_generic_callbacks.c" -P "${_REW}")

# ── 9. Verify the joined composed output against the catalogues + conventions ──
set(_verify_args
    --generated-private "${_GD}"
    --generated-public  "${_DPU}/generated"
    --literals-header   "${_core_clone}/include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h")
foreach(_lib_name IN LISTS _LIBRARY_NAMES)
    get_filename_component(_vf_cat "${UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE}" NAME)
    list(APPEND _verify_args --catalogue "${_lib_name}=${_STAGING}/${_lib_name}/${_vf_cat}")
endforeach()
# Optional: a host-only symbol inventory the verifier cross-checks against.
if(DEFINED UFSRV_CONFIG_HOST_SYMBOLS_HEADER AND NOT UFSRV_CONFIG_HOST_SYMBOLS_HEADER STREQUAL "")
    list(APPEND _verify_args --symbols-header "${UFSRV_CONFIG_HOST_SYMBOLS_HEADER}")
endif()

execute_process(
    COMMAND python3 "${UFSRV_CONFIG_HOST_MODULES_DIR}/ufsrv_config_verify_host.py"
        ${_verify_args}
    RESULT_VARIABLE _verify_res
    OUTPUT_VARIABLE _verify_out
    ERROR_VARIABLE _verify_err)
message(STATUS "${_verify_out}")
if(NOT _verify_res EQUAL 0)
    message(FATAL_ERROR "ufsrv_config_compose_host: composition verification FAILED\n${_verify_err}")
endif()

# ── 10. Stamp ─────────────────────────────────────────────────────────────────
file(TOUCH "${_DPR}/.composed")

message(STATUS "ufsrv_config_compose_host: done — deep tree populated")
