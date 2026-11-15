#
# cmake-modules/ufsrv_config_composition.cmake
#
# Host-server composition pipeline for the ufsrv_config module.
# Include this from the host's top-level CMakeLists.txt AFTER the server
# target is defined.
#
# Required (set before include):
#   UFSRV_CONFIG_SERVER_TARGET — name of the server executable/library target
#
# Optional overrides (set before include):
#   UFSRV_CONFIG_TOOLCHAIN_REPO, _REF
#   UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES
#   Per-library UFSRV_CONFIG_COMPOSER_<name>_REPO, _REF, _CATALOGUE
#

include_guard(GLOBAL)

# ═══════════════════════════════════════════════════════════════════════════════
# 1. Options and defaults
# ═══════════════════════════════════════════════════════════════════════════════

set(UFSRV_CONFIG_TOOLCHAIN_REPO "git.ro:ufsrvcorelib"
    CACHE STRING "Git repo for ufsrvcorelib (toolchain + catalogue source)")
set(UFSRV_CONFIG_TOOLCHAIN_REF "master"
    CACHE STRING "Git ref for toolchain checkout")
set(UFSRV_CONFIG_STAGING "${CMAKE_SOURCE_DIR}/dev/scripts/ufsrv_config_generator"
    CACHE STRING "Throw-away staging directory (source tree, untracked)")
set(UFSRV_CONFIG_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/src/ufsrv_config"
    CACHE STRING "Composed module output directory")
# Path to ufsrvcorelib source tree for generator include dirs + --repo-dir.
# Default: FetchContent clone under the build tree.
# The bootstrap sets this before including us; this default is a fallback
# for self-test / standalone include.
if(NOT DEFINED UFSRV_CONFIG_UFSRVCORELIB_SOURCE)
    set(UFSRV_CONFIG_UFSRVCORELIB_SOURCE
        "${CMAKE_BINARY_DIR}/ufsrv_config_staging/ufsrvcorelib_clone"
        CACHE STRING "Path to ufsrvcorelib source tree (clone or subtree)")
endif()

message(STATUS "ufsrv_config: composition ON — toolchain ${UFSRV_CONFIG_TOOLCHAIN_REPO}@${UFSRV_CONFIG_TOOLCHAIN_REF}")

# ── Library inventory (set BEFORE composer include — its sanity checks need these) ──

if(NOT DEFINED UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    set(UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES "ufsrvcorelib")
endif()

# ufsrvcorelib
if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufsrvcorelib_REPO)
    set(UFSRV_CONFIG_COMPOSER_ufsrvcorelib_REPO "${UFSRV_CONFIG_TOOLCHAIN_REPO}")
endif()
if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufsrvcorelib_REF)
    set(UFSRV_CONFIG_COMPOSER_ufsrvcorelib_REF "${UFSRV_CONFIG_TOOLCHAIN_REF}")
endif()
if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufsrvcorelib_CATALOGUE)
    set(UFSRV_CONFIG_COMPOSER_ufsrvcorelib_CATALOGUE
        "dev/scripts/ufsrv_config_generator/ufsrv_config_generator_fields.c.input")
endif()

# Per-library generator settings
if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufsrvcorelib_PREFIX)
    set(UFSRV_CONFIG_COMPOSER_ufsrvcorelib_PREFIX "")
endif()
if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufsrvcorelib_INCLUDE_PREFIX)
    set(UFSRV_CONFIG_COMPOSER_ufsrvcorelib_INCLUDE_PREFIX "ufsrvcorelib")
endif()

if("ufnetcorelib" IN_LIST UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufnetcorelib_REPO)
        set(UFSRV_CONFIG_COMPOSER_ufnetcorelib_REPO "git.ro:ufnetcorelib")
    endif()
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufnetcorelib_REF)
        set(UFSRV_CONFIG_COMPOSER_ufnetcorelib_REF "master")
    endif()
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufnetcorelib_CATALOGUE)
        set(UFSRV_CONFIG_COMPOSER_ufnetcorelib_CATALOGUE
            "dev/scripts/ufsrv_config_generator/ufsrv_config_generator_fields.c.input")
    endif()
    # NOTE: the join script (compose_ufsrv_config_join.py) does not yet honour
    # per-library prefixes — it expects uniform filenames across all gen-dirs.
    # Keep this empty until the join script is taught to handle per-library infixes.
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufnetcorelib_PREFIX)
        set(UFSRV_CONFIG_COMPOSER_ufnetcorelib_PREFIX "")
    endif()
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_ufnetcorelib_INCLUDE_PREFIX)
        set(UFSRV_CONFIG_COMPOSER_ufnetcorelib_INCLUDE_PREFIX "ufnetcorelib")
    endif()
