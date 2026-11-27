#
# tests/ufsrv_core/sfu_rtcp — the compound walker against byte fixtures.
#
# Only the parser is compiled: it has no engine dependency, so the awkward
# cases (truncation, padding, feedback-first compounds, the muxed payload type)
# are tested without a shard or a socket.
#

add_c_test(
    NAME sfu_rtcp_tests
    TEST_SOURCE sfu_rtcp_tests.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/rtcp/sfu_rtcp_parse.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server
    LIBRARIES
        uflib::uflib
        pthread
)
