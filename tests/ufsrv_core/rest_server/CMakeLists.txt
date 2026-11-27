add_c_test(
    NAME rest_server_tests
    TEST_SOURCE rest_server_test.cpp
    INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
    LIBRARIES
        ufsrvcorelib
)
