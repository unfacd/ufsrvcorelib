#
# cmake-modules/ufsrv_config/host/ufsrv_config_check_host.cmake
#
# Integrity guard for the ufsrv_config composition output.
# Invoked (via cmake -P) before the server target compiles (wired in step 6):
#
#   cmake -DUFSRV_CONFIG_SOURCE_DIR=... -P cmake-modules/ufsrv_config/host/ufsrv_config_check_host.cmake
#
# Verifies the .composed stamp is present AND the deep-tree layout is complete.
# On any inconsistency it FAILS with a clear error naming exactly what is missing
# and the two commands to run — it never silently re-composes.
#

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED UFSRV_CONFIG_SOURCE_DIR)
    message(FATAL_ERROR "ufsrv_config_check_host: UFSRV_CONFIG_SOURCE_DIR must be passed via -D")
endif()

set(_DPR "${UFSRV_CONFIG_SOURCE_DIR}/src/ufsrv_core/ufsrv_config")
set(_DPU "${UFSRV_CONFIG_SOURCE_DIR}/src/include/ufsrvcorelib/ufsrv_core/ufsrv_config")
# A site may place the deep tree elsewhere (libraries have no src/include/ nesting).
if(DEFINED UFSRV_CONFIG_HOST_PRIVATE_ROOT AND NOT UFSRV_CONFIG_HOST_PRIVATE_ROOT STREQUAL "")
    set(_DPR "${UFSRV_CONFIG_HOST_PRIVATE_ROOT}")
endif()
if(DEFINED UFSRV_CONFIG_HOST_PUBLIC_ROOT AND NOT UFSRV_CONFIG_HOST_PUBLIC_ROOT STREQUAL "")
    set(_DPU "${UFSRV_CONFIG_HOST_PUBLIC_ROOT}")
endif()

# Expected deep-tree layout (no .gitkeep checks — content is tracked directly).
set(_REQUIRED
    "${_DPR}/.composed"
    # invariants (private)
    "${_DPR}/ufsrv_config.c"
    "${_DPR}/ufsrv_config_generic_callbacks.c"
    "${_DPR}/ufsrv_config_generic_callbacks_priv.h"
    # private generated (6)
    "${_DPR}/generated/data_ufsrv_config_field_tables_generated.c"
    "${_DPR}/generated/data_ufsrv_config_dispatch_table_priv_generated.h"
    "${_DPR}/generated/data_ufsrv_config_literal_to_field_map_priv_generated.h"
    "${_DPR}/generated/ufsrv_config_accessor_defs_generated.c"
    "${_DPR}/generated/ufsrv_config_getters_setters_generated.c"
    "${_DPR}/generated/ufsrv_config_validate_generated.c"
    # public generated (2)
    "${_DPU}/generated/ufsrv_config_field_enums_generated.h"
    "${_DPU}/generated/ufsrv_config_accessor_decls_generated.h"
    # shim
    "${_DPU}/ufsrv_config_shim.h")

set(_missing "")
foreach(_f IN LISTS _REQUIRED)
    if(NOT EXISTS "${_f}")
        string(APPEND _missing "    ${_f}\n")
    endif()
endforeach()

if(NOT _missing STREQUAL "")
    message(FATAL_ERROR
        "ufsrv_config_check_host: deep tree incomplete or .composed missing.\n"
        "Missing:\n${_missing}"
        "Run:\n"
        "    cmake --build build --target ufsrv_config_bootstrap_host\n"
        "    cmake --build build --target ufsrv_config_compose_host\n")
endif()

message(STATUS "ufsrv_config_check_host: deep tree consistent (.composed + 12 files present)")
