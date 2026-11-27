#
# tests/jwt/CMakeLists.txt — compact JWS minting and verification
#
# The module is compiled directly rather than linked out of the archive: it
# depends only on OpenSSL, json-c and uflib, and pulling in the whole library
# would drag in the session and worker code the consumer supplies.
#

add_c_test(
    NAME jwt_tests
    TEST_SOURCE jwt_tests.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/jwt/jwt.c
    INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
    LIBRARIES
        uflib::uflib
        PkgConfig::JSONC
        OpenSSL::Crypto
)