endif()

# ── Shared composer config (set BEFORE composer include) ──────────────────

# Toolchain is cloned below via FetchContent — set to LOCAL so
# _composer_checkout_file copies from the clone instead of re-fetching.
set(UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO "LOCAL")
set(UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF  "")
set(UFSRV_CONFIG_COMPOSER_STAGING_DIR    "${UFSRV_CONFIG_STAGING}")
set(UFSRV_CONFIG_COMPOSER_OUTPUT_DIR     "${UFSRV_CONFIG_OUTPUT_DIR}")
set(UFSRV_CONFIG_COMPOSER_SOURCE_DIR     "${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}")
set(UFSRV_CONFIG_COMPOSER_LITERALS_HEADER "${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}/include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h")

# Include composer module (copied to staging by bootstrap)
include("${UFSRV_CONFIG_STAGING}/cmake-modules/compose_ufsrv_config.cmake")

# All libraries already cloned by bootstrap — set to LOCAL for asset extraction
foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    set(UFSRV_CONFIG_COMPOSER_${_lib_name}_REPO "LOCAL")
    set(UFSRV_CONFIG_COMPOSER_${_lib_name}_REF  "")
endforeach()

# ═══════════════════════════════════════════════════════════════════════════════
# 2. Configure-time: checkout all assets from clones into staging
# ═══════════════════════════════════════════════════════════════════════════════

# Toolchain assets (generator, verifier, module files) come from ufsrvcorelib clone
set(UFSRV_CONFIG_COMPOSER_SOURCE_DIR "${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}")
compose_ufsrv_config_bootstrap()

# Per-library catalogues: each from its own FetchContent clone
foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    set(_clone_dir "${CMAKE_BINARY_DIR}/ufsrv_config_staging/${_lib_name}_clone")
    set(UFSRV_CONFIG_COMPOSER_SOURCE_DIR "${_clone_dir}")
    set(_lib_staging "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/${_lib_name}")
    file(MAKE_DIRECTORY "${_lib_staging}")
    _composer_checkout_file("LOCAL" "" "${UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE}"
        "${_lib_staging}" "${_lib_name}/catalogue")
endforeach()

# ═══════════════════════════════════════════════════════════════════════════════
# 4. Export composed source list
# ═══════════════════════════════════════════════════════════════════════════════

compute_composed_sources()

# ═══════════════════════════════════════════════════════════════════════════════
# 5. Build generator host binaries — one per library
# ═══════════════════════════════════════════════════════════════════════════════
# Workaround: the generator reads the catalogue at compile time via
#   #include "catalogue_input.h"
# It cannot select a catalogue at runtime.  To support N libraries we build
# one generator binary per library, each compiled against that library's
# catalogue_input.h.  A proper fix would be a --catalogue CLI flag with a
# fopen-based parser, but that means rewriting the catalogue parser.
#
# See: UFSRVCORELIB_MODULE_UFSRV_CONFIG_DESIGN.md § Catalogue injection mechanism

set(_GEN_SRC "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/ufsrv_config_generator.c")
set(_GEN_COMMON_INCLUDES
    "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}"
    "${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}/include/ufsrvcorelib"
    "${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}/include"
    "${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}/include/ufsrvcorelib/ufsrv_core/ufsrv_config"
    "${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}/include/ufsrvcorelib/ufsrv_core/configfile_processor"
    ${LUA_INCLUDE_DIR}
)

foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    # Per-library catalogue indirection header
    get_filename_component(_cat_fname "${UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE}" NAME)
    set(_cat_abs "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/${_lib_name}/${_cat_fname}")
    set(_gen_bin_dir "${CMAKE_BINARY_DIR}/host/${_lib_name}")
    file(MAKE_DIRECTORY "${_gen_bin_dir}")
    file(WRITE "${_gen_bin_dir}/catalogue_input.h"
         "/* Generated by cmake — do not edit. */\n#include \"${_cat_abs}\"\n")

    # Per-library clone include path (for catalogue headers like
    # <ufnetcorelib/ufnet_msgcore/...> that live in that library's tree)
    set(_lib_clone "${CMAKE_BINARY_DIR}/ufsrv_config_staging/${_lib_name}_clone")

    add_executable(ufsrv_config_generator_${_lib_name} "${_GEN_SRC}")
    target_include_directories(ufsrv_config_generator_${_lib_name} PRIVATE
        ${_GEN_COMMON_INCLUDES}
        "${_gen_bin_dir}"
        "${_lib_clone}/include"
        "${_lib_clone}/include/${_lib_name}"
        "${_lib_clone}/src"
        "${_lib_clone}/packages"
    )
    target_compile_options(ufsrv_config_generator_${_lib_name} PRIVATE -fblocks)
    target_link_libraries(ufsrv_config_generator_${_lib_name} PRIVATE BlocksRuntime)
    set_target_properties(ufsrv_config_generator_${_lib_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host"
    )
endforeach()

# ═══════════════════════════════════════════════════════════════════════════════
# 6. Run generator per library (build-time custom commands)
# ═══════════════════════════════════════════════════════════════════════════════

foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    set(_lib_staging "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/${_lib_name}")
    get_filename_component(_cat_fname "${UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE}" NAME)
    set(_lib_cat "${_lib_staging}/${_cat_fname}")
    set(_lib_gen "${_lib_staging}/generated")
    set(_gen_bin "${CMAKE_BINARY_DIR}/host/ufsrv_config_generator_${_lib_name}")

    # Build generator args (omit --prefix/--include-prefix when empty)
    set(_gen_args --output-dir=${_lib_gen} --repo-dir=${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}
                  --literals-header=${UFSRV_CONFIG_UFSRVCORELIB_SOURCE}/include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h)
    if(NOT UFSRV_CONFIG_COMPOSER_${_lib_name}_INCLUDE_PREFIX STREQUAL "")
        list(APPEND _gen_args --include-prefix=${UFSRV_CONFIG_COMPOSER_${_lib_name}_INCLUDE_PREFIX})
    endif()
    if(NOT UFSRV_CONFIG_COMPOSER_${_lib_name}_PREFIX STREQUAL "")
        list(APPEND _gen_args --prefix=${UFSRV_CONFIG_COMPOSER_${_lib_name}_PREFIX})
    endif()

    add_custom_command(
        OUTPUT
            "${_lib_gen}/ufsrv_config_field_enums_generated.h"
            "${_lib_gen}/ufsrv_config_accessor_decls_generated.h"
            "${_lib_gen}/data_ufsrv_config_field_tables_generated.c"
            "${_lib_gen}/ufsrv_config_accessor_defs_generated.c"
            "${_lib_gen}/ufsrv_config_validate_generated.c"
            "${_lib_gen}/ufsrv_config_getters_setters_generated.c"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_lib_gen}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${_lib_gen}/ufsrv_config_*" "${_lib_gen}/data_ufsrv_config_*"
        COMMAND "${_gen_bin}" ${_gen_args} --header
        COMMAND "${_gen_bin}" ${_gen_args} --source
        DEPENDS ufsrv_config_generator_${_lib_name} "${_lib_cat}"
        COMMENT "ufsrv_config: generating ${_lib_name} → ${_lib_gen}"
        VERBATIM
    )
    list(APPEND _GEN_STAMPS "${_lib_gen}/ufsrv_config_getters_setters_generated.c")
endforeach()

# ═══════════════════════════════════════════════════════════════════════════════
# 7. Run composer (build-time, depends on all generators)
# ═══════════════════════════════════════════════════════════════════════════════
# Uses a stamp file as the OUTPUT so the command re-runs whenever generator
# stamps are newer.  The command removes stale output first, then composes.

# Escape semicolons: CMake interprets ";" as list separator even in quoted -D args
string(JOIN "|" _LIB_NAMES_ESCAPED ${UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES})
set(_COMPOSER_CMAKE_ARGS
    "-DUFSRV_CONFIG_COMPOSER_LIBRARY_NAMES_ESCAPED=${_LIB_NAMES_ESCAPED}"
    "-DUFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO=${UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO}"
    "-DUFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF=${UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF}"
    "-DUFSRV_CONFIG_COMPOSER_STAGING_DIR=${UFSRV_CONFIG_COMPOSER_STAGING_DIR}"
    "-DUFSRV_CONFIG_COMPOSER_OUTPUT_DIR=${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}"
    "-DUFSRV_CONFIG_COMPOSER_SOURCE_DIR=${UFSRV_CONFIG_COMPOSER_SOURCE_DIR}"
    "-DUFSRV_CONFIG_COMPOSER_LITERALS_HEADER=${UFSRV_CONFIG_COMPOSER_LITERALS_HEADER}"
)
foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    list(APPEND _COMPOSER_CMAKE_ARGS
        "-DUFSRV_CONFIG_COMPOSER_${_lib_name}_REPO=${UFSRV_CONFIG_COMPOSER_${_lib_name}_REPO}")
    list(APPEND _COMPOSER_CMAKE_ARGS
        "-DUFSRV_CONFIG_COMPOSER_${_lib_name}_REF=${UFSRV_CONFIG_COMPOSER_${_lib_name}_REF}")
    list(APPEND _COMPOSER_CMAKE_ARGS
        "-DUFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE=${UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE}")
