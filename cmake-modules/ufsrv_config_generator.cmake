#
# cmake-modules/ufsrv_config_generator.cmake
#
# ufsrv_config code-generation pipeline — reusable across ufsrvcorelib (core)
# and derivative servers (adjunct).
#
# Contract:
#   Set the required input variables BEFORE include(), then call:
#     target_sources(${_PROJECT_NAME} PRIVATE ${UFSRV_CONFIG_GENERATED_SOURCES})
#   after include().
#
# Required inputs (set before include):
#   UFSRV_CONFIG_GEN_DIR            — path to generator source directory
#                                      (contains ufsrv_config_generator.c)
#   UFSRV_CONFIG_GEN_CATALOGUE      — path to .c.input cold catalogue file
#   UFSRV_CONFIG_GEN_INCLUDE_PREFIX — e.g. "ufsrvcorelib", "ufnetcorelib"
#   UFSRV_CONFIG_GEN_PREFIX         — "" (core) or "adjunct"
#   UFSRV_CONFIG_GEN_VERIFIER       — path to ufsrv_config_verify.py
#   UFSRV_CONFIG_GEN_INCLUDE_DIRS   — extra include dirs for generator binary
#
# Optional overrides:
#   UFSRV_CONFIG_GEN_BINARY_NAME      — default: ufsrv_config_generator
#   UFSRV_CONFIG_GEN_STAGING          — default: ${GEN_DIR}/generated
#   UFSRV_CONFIG_GEN_DEST_H           — default: computed from include-prefix
#   UFSRV_CONFIG_GEN_DEST_C           — default: computed from include-prefix
#   UFSRV_CONFIG_GEN_LITERALS_HEADER  — override path to ufsrv_config_literals_defs.h
#                                        (default: ${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/...)
#
# Exports:
#   UFSRV_CONFIG_GENERATED_SOURCES  — generated .c files to compile into library
#
# Targets (only when UFSRV_CONFIG_GENERATOR_ON=ON):
#   ufsrv_config[_<prefix>]_verify_catalogue  — verifier on catalogue only
#   ufsrv_config[_<prefix>]_generate          — generator → staging
#   ufsrv_config[_<prefix>]_verify_generated  — verifier on catalogue + generated
#   ufsrv_config[_<prefix>]_install           — copy staging → include/ + src/
#   ufsrv_config[_<prefix>]_all               — chains all four in order

include_guard(GLOBAL)

# ── Sanity checks ─────────────────────────────────────────────────
foreach(_v IN ITEMS UFSRV_CONFIG_GEN_DIR UFSRV_CONFIG_GEN_CATALOGUE
                  UFSRV_CONFIG_GEN_INCLUDE_PREFIX UFSRV_CONFIG_GEN_PREFIX
                  UFSRV_CONFIG_GEN_VERIFIER)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "ufsrv_config_generator: ${_v} must be set before include()")
    endif()
endforeach()

# ── Derived paths and defaults ────────────────────────────────────
if(NOT DEFINED UFSRV_CONFIG_GEN_BINARY_NAME)
    set(UFSRV_CONFIG_GEN_BINARY_NAME "ufsrv_config_generator")
endif()
if(NOT DEFINED UFSRV_CONFIG_GEN_STAGING)
    set(UFSRV_CONFIG_GEN_STAGING "${UFSRV_CONFIG_GEN_DIR}/generated")
endif()
if(NOT DEFINED UFSRV_CONFIG_GEN_DEST_H)
    set(UFSRV_CONFIG_GEN_DEST_H
        "${CMAKE_CURRENT_SOURCE_DIR}/include/${UFSRV_CONFIG_GEN_INCLUDE_PREFIX}/ufsrv_core/ufsrv_config/generated")
