#
# cmake-modules/compose_ufsrv_config_run.cmake
#
# Runner script — invoked via cmake -P from add_custom_command at build time.
# All required variables are passed via -D from the calling CMakeLists.txt.
# Includes the composer module, runs bootstrap (idempotent — fetches any
# assets not already in staging), then runs composition.
#

# Unescape library names (passed as pipe-separated to avoid CMake list splitting)
if(NOT DEFINED UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    if(DEFINED UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES_ESCAPED)
        string(REPLACE "|" ";" UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES "${UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES_ESCAPED}")
    endif()
endif()

# Validate required inputs
foreach(_v IN ITEMS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES
                  UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO
                  UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF
                  UFSRV_CONFIG_COMPOSER_STAGING_DIR
                  UFSRV_CONFIG_COMPOSER_OUTPUT_DIR
                  UFSRV_CONFIG_COMPOSER_SOURCE_DIR)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "compose_ufsrv_config_run: ${_v} must be set via -D")
    endif()
endforeach()

# Validate per-library vars (using the unescaped list)
foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    foreach(_suffix IN ITEMS REPO REF CATALOGUE)
        if(NOT DEFINED UFSRV_CONFIG_COMPOSER_${_lib_name}_${_suffix})
            message(FATAL_ERROR "compose_ufsrv_config_run: UFSRV_CONFIG_COMPOSER_${_lib_name}_${_suffix} not set")
        endif()
    endforeach()
endforeach()

# Include and run
# Bootstrap already ran at configure time — only compose at build time
set(_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")
include("${_SCRIPT_DIR}/compose_ufsrv_config.cmake")
compose_ufsrv_config()             # join/copy → composed output