endforeach()

set(_COMPOSER_STAMP "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}/.composed")

# Use add_custom_command with explicit OUTPUT so CMake knows these files are
# generated.  The stamp ensures the command re-runs when generator output changes.
add_custom_command(
    OUTPUT ${UFSRV_CONFIG_COMPOSED_ALL_OUTPUTS} "${_COMPOSER_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} ${_COMPOSER_CMAKE_ARGS}
        -P "${UFSRV_CONFIG_STAGING}/cmake-modules/compose_ufsrv_config_run.cmake"
    COMMAND ${CMAKE_COMMAND} -E touch "${_COMPOSER_STAMP}"
    DEPENDS ${_GEN_STAMPS}
    COMMENT "ufsrv_config: composing src/ufsrv_config/"
    VERBATIM
)

# ═══════════════════════════════════════════════════════════════════════════════
# 8. Convenience target — run generator + composer, nothing else
# ═══════════════════════════════════════════════════════════════════════════════

add_custom_target(ufsrv_config_compose
    DEPENDS ${_COMPOSER_STAMP}
    COMMENT "ufsrv_config: generator + composer complete"
)

# ═══════════════════════════════════════════════════════════════════════════════
# 9. Clean target — removes all throw-away assets and composed output
# ═══════════════════════════════════════════════════════════════════════════════

# Toolchain asset files placed in staging by the bootstrap.
# If a new asset is added to compose_ufsrv_config_bootstrap(), add its
# filename here so the clean target removes it.
set(UFSRV_CONFIG_STAGING_TOOLCHAIN_FILES
    ufsrv_config_generator.c
    ufsrv_config_verify.py
    ufsrv_config.c
    ufsrv_config.h
    ufsrv_config_generic_callbacks.c
    ufsrv_config_generic_callbacks_priv.h
)

# Build the clean-command list.
# Staging directory itself is NOT removed — preserves a tracked .gitkeep
# fixture.  Only explicitly listed contents are cleaned.
set(_clean_cmds
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${UFSRV_CONFIG_STAGING}/cmake-modules"
)
foreach(_f IN LISTS UFSRV_CONFIG_STAGING_TOOLCHAIN_FILES)
    list(APPEND _clean_cmds
        COMMAND ${CMAKE_COMMAND} -E rm -f "${UFSRV_CONFIG_STAGING}/${_f}"
    )
endforeach()
list(APPEND _clean_cmds
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/ufsrv_config_staging"
)
# Remove per-library staging subdirectories
foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    list(APPEND _clean_cmds
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${UFSRV_CONFIG_STAGING}/${_lib_name}"
    )
endforeach()
# Remove FetchContent _deps for every library (convention: <lib>_toolchain)
foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    string(MAKE_C_IDENTIFIER "${_lib_name}_toolchain" _fc_name)
    list(APPEND _clean_cmds
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/_deps/${_fc_name}-subbuild"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/_deps/${_fc_name}-src"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/_deps/${_fc_name}-build"
    )
endforeach()

add_custom_target(ufsrv_config_clean
    ${_clean_cmds}
    COMMENT "ufsrv_config: removing staging + composed output + clones"
    VERBATIM
)

# ═══════════════════════════════════════════════════════════════════════════════
# 9. Wire composed sources into the server target
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT TARGET ${UFSRV_CONFIG_SERVER_TARGET})
    message(FATAL_ERROR "ufsrv_config: UFSRV_CONFIG_SERVER_TARGET='${UFSRV_CONFIG_SERVER_TARGET}' is not a target")
endif()

target_sources(${UFSRV_CONFIG_SERVER_TARGET} PRIVATE ${UFSRV_CONFIG_COMPOSED_SOURCES})
target_include_directories(${UFSRV_CONFIG_SERVER_TARGET} PRIVATE ${UFSRV_CONFIG_COMPOSED_INCLUDE_DIR})
