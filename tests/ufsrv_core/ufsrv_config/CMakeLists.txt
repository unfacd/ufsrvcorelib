#
# tests/ufsrv_core/ufsrv_config/CMakeLists.txt
# ── Config module tests with ASan/LSan/UBSan instrumentation.
#
# Two build modes — mutually exclusive (ASan and Valgrind both replace malloc):
#
# ── Sanitizer build (ASan + LSan + UBSan) ───────────────────────────────
#   cmake --preset dev -B build -DUFSRV_CONFIG_MODULE=ON -DUFSRV_CONFIG_GENERATOR_ON=ON -D_PACKAGE_TESTS=ON
#   cmake --build build --target ufsrv_config_accessor_exhaustive_test ufsrv_config_stride_fix_test -j$(nproc)
#
#   # Run with strictest runtime options:
#   ASAN_OPTIONS="strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:halt_on_error=1" \
#   UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
#   LSAN_OPTIONS="suppressions=/dev/null" \
#   ./build/tests/ufsrv_core/ufsrv_config/ufsrv_config_accessor_exhaustive_test
#
#   ASAN_OPTIONS="strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:halt_on_error=1" \
#   UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
#   LSAN_OPTIONS="suppressions=/dev/null" \
#   ./build/tests/ufsrv_core/ufsrv_config/ufsrv_config_stride_fix_test
#
# ── Valgrind build (no sanitizers) ──────────────────────────────────────
#   cmake -B build_valgrind -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang \
#       -DCMAKE_CXX_COMPILER=clang++ -D_PACKAGE_TESTS=ON \
#       -DUFSRV_CONFIG_MODULE=ON -DUFSRV_CONFIG_GENERATOR_ON=OFF \
#       -DUFSRV_BUILD_WITH_ASAN=OFF -DUFSRV_BUILD_WITH_UBSAN=OFF \
#       -DUFSRV_BUILD_WITH_LSAN=OFF -DVALGRIND_BUILD=ON
#   cmake --build build_valgrind --target ufsrv_config_accessor_exhaustive_test ufsrv_config_stride_fix_test -j$(nproc)
#
#   # Run under Valgrind memcheck:
#   valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
#       --track-fds=yes --errors-for-leak-kinds=all --error-exitcode=1 \
#       ./build_valgrind/tests/ufsrv_core/ufsrv_config/ufsrv_config_accessor_exhaustive_test
#
#   valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
#       --track-fds=yes --errors-for-leak-kinds=all --error-exitcode=1 \
#       ./build_valgrind/tests/ufsrv_core/ufsrv_config/ufsrv_config_stride_fix_test

# ── ValuesInstance stride-fix proof — uses correctly-typed
#    UfsrvConfigFieldEntry[] stores and proves all 54 slots are
#    independently addressable through ValuesInstance + generated accessors.

add_c_test(
    NAME ufsrv_config_stride_fix_test
    TEST_SOURCE ufsrv_config_stride_fix_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_generic_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/data_ufsrv_config_field_tables_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_defs_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_validate_generated.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        BlocksRuntime
)

if(NOT VALGRIND_BUILD)
target_compile_options(ufsrv_config_stride_fix_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
    -fsanitize-address-use-after-scope
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
)

target_link_options(ufsrv_config_stride_fix_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
)
else()
target_compile_options(ufsrv_config_stride_fix_test PRIVATE
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
    -ggdb3
)
endif()

# ── Accessor exhaustive test — exercises every generated setter/getter
#    (all 54 fields) with adversarial edge-case inputs.

add_c_test(
    NAME ufsrv_config_accessor_exhaustive_test
    TEST_SOURCE ufsrv_config_accessor_exhaustive_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_generic_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/data_ufsrv_config_field_tables_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_defs_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_validate_generated.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        BlocksRuntime
)

if(NOT VALGRIND_BUILD)
target_compile_options(ufsrv_config_accessor_exhaustive_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
    -fsanitize-address-use-after-scope
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
    -Wno-incompatible-pointer-types
)

target_link_options(ufsrv_config_accessor_exhaustive_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
)
else()
target_compile_options(ufsrv_config_accessor_exhaustive_test PRIVATE
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
    -Wno-incompatible-pointer-types
    -ggdb3
)
endif()

# ── Setter delegation test — verifies every named setter dispatches
#    through lifecycle.on_value_available (the catalogue-wired callback).

add_c_test(
    NAME ufsrv_config_setter_delegation_test
    TEST_SOURCE ufsrv_config_setter_delegation_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_generic_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/data_ufsrv_config_field_tables_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_defs_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_validate_generated.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        BlocksRuntime
)

