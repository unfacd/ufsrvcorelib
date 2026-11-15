# packages/ufsrvscriptlib/tests/CMakeLists.txt — gtest suite for the Lua engine.
#
# Uses the shared add_c_test() macro and gtest_shared_main object library
# defined by the top-level tests/ subdirectory (added earlier in the
# _PACKAGE_TESTS block). The test links the ufsrvscriptlib static library, which
# carries its own include dirs and liblua transitively via PUBLIC linkage.

add_c_test(
    NAME ufsrvscriptlib_lua_test
    TEST_SOURCE ufsrvscriptlib_lua_test.cpp
    LIBRARIES ufsrvscriptlib
)

add_c_test(
    NAME ufsrvscriptlib_json_test
    TEST_SOURCE ufsrvscriptlib_json_test.cpp
    LIBRARIES ufsrvscriptlib
)

add_c_test(
    NAME ufsrvscriptlib_protobuf_test
    TEST_SOURCE ufsrvscriptlib_protobuf_test.cpp
    LIBRARIES ufsrvscriptlib
)


add_c_test(
    NAME ufsrvscriptlib_eval_test
    TEST_SOURCE ufsrvscriptlib_eval_test.cpp
    LIBRARIES ufsrvscriptlib
)
