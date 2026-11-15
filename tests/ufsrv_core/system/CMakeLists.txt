add_c_test(
    NAME ufsrv_cpu_affinity_tests
    TEST_SOURCE cpu_affinity_policy_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/system/utils_cpu.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/system
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/system
)
