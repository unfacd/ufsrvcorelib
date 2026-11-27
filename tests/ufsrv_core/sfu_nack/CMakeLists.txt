#
# tests/ufsrv_core/sfu_nack — the retention ring, without a socket.
#
# Only the NACK module is compiled over a hand-built shard: the pool, the ring
# descriptors and the spec are laid out by the fixture, so grant, eviction,
# stale-hit refusal and pressure clamping are tested in isolation.
#

add_c_test(
    NAME sfu_nack_tests
    TEST_SOURCE sfu_nack_tests.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_nack.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server
        ${SRTP3_INCLUDE_DIR}
    LIBRARIES
        uflib::uflib
        libSRTP::srtp3
        pthread
)
