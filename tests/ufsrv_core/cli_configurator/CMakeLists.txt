#
# tests/ufsrv_core/cli_configurator/CMakeLists.txt
#
# CLI argument parser tests — compiles ufsrv_cli_configurator.c directly
# (lightweight; only uflib dependency).
#

add_c_test(
    NAME ufsrv_cli_configurator_tests
    TEST_SOURCE ufsrv_cli_configurator_tests.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/cli_configurator/ufsrv_cli_configurator.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/cli_configurator
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
    LIBRARIES
        uflib::uflib
)
