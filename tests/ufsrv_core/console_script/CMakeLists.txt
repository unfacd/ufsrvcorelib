add_c_test(
    NAME ufsrv_console_script_tests
    TEST_SOURCE console_script_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_console_script/ufsrv_console_script.c
        console_script_eval_stub.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/ufsrv_console_script
    LIBRARIES
        uflib::uflib
)
