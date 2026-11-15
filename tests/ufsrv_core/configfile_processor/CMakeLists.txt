add_c_test(
    NAME conf_file_processor_tests
    TEST_SOURCE configfile_processor_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/configfile_processor/configfile_processor.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/configfile_processor
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/configfile_processor
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        ${LUA_LIBRARY}
)

target_compile_options(conf_file_processor_tests PRIVATE -fblocks)

target_compile_definitions(conf_file_processor_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_c_test(
    NAME conf_file_processor_adversarial_tests
    TEST_SOURCE configfile_processor_adversarial_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/configfile_processor/configfile_processor.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/configfile_processor
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/configfile_processor
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        ${LUA_LIBRARY}
)

target_compile_options(conf_file_processor_adversarial_tests PRIVATE
    -fblocks
    -fsanitize=address
    -fsanitize=undefined
    -fsanitize=leak
    -fno-omit-frame-pointer
    -g
    -O0
)

target_link_options(conf_file_processor_adversarial_tests PRIVATE
    -fsanitize=address
    -fsanitize=undefined
    -fsanitize=leak
)

target_compile_definitions(conf_file_processor_adversarial_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_c_test(
    NAME conf_file_validate_adversarial_tests
    TEST_SOURCE conf_file_validate_adversarial_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/configfile_processor/configfile_processor.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/configfile_processor
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/configfile_processor
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        ${LUA_LIBRARY}
)

target_compile_options(conf_file_validate_adversarial_tests PRIVATE
    -fblocks
    -fsanitize=address
    -fsanitize=undefined
    -fsanitize=leak
    -fno-omit-frame-pointer
    -g
    -O0
)

target_link_options(conf_file_validate_adversarial_tests PRIVATE
    -fsanitize=address
    -fsanitize=undefined
    -fsanitize=leak
)

target_compile_definitions(conf_file_validate_adversarial_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
