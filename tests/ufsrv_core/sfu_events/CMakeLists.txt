add_c_test(
    NAME sfu_event_wire_tests
    TEST_SOURCE sfu_event_wire_tests.cpp
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include
)

if(libSRTP_FOUND)
    add_c_test(
        NAME sfu_event_egress_tests
        TEST_SOURCE sfu_event_egress_tests.cpp
        C_SOURCES
            sfu_shard_event_fixture.c
            ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/events/sfu_event_egress.c
            ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server/events/sfu_shard_events.c
        INCLUDE_DIRS
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
            ${CMAKE_SOURCE_DIR}/src/ufsrv_core/sfu_server
            ${SRTP3_INCLUDE_DIR}
        LIBRARIES uflib::uflib pthread
    )
endif()
