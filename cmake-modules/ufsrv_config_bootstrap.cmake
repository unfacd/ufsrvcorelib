#
# cmake-modules/ufsrv_config_bootstrap.cmake
#
# Bootstrap for the ufsrv_config composition pipeline.
# Fetches ufsrvcorelib + all library catalogues via FetchContent,
# copies cmake modules into staging, then includes the composition module.
#
# Required before include:
#   UFSRV_CONFIG_SERVER_TARGET — name of the server target
#
# Optional overrides (set before include):
#   UFSRV_CONFIG_TOOLCHAIN_REPO, _REF
#   UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES
#   Per-library UFSRV_CONFIG_COMPOSER_<name>_REPO, _REF, _CATALOGUE
#

include_guard(GLOBAL)

set(UFSRV_CONFIG_TOOLCHAIN_REPO "git.ro:ufsrvcorelib"
    CACHE STRING "Git repo for ufsrvcorelib")
set(UFSRV_CONFIG_TOOLCHAIN_REF "master"
    CACHE STRING "Git ref for toolchain")
set(UFSRV_CONFIG_STAGING "${CMAKE_SOURCE_DIR}/dev/scripts/ufsrv_config_generator"
    CACHE STRING "Throw-away staging directory")

# Library list (must be set before composition module is included)
if(NOT DEFINED UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    set(UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES "ufsrvcorelib")
endif()

# All clones land under the same build-tree prefix
set(_UFSRV_CLONE_PREFIX "${CMAKE_BINARY_DIR}/ufsrv_config_staging")

# ── Per-library defaults (convention-based, overridable) ──────────────────
# Every library ships its catalogue at the same relative path.
# The git repo is git.ro:<library_name> unless overridden.

foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_${_lib_name}_REPO)
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_REPO "git.ro:${_lib_name}")
    endif()
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_${_lib_name}_REF)
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_REF "master")
    endif()
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE)
        set(UFSRV_CONFIG_COMPOSER_${_lib_name}_CATALOGUE
            "dev/scripts/ufsrv_config_generator/ufsrv_config_generator_fields.c.input")
    endif()
endforeach()

# ═══════════════════════════════════════════════════════════════════════════════
# Clone all libraries via FetchContent — one uniform loop.
# No special-casing.  The POPULATED guard makes it idempotent when the host
# has already fetched a library (e.g. ufsrvcorelib in src/CMakeLists.txt).
# ═══════════════════════════════════════════════════════════════════════════════

include(FetchContent)

foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    string(MAKE_C_IDENTIFIER "${_lib_name}_toolchain" _fc_name)
    set(_clone_dir "${_UFSRV_CLONE_PREFIX}/${_lib_name}_clone")

    FetchContent_GetProperties(${_fc_name})
    if(NOT ${_fc_name}_POPULATED)
        FetchContent_Declare(
            ${_fc_name}
            GIT_REPOSITORY "${UFSRV_CONFIG_COMPOSER_${_lib_name}_REPO}"
            GIT_TAG        "${UFSRV_CONFIG_COMPOSER_${_lib_name}_REF}"
            GIT_SHALLOW    TRUE
            SOURCE_DIR     "${_clone_dir}"
        )
        FetchContent_Populate(${_fc_name})
    endif()
endforeach()

# Copy cmake modules from ufsrvcorelib clone into staging
set(_core_clone "${_UFSRV_CLONE_PREFIX}/ufsrvcorelib_clone")
file(MAKE_DIRECTORY "${UFSRV_CONFIG_STAGING}/cmake-modules")
file(COPY "${_core_clone}/cmake-modules/compose_ufsrv_config.cmake"
     DESTINATION "${UFSRV_CONFIG_STAGING}/cmake-modules")
file(COPY "${_core_clone}/cmake-modules/compose_ufsrv_config_run.cmake"
     DESTINATION "${UFSRV_CONFIG_STAGING}/cmake-modules")
file(COPY "${_core_clone}/cmake-modules/compose_ufsrv_config_join.py"
     DESTINATION "${UFSRV_CONFIG_STAGING}/cmake-modules")
file(COPY "${_core_clone}/cmake-modules/ufsrv_config_composition.cmake"
     DESTINATION "${UFSRV_CONFIG_STAGING}/cmake-modules")

# Export ufsrvcorelib source path for the composition module (generator includes etc.)
set(UFSRV_CONFIG_UFSRVCORELIB_SOURCE "${_core_clone}"
    CACHE STRING "Path to ufsrvcorelib source tree for generator")

include("${UFSRV_CONFIG_STAGING}/cmake-modules/ufsrv_config_composition.cmake")