if(NOT VALGRIND_BUILD)
target_compile_options(ufsrv_config_setter_delegation_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
    -fsanitize-address-use-after-scope
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
)

target_link_options(ufsrv_config_setter_delegation_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
)
else()
target_compile_options(ufsrv_config_setter_delegation_test PRIVATE
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
    -ggdb3
)
endif()

# ── Defaults populate adversarial test — verifies PopulateDefaults functions
#    correctly load compiled defaults, handling edge cases, forced zeroing,
#    NULL arguments, and value-type round-trips.

add_c_test(
    NAME ufsrv_config_defaults_populate_adversarial_test
    TEST_SOURCE ufsrv_config_defaults_populate_adversarial_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_generic_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/data_ufsrv_config_field_tables_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_defs_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_validate_generated.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        BlocksRuntime
)

if(NOT VALGRIND_BUILD)
target_compile_options(ufsrv_config_defaults_populate_adversarial_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
    -fsanitize-address-use-after-scope
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
)

target_link_options(ufsrv_config_defaults_populate_adversarial_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
)
else()
target_compile_options(ufsrv_config_defaults_populate_adversarial_test PRIVATE
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
    -ggdb3
)
endif()

# ── DistinctArray custom accessor test — exercises the hand-authored
#    DistinctArray-backed custom setters + AsString marshallers
#    (ssl_command_console allow/deny lists, fileloader black-list registry).
#    These stub files are NOT compiled into the library, so they are added
#    directly to the test's C_SOURCES.
#
#    uflib (static) references utf8proc_iterate from utils_str.c but does not
#    propagate the dependency, so the test executable must link it explicitly.

find_library(UTF8PROC_LIBRARY utf8proc PATHS /opt/lib)

add_c_test(
    NAME ufsrv_config_distinct_array_test
    TEST_SOURCE ufsrv_config_distinct_array_test.cpp
    C_SOURCES
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_generic_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_defs_generated.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_accessor_defs.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_as_string.c
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/instrumentation/instrumentation_statsd.c
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
        ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib
        ${CMAKE_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIR}
    LIBRARIES
        uflib::uflib
        ${UTF8PROC_LIBRARY}
        BlocksRuntime
)

if(NOT VALGRIND_BUILD)
target_compile_options(ufsrv_config_distinct_array_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
    -fsanitize-address-use-after-scope
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
)

target_link_options(ufsrv_config_distinct_array_test PRIVATE
    -fsanitize=address,leak,undefined,pointer-compare,pointer-subtract
    -fno-sanitize-recover=all
)
else()
target_compile_options(ufsrv_config_distinct_array_test PRIVATE
    -fno-omit-frame-pointer
    -fblocks
    -Wno-c99-designator
    -ggdb3
)
endif()

# ── Generator end-to-end test ───────────────────────────────────────────────
# Runs the REAL generator host binary against the REAL cold catalogue in
# decomposed mode (--header + --source), then runs the standalone verifier over
# the generated output and asserts zero failures.  This is the same pipeline
# the build's ufsrv_config_all target chains, exercised here as a CTest test
# against a throw-away staging dir.
#
# Only registered when UFSRV_CONFIG_MODULE=ON (which builds the generator host
# binary); otherwise the generator does not exist and the test would be noise.

if(UFSRV_CONFIG_MODULE)
    # The generator is built by `cmake --build` (part of `all`), and CMake
    # recompiles it via the catalogue_input.h include dependency whenever the
    # catalogue or generator source changes.  Point the test at that target's
    # output directly — no manual compile step, no nested build.
    add_test(
        NAME ufsrv_config_generator_e2e_test
        COMMAND python3
            ${CMAKE_CURRENT_SOURCE_DIR}/generator_e2e_test.py
            --generator $<TARGET_FILE:ufsrv_config_generator>
            --catalogue ${CMAKE_SOURCE_DIR}/dev/scripts/ufsrv_config_generator/ufsrv_config_generator_fields.c.input
            --verifier ${CMAKE_SOURCE_DIR}/dev/scripts/ufsrv_config_generator/ufsrv_config_verify.py
            --repo-dir ${CMAKE_SOURCE_DIR}
            --literals-header ${CMAKE_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h
            --include-prefix ufsrvcorelib
    )
    set_tests_properties(ufsrv_config_generator_e2e_test PROPERTIES
        SKIP_RETURN_CODE 2
        TIMEOUT 300
    )
endif()
