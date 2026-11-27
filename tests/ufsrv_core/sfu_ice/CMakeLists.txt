#
# tests/ufsrv_core/sfu_ice — the STUN/ICE responder against real sockets.
#
# The engine modules are compiled directly rather than linked out of the
# archive, which would drag in the composed config store this test does not
# need; the codec comes from the library's own turn module.
#

add_c_test(
    NAME sfu_ice_tests
    TEST_SOURCE sfu_ice_tests.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/command/sfu_console_router.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_server.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/events/sfu_event_egress.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/events/sfu_shard_events.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_onboarding.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_thread.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_describe.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_flow.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_ice.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_nack.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_room.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_subscription.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/sfu_shard_track.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/rtcp/sfu_rtcp_parse.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/rtcp/sfu_shard_rtcp.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/srtp/sfu_shard_srtp.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/dtls/sfu_dtls_context.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/dtls/sfu_shard_dtls.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/turn/codec/ns_turn_msg.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/turn/codec/ns_turn_msg_addr.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/turn/codec/ns_turn_ioaddr.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/turn/codec/stun_buffer.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/turn/codec/turn_alloc.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server
        ${SRTP3_INCLUDE_DIR}
    LIBRARIES
        uflib::uflib
        OpenSSL::SSL
        OpenSSL::Crypto
        libSRTP::srtp3
        pthread
)