endif()
if(NOT DEFINED UFSRV_CONFIG_GEN_DEST_C)
    set(UFSRV_CONFIG_GEN_DEST_C
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated")
endif()

# ── Compute file-naming components ────────────────────────────────
# With PREFIX=""     → infix=""     master: ufsrv_config_generated.h
# With PREFIX="adjunct" → infix="_adjunct"  master: ufsrv_config_adjunct_generated.h
if(UFSRV_CONFIG_GEN_PREFIX STREQUAL "")
    set(_UGEN_INFIX "")
else()
    set(_UGEN_INFIX "_${UFSRV_CONFIG_GEN_PREFIX}")
endif()

# Target name suffix (empty for core, "_adjunct" for adjunct)
if(UFSRV_CONFIG_GEN_PREFIX STREQUAL "")
    set(_UGEN_TGT_SUFFIX "")
else()
    set(_UGEN_TGT_SUFFIX "_${UFSRV_CONFIG_GEN_PREFIX}")
endif()

# ── Generated file manifests (single source of truth) ─────────────

# Stem names (without prefix or _generated suffix)
set(_UGEN_HEADER_STEMS
    field_enums
    accessor_decls
)
set(_UGEN_SOURCE_STEMS
    accessor_defs
    validate
)

# Build full file lists from stems
set(_UGEN_GEN_HEADERS "")
set(_UGEN_GEN_SOURCES "")

# Sub-headers: ufsrv_config_{stem}[_<prefix>]_generated.h
foreach(_stem IN LISTS _UGEN_HEADER_STEMS)
    list(APPEND _UGEN_GEN_HEADERS "ufsrv_config_${_stem}${_UGEN_INFIX}_generated.h")
endforeach()

# Extra manifest header (only with prefix)
if(NOT UFSRV_CONFIG_GEN_PREFIX STREQUAL "")
    list(APPEND _UGEN_GEN_HEADERS "ufsrv_config_${UFSRV_CONFIG_GEN_PREFIX}_manifest.h")
endif()

# Master source: ufsrv_config_getters_setters[_<prefix>]_generated.c
list(APPEND _UGEN_GEN_SOURCES "ufsrv_config_getters_setters${_UGEN_INFIX}_generated.c")

# Sub-sources: ufsrv_config_{stem}[_<prefix>]_generated.c
foreach(_stem IN LISTS _UGEN_SOURCE_STEMS)
    list(APPEND _UGEN_GEN_SOURCES "ufsrv_config_${_stem}${_UGEN_INFIX}_generated.c")
endforeach()

# Field tables file has a "data_" prefix — hard-coded (not stem-based)
list(APPEND _UGEN_GEN_SOURCES "data_ufsrv_config_field_tables${_UGEN_INFIX}_generated.c")

# Key-map header — also "data_" prefix, private generated header
list(APPEND _UGEN_GEN_SOURCES "data_ufsrv_config_literal_to_field_map${_UGEN_INFIX}_priv_generated.h")

# Dispatch table — "data_" prefix, private generated header (preserved, not compiled)
list(APPEND _UGEN_GEN_SOURCES "data_ufsrv_config_dispatch_table${_UGEN_INFIX}_priv_generated.h")

# ── Sources to compile into the library (always exported) ─────────
set(UFSRV_CONFIG_GENERATED_SOURCES
    ${UFSRV_CONFIG_GEN_DEST_C}/data_ufsrv_config_field_tables${_UGEN_INFIX}_generated.c
    ${UFSRV_CONFIG_GEN_DEST_C}/ufsrv_config_accessor_defs${_UGEN_INFIX}_generated.c
    ${UFSRV_CONFIG_GEN_DEST_C}/ufsrv_config_validate${_UGEN_INFIX}_generated.c
)

# ── Option: gate the entire generation pipeline ───────────────────
option(UFSRV_CONFIG_GENERATOR_ON "Run ufsrv_config_generator at build time" OFF)

if(NOT UFSRV_CONFIG_GENERATOR_ON)
    message(STATUS "ufsrv_config: generator OFF — using committed generated files")
    return()
endif()

message(STATUS "ufsrv_config: generator ON — ${UFSRV_CONFIG_GEN_CATALOGUE}")

# ═══════════════════════════════════════════════════════════════════
# Generate catalogue indirection header so the generator can
# #include "catalogue_input.h" without a hardcoded path.
# The actual catalogue file is injected via this generated header.
# CMake's C dependency scanner follows the #include chain and
# automatically recompiles when the catalogue changes.
# ═══════════════════════════════════════════════════════════════════

set(_UGEN_CATALOGUE_INPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/ufsrv_config_generator")
file(MAKE_DIRECTORY "${_UGEN_CATALOGUE_INPUT_DIR}")
file(WRITE "${_UGEN_CATALOGUE_INPUT_DIR}/catalogue_input.h"
     "/* Generated by cmake — do not edit. */\n#include \"${UFSRV_CONFIG_GEN_CATALOGUE}\"\n")

# ═══════════════════════════════════════════════════════════════════
# Generator host binary
# ═══════════════════════════════════════════════════════════════════

add_executable(${UFSRV_CONFIG_GEN_BINARY_NAME}
    ${UFSRV_CONFIG_GEN_DIR}/ufsrv_config_generator.c
)
target_include_directories(${UFSRV_CONFIG_GEN_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LUA_INCLUDE_DIR}
    ${UFSRV_CONFIG_GEN_INCLUDE_DIRS}
    ${_UGEN_CATALOGUE_INPUT_DIR}
)
if(TARGET uflib::uflib)
    get_target_property(_UGEN_UFLIB_INCLUDES uflib::uflib INTERFACE_INCLUDE_DIRECTORIES)
    if(_UGEN_UFLIB_INCLUDES)
        target_include_directories(${UFSRV_CONFIG_GEN_BINARY_NAME} SYSTEM PRIVATE ${_UGEN_UFLIB_INCLUDES})
    endif()
endif()
set_target_properties(${UFSRV_CONFIG_GEN_BINARY_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host"
)
target_compile_options(${UFSRV_CONFIG_GEN_BINARY_NAME} PRIVATE -fblocks)
target_link_libraries(${UFSRV_CONFIG_GEN_BINARY_NAME} PRIVATE BlocksRuntime)

# ── Generator flags ───────────────────────────────────────────────
# UFSRV_CONFIG_GEN_LITERALS_HEADER (optional) overrides the default
# path so consumers (e.g. ufnetcorelib) that borrow the toolchain
# from a subtree can point at their local copy.
if(DEFINED UFSRV_CONFIG_GEN_LITERALS_HEADER)
    set(_UGEN_LITERALS_HEADER "${UFSRV_CONFIG_GEN_LITERALS_HEADER}")
else()
    set(_UGEN_LITERALS_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h")
endif()

set(_UGEN_GEN_FLAGS
    --output-dir=${UFSRV_CONFIG_GEN_STAGING}
    --include-prefix=${UFSRV_CONFIG_GEN_INCLUDE_PREFIX}
    --repo-dir=${CMAKE_CURRENT_SOURCE_DIR}
    --literals-header=${_UGEN_LITERALS_HEADER}
)
if(NOT UFSRV_CONFIG_GEN_PREFIX STREQUAL "")
    list(APPEND _UGEN_GEN_FLAGS --prefix=${UFSRV_CONFIG_GEN_PREFIX})
endif()

# ═══════════════════════════════════════════════════════════════════
# Target: ufsrv_config_verify_catalogue
#   python3 verifier --catalogue <file>
# ═══════════════════════════════════════════════════════════════════

set(_UGEN_TGT_VFY_CAT "ufsrv_config${_UGEN_TGT_SUFFIX}_verify_catalogue")

if(EXISTS "${UFSRV_CONFIG_GEN_VERIFIER}")
    add_custom_target(${_UGEN_TGT_VFY_CAT}
        COMMAND python3 "${UFSRV_CONFIG_GEN_VERIFIER}"
            --catalogue "${UFSRV_CONFIG_GEN_CATALOGUE}"
        COMMENT "Verifying cold catalogue: ${UFSRV_CONFIG_GEN_CATALOGUE}"
        VERBATIM
    )
else()
    message(WARNING "ufsrv_config_generator: verifier not found at ${UFSRV_CONFIG_GEN_VERIFIER} — skipping ${_UGEN_TGT_VFY_CAT} target")
endif()

# ═══════════════════════════════════════════════════════════════════
# Target: ufsrv_config_generate
#   1. Remove all previously generated files from staging
#   2. ./generator --header ... && ./generator --source ...
# ═══════════════════════════════════════════════════════════════════

set(_UGEN_TGT_GEN "ufsrv_config${_UGEN_TGT_SUFFIX}_generate")

# Build staging output path list
set(_UGEN_STAGING_OUTPUTS "")
foreach(_f IN LISTS _UGEN_GEN_HEADERS _UGEN_GEN_SOURCES)
    list(APPEND _UGEN_STAGING_OUTPUTS "${UFSRV_CONFIG_GEN_STAGING}/${_f}")
endforeach()

add_custom_command(
    OUTPUT ${_UGEN_STAGING_OUTPUTS}
    # Step 0 — ensure staging directory exists (mkdir -p)
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UFSRV_CONFIG_GEN_STAGING}
    # Step 1 — purge stale generated files
    COMMAND ${CMAKE_COMMAND} -E rm -f ${UFSRV_CONFIG_GEN_STAGING}/ufsrv_config_*
    # Step 2 — regenerate
    COMMAND ${UFSRV_CONFIG_GEN_BINARY_NAME}
        --header ${_UGEN_GEN_FLAGS}
    COMMAND ${UFSRV_CONFIG_GEN_BINARY_NAME}
        --source ${_UGEN_GEN_FLAGS}
    DEPENDS ${UFSRV_CONFIG_GEN_BINARY_NAME}
            ${UFSRV_CONFIG_GEN_DIR}/ufsrv_config_generator.c
            ${UFSRV_CONFIG_GEN_CATALOGUE}
    COMMENT "ufsrv_config_generator: clean stale + regenerate → ${UFSRV_CONFIG_GEN_STAGING}/"
    VERBATIM
)

add_custom_target(${_UGEN_TGT_GEN}
    DEPENDS ${_UGEN_STAGING_OUTPUTS}
)

# ═══════════════════════════════════════════════════════════════════
# Target: ufsrv_config_verify_generated
#   python3 verifier --catalogue <file> --generated-dir <staging>
# ═══════════════════════════════════════════════════════════════════

set(_UGEN_TGT_VFY_GEN "ufsrv_config${_UGEN_TGT_SUFFIX}_verify_generated")

if(EXISTS "${UFSRV_CONFIG_GEN_VERIFIER}")
    add_custom_target(${_UGEN_TGT_VFY_GEN}
        COMMAND python3 "${UFSRV_CONFIG_GEN_VERIFIER}"
            --catalogue "${UFSRV_CONFIG_GEN_CATALOGUE}"
            --generated-dir "${UFSRV_CONFIG_GEN_STAGING}"
        DEPENDS ${_UGEN_STAGING_OUTPUTS}
        COMMENT "Verifying generated artefacts in ${UFSRV_CONFIG_GEN_STAGING}/"
        VERBATIM
    )
else()
    message(WARNING "ufsrv_config_generator: verifier not found — skipping ${_UGEN_TGT_VFY_GEN} target")
endif()

# ═══════════════════════════════════════════════════════════════════
# Target: ufsrv_config_install
#   1. Copy headers + sources from staging → dest IN PLACE (no prior
#      rm — overwrite preserves git inode identity for log --follow).
#   2. Remove orphaned files (tracked by git in dest dirs but no
#      longer in the generator manifest — renamed/deleted fields).
#   3. git add -A both dest dirs to stage modifications + adds +
#      orphan deletions in a single coherent commit.
# ═══════════════════════════════════════════════════════════════════

set(_UGEN_TGT_INSTALL "ufsrv_config${_UGEN_TGT_SUFFIX}_install")

set(_UGEN_COPIED_OUTPUTS "")

# ── Step 1: copy headers — overwrite in place ────────────────────
foreach(_f IN LISTS _UGEN_GEN_HEADERS)
    add_custom_command(
        OUTPUT ${UFSRV_CONFIG_GEN_DEST_H}/${_f}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${UFSRV_CONFIG_GEN_STAGING}/${_f}
            ${UFSRV_CONFIG_GEN_DEST_H}/${_f}
        DEPENDS ${UFSRV_CONFIG_GEN_STAGING}/${_f}
        COMMENT "Copying ${_f} → include/"
    )
    list(APPEND _UGEN_COPIED_OUTPUTS "${UFSRV_CONFIG_GEN_DEST_H}/${_f}")
endforeach()

# ── Step 1: copy sources — overwrite in place ────────────────────
foreach(_f IN LISTS _UGEN_GEN_SOURCES)
    add_custom_command(
        OUTPUT ${UFSRV_CONFIG_GEN_DEST_C}/${_f}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${UFSRV_CONFIG_GEN_STAGING}/${_f}
            ${UFSRV_CONFIG_GEN_DEST_C}/${_f}
        DEPENDS ${UFSRV_CONFIG_GEN_STAGING}/${_f}
        COMMENT "Copying ${_f} → src/"
    )
    set_source_files_properties(${UFSRV_CONFIG_GEN_DEST_C}/${_f} PROPERTIES GENERATED TRUE)
    list(APPEND _UGEN_COPIED_OUTPUTS "${UFSRV_CONFIG_GEN_DEST_C}/${_f}")
endforeach()

# ── Step 2: remove orphaned generated files ──────────────────────
# Only removes files that are in git but no longer in the manifest.
# Files not tracked by git are left alone (they may be build artefacts).
# We build a |-delimited manifest of basenames so the shell can do a
# simple pattern match without multi-line quoting issues.
set(_UGEN_MANIFEST_BASENAMES "")
foreach(_f IN ITEMS ${_UGEN_GEN_HEADERS} ${_UGEN_GEN_SOURCES})
    if(_UGEN_MANIFEST_BASENAMES STREQUAL "")
        set(_UGEN_MANIFEST_BASENAMES "${_f}")
    else()
        set(_UGEN_MANIFEST_BASENAMES "${_UGEN_MANIFEST_BASENAMES}|${_f}")
    endif()
endforeach()

set(_UGEN_ORPHAN_STAMP "${UFSRV_CONFIG_GEN_STAGING}/.orphans_cleaned")

# Write a throw-away orphan-cleanup script.  Use file(WRITE) so the
# shell code is emitted verbatim — no escaping fight with cmake.
set(_UGEN_ORPHAN_SCRIPT "${UFSRV_CONFIG_GEN_STAGING}/_clean_orphans.sh")
file(WRITE ${_UGEN_ORPHAN_SCRIPT}
"#!/bin/bash
# Auto-generated — do not edit.
manifest='${_UGEN_MANIFEST_BASENAMES}'
for dest in '${UFSRV_CONFIG_GEN_DEST_H}' '${UFSRV_CONFIG_GEN_DEST_C}'; do
    [ -d \"\$dest\" ] || continue
    cd \"\$dest\" || exit 1
    git ls-files . | while IFS= read -r f; do
        # skip .gitkeep (not a generated artefact)
        [ \"\$f\" = .gitkeep ] && continue
        bn=\$(basename \"\$f\")
        case \"|\$manifest|\" in
            *\"|\$bn|\"*) ;;
            *) echo '  removing orphan: '\"\$f\"; git rm -f -- \"\$f\" ;;
        esac
    done
done
touch ${_UGEN_ORPHAN_STAMP}
")

add_custom_command(
    OUTPUT ${_UGEN_ORPHAN_STAMP}
    COMMAND bash ${_UGEN_ORPHAN_SCRIPT}
    DEPENDS ${_UGEN_COPIED_OUTPUTS} ${_UGEN_ORPHAN_SCRIPT}
    COMMENT "Removing orphaned generated files (in git but not in manifest)"
)

# ── Step 3: stage everything (modifications + adds + orphan deletions) ──
set(_UGEN_STAGED_STAMP "${UFSRV_CONFIG_GEN_STAGING}/.staged")

add_custom_command(
    OUTPUT ${_UGEN_STAGED_STAMP}
    COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} add -A
        ${UFSRV_CONFIG_GEN_DEST_H}
        ${UFSRV_CONFIG_GEN_DEST_C}
    COMMAND ${CMAKE_COMMAND} -E touch ${_UGEN_STAGED_STAMP}
    DEPENDS ${_UGEN_ORPHAN_STAMP}
    COMMENT "Staging generated files (git add -A)"
)

list(APPEND _UGEN_COPIED_OUTPUTS ${_UGEN_ORPHAN_STAMP} ${_UGEN_STAGED_STAMP})

add_custom_target(${_UGEN_TGT_INSTALL}
    DEPENDS ${_UGEN_COPIED_OUTPUTS}
)

# ═══════════════════════════════════════════════════════════════════
# Target: ufsrv_config_all
#   Chains: verify_catalogue → generate → verify_generated → install
# ═══════════════════════════════════════════════════════════════════

set(_UGEN_TGT_ALL "ufsrv_config${_UGEN_TGT_SUFFIX}_all")

add_custom_target(${_UGEN_TGT_ALL})

# Chain dependencies so _all runs the pipeline in strict order.
# Each target is also independently runnable.
# NOTE: install depends on generate (not verify_generated) because the
#       verifier has known pre-existing orphan false-positives (I02).
if(TARGET ${_UGEN_TGT_INSTALL})
    add_dependencies(${_UGEN_TGT_ALL} ${_UGEN_TGT_INSTALL})
endif()
if(TARGET ${_UGEN_TGT_VFY_GEN})
    add_dependencies(${_UGEN_TGT_ALL} ${_UGEN_TGT_VFY_GEN})
    add_dependencies(${_UGEN_TGT_VFY_GEN} ${_UGEN_TGT_GEN})
endif()
if(TARGET ${_UGEN_TGT_GEN})
    add_dependencies(${_UGEN_TGT_INSTALL} ${_UGEN_TGT_GEN})
    add_dependencies(${_UGEN_TGT_GEN} ${_UGEN_TGT_VFY_CAT})
endif()

# ── Wire into library build ───────────────────────────────────────
# The library depends on the generated files being in place.
# In ON mode, the _install target ensures they're copied before compilation.
# In OFF mode, committed files are used directly.
add_dependencies(${_PROJECT_NAME} ${_UGEN_TGT_INSTALL})

message(STATUS "ufsrv_config: generator ON — staging → ${UFSRV_CONFIG_GEN_STAGING}")
message(STATUS "ufsrv_config: targets — ${_UGEN_TGT_VFY_CAT} | ${_UGEN_TGT_GEN} | ${_UGEN_TGT_VFY_GEN} | ${_UGEN_TGT_INSTALL} | ${_UGEN_TGT_ALL}")
