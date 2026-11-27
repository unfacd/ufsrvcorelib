#
# tests/ufsrv_core/main_utils/CMakeLists.txt — CLI-defaults / secret-key-store tests
#
# Exercises the real ConfigBootstrapCliDefaults() and
# ConfigFinalizeSensitiveConfig() from src/ufsrv_core/main_utils.c against the
# real composed config store. Privilege, pid-file and config-file-validation
# concerns of that TU are stubbed in main_utils_stubs.c.
#
# The generated config module is compiled as discrete fragments (the layout
# corelib's own tests use), not via the master include.
#

add_c_test(
    NAME ufsrv_core_main_utils_tests
    TEST_SOURCE main_utils_cli_defaults_tests.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/main_utils.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_generic_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_as_string.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_accessor_defs.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/data_ufsrv_config_field_tables_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_defs_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_validate_generated.c
        main_utils_harness.c
        main_utils_stubs.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        uflib::uflib
        ${LUA_LIBRARY}
        bsd
)

target_compile_definitions(ufsrv_core_main_utils_tests PRIVATE
    HAVE_CONFIG_UFSRVCORELIB_H)
