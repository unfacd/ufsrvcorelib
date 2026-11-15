# ufsrvcorelib — CPU affinity and thread management library
#
# Dual-mode (mirrors ufnetcorelib / uflib pattern):
#   IS_ROOT_PROJECT=ON  → STATIC library + install + pkg-config + CPack
#   IS_ROOT_PROJECT=OFF → OBJECT library (compiled into parent via add_subdirectory)
#
# Usage:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j$(nproc)
#   cmake --install build --prefix /usr/local
#   cmake --build build --target package    # .deb

cmake_minimum_required(VERSION 3.20)

set(_PROJECT_NAME "ufsrvcorelib")
string(TOUPPER ${_PROJECT_NAME} _PROJECT_NAME_X)

MACRO(getVersionPatchLevel name)
    SET(VERSION_REGEX "^#define ${name} (.+)$")
    FILE(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/version.h"
            VERSION_BIT REGEX ${VERSION_REGEX})
    STRING(REGEX REPLACE ${VERSION_REGEX} "\\1" ${name} "${VERSION_BIT}")
ENDMACRO(getVersionPatchLevel)

getVersionPatchLevel(${_PROJECT_NAME_X}_MAJOR)
getVersionPatchLevel(${_PROJECT_NAME_X}_MINOR)
getVersionPatchLevel(${_PROJECT_NAME_X}_PATCH)
getVersionPatchLevel(${_PROJECT_NAME_X}_INTERNAL)
SET(${_PROJECT_NAME_X}_VERSION "${${_PROJECT_NAME_X}_MAJOR}.${${_PROJECT_NAME_X}_MINOR}.${${_PROJECT_NAME_X}_PATCH}")
if(NOT "${${_PROJECT_NAME_X}_INTERNAL}" STREQUAL "")
    SET(${_PROJECT_NAME_X}_VERSION "${${_PROJECT_NAME_X}_VERSION}.${${_PROJECT_NAME_X}_INTERNAL}")
endif()
MESSAGE("Detected ${_PROJECT_NAME_X} version: ${${_PROJECT_NAME_X}_VERSION}")

project(${_PROJECT_NAME}
    VERSION ${${_PROJECT_NAME_X}_VERSION}
    LANGUAGES C
)

# ── Shared ufsrv build options ───────────────────────────────────────────────
# clang enforcement, base compile flags, and the UFSRV_BUILD_WITH_{ASAN,MSAN,TSAN}
# toggles live in the shared module. Warning flags are THIS repo's own policy —
# set them before including the module. Invoke via a preset: `cmake --preset debug`.
#
# _UFSRV_BUILD_FLAVOUR is set BEFORE the module include so the build digest reports
# the correct output kind (OBJECT/STATIC/SHARED+STATIC) — the module reads it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(BUILD_SHARED_LIBS)
        set(_UFSRV_BUILD_FLAVOUR "SHARED+STATIC")
    else()
        set(_UFSRV_BUILD_FLAVOUR "STATIC")
    endif()
else()
    set(_UFSRV_BUILD_FLAVOUR "OBJECT")
endif()

# Ensure git uses the repo's custom hooks directory (pre-commit → ufsrv_config
# version management). Safe: only touches .git/config — no tracked files.
execute_process(
    COMMAND git config --local core.hooksPath .githooks
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    ERROR_QUIET
)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules)

# ── ufsrv_config: self-test composition mode ──────────────────────────────────
# Must be declared BEFORE ufsrv_build_options so the build digest reflects the
# correct generator state (forced ON when module is ON).
option(UFSRV_CONFIG_MODULE "Enable ufsrv_config self-test mode (N=1 composition)" OFF)
if(UFSRV_CONFIG_MODULE)
    set(UFSRV_CONFIG_GENERATOR_ON ON CACHE BOOL "Run ufsrv_config_generator at build time" FORCE)
endif()

set(UFSRV_C_WARNING_FLAGS
    -Wall -Wimplicit-fallthrough -Wno-unused-label -Wno-unused-variable
    -Wno-comment -Wno-nullability-completeness -Wno-unknown-attributes)
set(UFSRV_C_DEBUG_FLAGS_DEFAULT -ggdb3)
include(ufsrv_build_options)

add_definitions(-DHAVE_CONFIG_UFSRVCORELIB_H)

# Always emit compile_commands.json so tools (clang-tidy, LSP, ...) can
# resolve include paths without manual configuration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(UF_TESTING "Enable testing mode" ON)
option(UF_DEV "Enable development mode" ON)
option(UFSRV_DEBUG_BUILD_L2 "Verbose level-2 debug logging" OFF)
option(UFSRV_DEBUG_BUILD_L3 "Very verbose level-3 debug logging (per-object traces)" OFF)
if(UF_TESTING)
    set(__UF_TESTING 1)
else()
    set(__UF_TESTING 0)
endif()
if(UF_DEV)
    set(__UF_DEV 1)
else()
    set(__UF_DEV 0)
endif()
# L3 implies L2, and either implies the base debug build switch.
if(UFSRV_DEBUG_BUILD_L3)
    set(UFSRV_DEBUG_L3 1)
    set(UFSRV_DEBUG_L2 1)
else()
    set(UFSRV_DEBUG_L3 0)
    if(UFSRV_DEBUG_BUILD_L2)
        set(UFSRV_DEBUG_L2 1)
    else()
        set(UFSRV_DEBUG_L2 0)
    endif()
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR UFSRV_DEBUG_L2 OR UFSRV_DEBUG_L3)
    set(UFSRV_DEBUG_BUILD 1)
else()
    set(UFSRV_DEBUG_BUILD 0)
endif()
add_compile_definitions(UFSRV_DEBUG_BUILD=${UFSRV_DEBUG_BUILD}
                        UFSRV_DEBUG_BUILD_L2=${UFSRV_DEBUG_L2}
                        UFSRV_DEBUG_BUILD_L3=${UFSRV_DEBUG_L3})

include(config_defaults_definitions)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config_ufsrvcorelib.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config_ufsrvcorelib.h
    @ONLY
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(IS_ROOT_PROJECT ON)
else()
    set(IS_ROOT_PROJECT OFF)
endif()

option(_PACKAGE_TESTS "Enable test targets" OFF)
option(_PACKAGE_INSTALL "Enable install target" ${IS_ROOT_PROJECT})

if(_PACKAGE_TESTS)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.0
    )
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

if(IS_ROOT_PROJECT)
    set(DOXYGEN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}" CACHE PATH "Doxygen HTML output directory (default: repo root)")
    find_package(Doxygen QUIET)
    if(Doxygen_FOUND)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxyfile.in
                       ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
        add_custom_target(docs
            ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation in ${DOXYGEN_OUTPUT_DIR}/html"
            VERBATIM
        )
        set_target_properties(docs PROPERTIES
            ADDITIONAL_CLEAN_FILES "${DOXYGEN_OUTPUT_DIR}/html"
        )
    else()
        message(STATUS "Doxygen not found — 'docs' target disabled.")
    endif()
endif()

# ── Dependencies ──
#
#Guard the call for `uflib` to resolve in-source, so as not to not hard-fail
#on `find_package(uflib)`. It is harmless in every other mode: with a
##system `uflib` the target is absent, so `find_package` runs exactly as before
#
if(NOT TARGET uflib::uflib)
    find_package(uflib REQUIRED)
endif()

# hiredis — the transport behind ufsrvscriptlib's redis binding. A host app
# supplies it; a build of this repo on its own brings its own copy.
if(IS_ROOT_PROJECT AND NOT TARGET hiredis)
    add_subdirectory(packages/hiredis)
endif()

# ufsrvscriptlib — sandboxed Lua engine. In-source (packages/) when present,
# otherwise a system install via find_package (the future /opt model).
if(NOT TARGET ufsrvscriptlib::ufsrvscriptlib)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packages/ufsrvscriptlib/CMakeLists.txt")
        add_subdirectory(packages/ufsrvscriptlib)
    else()
        find_package(ufsrvscriptlib REQUIRED)
    endif()
endif()

include(GNUInstallDirs)
add_library(${_PROJECT_NAME}_packages INTERFACE)
target_link_libraries(${_PROJECT_NAME}_packages INTERFACE
    $<BUILD_INTERFACE:hiredis>
    $<BUILD_INTERFACE:ufsrvscriptlib::ufsrvscriptlib>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/libhiredis.a>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/libufsrvscriptlib.a>
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Lua 5.4 REQUIRED)
find_package(JsonC REQUIRED)
find_package(Jemalloc REQUIRED)
find_package(Systemd REQUIRED)
find_package(OpenSSL REQUIRED)

# ── Build target ──
add_library(${_PROJECT_NAME} OBJECT)

if(IS_ROOT_PROJECT)
    add_library(${_PROJECT_NAME}_static STATIC $<TARGET_OBJECTS:${_PROJECT_NAME}>)
    set_target_properties(${_PROJECT_NAME}_static PROPERTIES OUTPUT_NAME ${_PROJECT_NAME})

    if(BUILD_SHARED_LIBS)
        # Interim switch: until every public symbol carries PUBLIC_API, exporting
        # all globals keeps the shared library usable by consumers. Set OFF to
        # enforce the annotation sweep; a gap then fails at link time.
        option(UFSRVCORELIB_SHARED_EXPORT_ALL
            "Export every global from the shared library (interim; disable once PUBLIC_API coverage is complete)" ON)

        if(UFSRVCORELIB_SHARED_EXPORT_ALL)
            set(_UFSRVCORELIB_SHARED_VISIBILITY default)
        else()
            set(_UFSRVCORELIB_SHARED_VISIBILITY hidden)
        endif()

        # Sources compile once, in the OBJECT target, so visibility must be set
        # there; a preset on the shared target would be inert (it compiles nothing).
        set_target_properties(${_PROJECT_NAME} PROPERTIES
            C_VISIBILITY_PRESET ${_UFSRVCORELIB_SHARED_VISIBILITY}
            VISIBILITY_INLINES_HIDDEN ON)

        add_library(${_PROJECT_NAME}_shared SHARED $<TARGET_OBJECTS:${_PROJECT_NAME}>)
        set_target_properties(${_PROJECT_NAME}_shared PROPERTIES
            OUTPUT_NAME ${_PROJECT_NAME}
            VERSION   ${${_PROJECT_NAME_X}_VERSION}
            SOVERSION ${${_PROJECT_NAME_X}_MAJOR})
        add_library(${_PROJECT_NAME}::${_PROJECT_NAME} ALIAS ${_PROJECT_NAME}_shared)
    else()
        add_library(${_PROJECT_NAME}::${_PROJECT_NAME} ALIAS ${_PROJECT_NAME}_static)
    endif()

    # config_ufsrvcorelib.h is part of the public interface: public headers such
    # as ufsrv_secret_key_type.h guard its inclusion on this define, so every
    # consumer of the installed package must inherit it.
    target_compile_definitions(${_PROJECT_NAME}_static INTERFACE HAVE_CONFIG_UFSRVCORELIB_H)
    if(BUILD_SHARED_LIBS)
        target_compile_definitions(${_PROJECT_NAME}_shared INTERFACE HAVE_CONFIG_UFSRVCORELIB_H)
    endif()
else()
    add_library(${_PROJECT_NAME}::${_PROJECT_NAME} ALIAS ${_PROJECT_NAME})
endif()

target_compile_definitions(${_PROJECT_NAME}_packages INTERFACE HAVE_CONFIG_UFSRVCORELIB_H)

ufsrv_apply_build_options(${_PROJECT_NAME})
ufsrv_build_summary(${_PROJECT_NAME})
if(IS_ROOT_PROJECT)
    ufsrv_apply_build_options(${_PROJECT_NAME}_static)
    ufsrv_build_summary(${_PROJECT_NAME}_static)
    if(BUILD_SHARED_LIBS)
        ufsrv_apply_build_options(${_PROJECT_NAME}_shared)
        ufsrv_build_summary(${_PROJECT_NAME}_shared)
    endif()
endif()

target_include_directories(${_PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib>
        $<INSTALL_INTERFACE:include>
        $<INSTALL_INTERFACE:include/ufsrvcorelib>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ufsrv_core/ufsrv_config
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ufsrv_core/system
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ufsrv_core/configfile_processor
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ufsrv_core/instrumentation
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ufsrv_core/cli_configurator
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/system
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/configfile_processor
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/instrumentation
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/cli_configurator
        ${LUA_INCLUDE_DIR}
        ${JSONC_INCLUDE_DIRS}
        ${JEMALLOC_INCLUDE_DIRS}
        ${SYSTEMD_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/packages/csonpath
        # ── Generated files (key-map consumed by configfile_processor) ───
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/generated
)

# ── Link dependencies ──
# One list per visibility class: the OBJECT target uses both, and so does the
# shared library's own link (see "Link closure" below) — naming the set twice by
# hand is how the two drift apart.
set(_UFSRVCORELIB_PUBLIC_LIBS
    uflib::uflib
    ${_PROJECT_NAME}_packages
)
set(_UFSRVCORELIB_PRIVATE_LIBS
    ${CMAKE_THREAD_LIBS_INIT}
    ${LUA_LIBRARY}
    ${JSONC_LIBRARIES}
    ${JEMALLOC_LIBRARIES}
    ${SYSTEMD_LIBRARIES}
    OpenSSL::Crypto
)

target_link_libraries(${_PROJECT_NAME}
    PUBLIC ${_UFSRVCORELIB_PUBLIC_LIBS}
    PRIVATE ${_UFSRVCORELIB_PRIVATE_LIBS}
)

# ── Link closure for the shared library ─────────────────────────────────────
# libufsrvcorelib.so is built from $<TARGET_OBJECTS:...>, which carries no usage
# requirements: the libraries above reach the static archive through its
# INTERFACE exports, and reached the shared object not at all. It recorded no
# DT_NEEDED entry beyond libc and left every lua, json-c, jemalloc, systemd,
# openssl and uflib call for the consumer to resolve by hand. Naming the same set
# again as PRIVATE closes the object without changing what consumers inherit from
# the installed target.
if(BUILD_SHARED_LIBS AND IS_ROOT_PROJECT)
    target_link_libraries(${_PROJECT_NAME}_shared PRIVATE
        ${_UFSRVCORELIB_PUBLIC_LIBS}
        ${_UFSRVCORELIB_PRIVATE_LIBS}
    )
endif()

# ── Position-independent code ──
set_target_properties(${_PROJECT_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME ${_PROJECT_NAME}
)

# ── Sources (explicit) ──
target_sources(${_PROJECT_NAME} PRIVATE
    # ── ufsrv_core/system ────────────────────────────────────────────────
    src/ufsrv_core/system/utils_cpu.c
    src/ufsrv_core/system/utils_runtime.c
    src/ufsrv_core/system/utils_jemalloc.c

    # ── ufsrv_core/configfile_processor ──────────────────────────────────
    src/ufsrv_core/configfile_processor/configfile_processor.c

    # ── ufsrv_core/instrumentation ───────────────────────────────────────
    src/ufsrv_core/instrumentation/instrumentation_statsd.c

    # ── ufsrv_core/cli_configurator ──────────────────────────────────────
    src/ufsrv_core/cli_configurator/ufsrv_cli_configurator.c

    # ── ufsrv_core/service/file_loader ───────────────────────────────────
    src/ufsrv_core/service/file_loader/service_file_loader.c

    # ── ufsrv_core/ufsrv_config ──────────────────────────────────────
    src/ufsrv_core/ufsrv_config/ufsrv_config_pretty_printers.c
    src/ufsrv_core/ufsrv_config/ufsrv_config_secret.c
    src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_as_string.c
    src/ufsrv_core/ufsrv_config/ufsrv_config_stubs_accessor_defs.c

    # ── ufsrv_console_script ───────────────────────────────────────────────
    src/ufsrv_console_script/ufsrv_console_script.c
    src/ufsrv_console_script/ufsrv_console_script_eval.c
)

# ── Dynamic version ──
add_custom_target(${_PROJECT_NAME}_version
    COMMAND chmod 755 ${CMAKE_CURRENT_SOURCE_DIR}/src/version.c.sh
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/src/version.c.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.c
    COMMENT "Generating version.c with a fresh compilation timestamp"
    VERBATIM
)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/version.c PROPERTIES GENERATED TRUE)
target_sources(${_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.c)
add_dependencies(${_PROJECT_NAME} ${_PROJECT_NAME}_version)

# ── ufsrv_config: self-test composition mode ──────────────────────────────────
# When ON, the library acts as its own host — generator runs against the core
# catalogue, the composer produces a self-contained src/ufsrv_config/ module,
# and those sources are compiled into the library target (N=1 composition).
# When OFF (default), zero ufsrv_config symbols are compiled into the library.

# ── Code generation: ufsrv_config tables ─────────────────────────────────────
# Build-time host tool — reads core catalogue, emits header + source.
# Gated behind UFSRV_CONFIG_MODULE=ON (default OFF — library has zero
# ufsrv_config symbols in natural state).
# See cmake-modules/ufsrv_config_generator.cmake for the generator pipeline
# and cmake-modules/compose_ufsrv_config.cmake for the composition step.

if(UFSRV_CONFIG_MODULE)
    message(STATUS "ufsrv_config: self-test module ON — N=1 composition pipeline active")

    set(UFSRV_CONFIG_GEN_DIR            "${CMAKE_CURRENT_SOURCE_DIR}/dev/scripts/ufsrv_config_generator")
    set(UFSRV_CONFIG_GEN_CATALOGUE      "${UFSRV_CONFIG_GEN_DIR}/ufsrv_config_generator_fields.c.input")
    set(UFSRV_CONFIG_GEN_INCLUDE_PREFIX "ufsrvcorelib")
    set(UFSRV_CONFIG_GEN_PREFIX         "")
    set(UFSRV_CONFIG_GEN_VERIFIER       "${UFSRV_CONFIG_GEN_DIR}/ufsrv_config_verify.py")
    # Generator output → staging/ufsrvcorelib/generated/ (per-library layout)
    set(UFSRV_CONFIG_GEN_STAGING        "${UFSRV_CONFIG_GEN_DIR}/ufsrvcorelib/generated")
    set(UFSRV_CONFIG_GEN_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/configfile_processor
    )
    include(ufsrv_config_generator)

    # Compose generated files + invariant module into src/ufsrv_config/
    # Default: self-test (N=1).  Host servers override via -D or set() before
    # add_subdirectory(), e.g. set(UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES "ufsrvcorelib;ufnetcorelib")
    if(NOT DEFINED UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
        set(UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES "ufsrvcorelib")
    endif()
    set(UFSRV_CONFIG_COMPOSER_ufsrvcorelib_REPO      "LOCAL")
    set(UFSRV_CONFIG_COMPOSER_ufsrvcorelib_REF       "")
    set(UFSRV_CONFIG_COMPOSER_ufsrvcorelib_CATALOGUE "dev/scripts/ufsrv_config_generator/ufsrv_config_generator_fields.c.input")

    set(UFSRV_CONFIG_COMPOSER_STAGING_DIR "${UFSRV_CONFIG_GEN_DIR}")
    set(UFSRV_CONFIG_COMPOSER_OUTPUT_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src/ufsrv_config")
    set(UFSRV_CONFIG_COMPOSER_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}")

    set(UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO "LOCAL")
    set(UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF  "")
    include(compose_ufsrv_config)
    compute_composed_sources()  # exports UFSRV_CONFIG_COMPOSED_SOURCES at configure time

    # Run composer at build time (depends on generator output).
    # Passes all per-library and toolchain variables so the composer can
    # fetch assets from git (or LOCAL copy) and assemble the composed module.
    set(_COMPOSER_CMAKE_ARGS
        "-DUFSRV_CONFIG_COMPOSER_LIBRARY_NAMES=${UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES}"
        "-DUFSRV_CONFIG_COMPOSER_ufsrvcorelib_REPO=${UFSRV_CONFIG_COMPOSER_ufsrvcorelib_REPO}"
        "-DUFSRV_CONFIG_COMPOSER_ufsrvcorelib_REF=${UFSRV_CONFIG_COMPOSER_ufsrvcorelib_REF}"
        "-DUFSRV_CONFIG_COMPOSER_ufsrvcorelib_CATALOGUE=${UFSRV_CONFIG_COMPOSER_ufsrvcorelib_CATALOGUE}"
        "-DUFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO=${UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO}"
        "-DUFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF=${UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF}"
        "-DUFSRV_CONFIG_COMPOSER_STAGING_DIR=${UFSRV_CONFIG_COMPOSER_STAGING_DIR}"
        "-DUFSRV_CONFIG_COMPOSER_OUTPUT_DIR=${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}"
        "-DUFSRV_CONFIG_COMPOSER_SOURCE_DIR=${UFSRV_CONFIG_COMPOSER_SOURCE_DIR}"
    )
    add_custom_command(
        OUTPUT ${UFSRV_CONFIG_COMPOSED_ALL_OUTPUTS}
        COMMAND ${CMAKE_COMMAND} ${_COMPOSER_CMAKE_ARGS}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules/compose_ufsrv_config_run.cmake"
        DEPENDS
            ufsrv_config_generate
        COMMENT "compose_ufsrv_config: assembling src/ufsrv_config/ (N=1)"
        VERBATIM
    )

    # Library target includes composed sources
    target_sources(${_PROJECT_NAME} PRIVATE
        ${UFSRV_CONFIG_COMPOSED_SOURCES}
    )

    # Composed include dir for the module's own headers
    target_include_directories(${_PROJECT_NAME} PRIVATE
        ${UFSRV_CONFIG_COMPOSED_INCLUDE_DIR}
    )

    # Clean target — removes generated staging output + invariant copies
    # deposited by the composer + composed output.  Does NOT remove:
    #   - shipped toolchain assets (generator source, verifier, catalogues,
    #     VERSION) — tracked files
    #   - configure-time pipeline files in staging (e.g. _clean_orphans.sh
    #     written by file(WRITE) at configure time; the install target
    #     depends on them)
    # Only removes generated source/header files matching the generator's
    # own purge pattern (ufsrv_config_* and data_ufsrv_config_*), not the
    # entire ufsrvcorelib/generated/ directory.
    # Matches ufnetcorelib + ufsrvwebsock patterns.
    set(_staging_toolchain_debris
        ufsrv_config.c
        ufsrv_config.h
        ufsrv_config_generic_callbacks.c
        ufsrv_config_generic_callbacks_priv.h
    )
    set(_clean_cmds
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "${UFSRV_CONFIG_GEN_STAGING}/ufsrv_config_*"
            "${UFSRV_CONFIG_GEN_STAGING}/data_ufsrv_config_*"
    )
    foreach(_f IN LISTS _staging_toolchain_debris)
        list(APPEND _clean_cmds
            COMMAND ${CMAKE_COMMAND} -E rm -f "${UFSRV_CONFIG_GEN_DIR}/${_f}"
        )
    endforeach()
    list(APPEND _clean_cmds
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}"
    )
    add_custom_target(ufsrv_config_module_clean
        ${_clean_cmds}
        COMMENT "ufsrv_config: removing generated staging output + composed output (keeps shipped assets + pipeline files + installed generated files)"
        VERBATIM
    )

else()
    message(STATUS "ufsrv_config: module OFF — zero ufsrv_config symbols in library")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
pkg_check_modules(JEMALLOC REQUIRED IMPORTED_TARGET jemalloc)

if(_PACKAGE_INSTALL)
    include(GNUInstallDirs)

    set(_UFSRVCORELIB_EXPORT_LIBS
        ${_PROJECT_NAME}_packages
        uflib::uflib
        ${CMAKE_THREAD_LIBS_INIT}
        PkgConfig::LUA
        PkgConfig::JSONC
        PkgConfig::JEMALLOC
        ${SYSTEMD_LIBRARIES}
        OpenSSL::Crypto)
    target_link_libraries(${_PROJECT_NAME}_static INTERFACE ${_UFSRVCORELIB_EXPORT_LIBS})
    if(BUILD_SHARED_LIBS)
        target_link_libraries(${_PROJECT_NAME}_shared INTERFACE ${_UFSRVCORELIB_EXPORT_LIBS})
    endif()

    install(TARGETS hiredis ufsrvscriptlib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(TARGETS ${_PROJECT_NAME}_packages
        EXPORT ${_PROJECT_NAME}-targets
    )

    install(TARGETS ${_PROJECT_NAME}_static
        EXPORT ${_PROJECT_NAME}-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} ${CMAKE_INSTALL_INCLUDEDIR}/${_PROJECT_NAME}
    )

    if(BUILD_SHARED_LIBS)
        install(TARGETS ${_PROJECT_NAME}_shared
            EXPORT ${_PROJECT_NAME}-targets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} ${CMAKE_INSTALL_INCLUDEDIR}/${_PROJECT_NAME}
        )
    endif()

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # ── Host orchestration modules ──────────────────────────────────────────
    # Single source of truth for every consumer: leaf servers resolve these via
    # ufsrvcorelib_HOST_MODULES_DIR (package config) instead of vendoring copies.
    install(DIRECTORY cmake-modules/ufsrv_config/host/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${_PROJECT_NAME}/cmake-modules/ufsrv_config/host
        FILES_MATCHING PATTERN "*.cmake" PATTERN "*.py"
        PATTERN "__pycache__" EXCLUDE
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/config_ufsrvcorelib.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${_PROJECT_NAME}
    )

    # pkg-config
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/${_PROJECT_NAME}.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME}.pc
        @ONLY
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME}.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )

    # cmake package config
    include(CMakePackageConfigHelpers)
    set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${_PROJECT_NAME})

    install(EXPORT ${_PROJECT_NAME}-targets
        FILE ${_PROJECT_NAME}Targets.cmake
        NAMESPACE ${_PROJECT_NAME}::
        DESTINATION ${INSTALL_CONFIGDIR}
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/${_PROJECT_NAME}Config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME}Config.cmake
        INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME}ConfigVersion.cmake
        VERSION ${${_PROJECT_NAME_X}_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME}Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME}ConfigVersion.cmake
        DESTINATION ${INSTALL_CONFIGDIR}
    )

    # ── IDE source manifest ──────────────────────────────────────────────────
    # Publish the translation units this library actually compiles, together with the
    # checkout they were configured from, so a consumer can index them. (helps with IDE loading function definitions)
    #
    # Paths are absolute and machine-specific by design: the manifest records
    # where the sources were when the package was built. A consumer must
    # tolerate entries that do not exist on its machine and fall back to
    # whatever it can resolve itself (other checkout location, another host).
    get_target_property(_ufsrv_ide_sources ${_PROJECT_NAME} SOURCES)
    set(_ufsrv_ide_manifest
        "set(${_PROJECT_NAME_X}_IDE_SOURCE_DIR \"${CMAKE_CURRENT_SOURCE_DIR}\")\nset(${_PROJECT_NAME_X}_IDE_SOURCES\n")
    foreach(_ufsrv_ide_src IN LISTS _ufsrv_ide_sources)
        if(NOT IS_ABSOLUTE "${_ufsrv_ide_src}")
            set(_ufsrv_ide_src "${CMAKE_CURRENT_SOURCE_DIR}/${_ufsrv_ide_src}")
        endif()
        # Generated sources live in the build tree, not the source checkout:
        # there is no file for a consumer to navigate to.
        string(FIND "${_ufsrv_ide_src}" "${CMAKE_CURRENT_BINARY_DIR}/" _ufsrv_ide_gen)
        if(_ufsrv_ide_gen EQUAL 0)
            continue()
        endif()
        string(APPEND _ufsrv_ide_manifest "    \"${_ufsrv_ide_src}\"\n")
    endforeach()
    string(APPEND _ufsrv_ide_manifest ")\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME}-ide-sources.cmake"
        "${_ufsrv_ide_manifest}")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${_PROJECT_NAME}-ide-sources.cmake
        DESTINATION ${INSTALL_CONFIGDIR}
    )

    # archive-uninstall: move currently installed files to /tmp before installing new version
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/archive_uninstall.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/archive_uninstall.cmake
        @ONLY
    )
    add_custom_target(archive-uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/archive_uninstall.cmake
        COMMENT "Archiving currently installed ${_PROJECT_NAME} to /tmp/archived_${_PROJECT_NAME}_${${_PROJECT_NAME_X}_VERSION}"
        VERBATIM
    )

    # Uninstall target
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
        @ONLY
    )
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )

    add_custom_target(install_verification
        COMMAND ${CMAKE_COMMAND}
            -DINSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/install_verification.cmake
        COMMENT "Verifying ${_PROJECT_NAME} version across system install, git, and local copy"
        VERBATIM
    )

    # CPack
    set(CPACK_PACKAGE_NAME "${_PROJECT_NAME}")
    set(CPACK_PACKAGE_VERSION "${${_PROJECT_NAME_X}_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Core library for ufsrv platform (CPU affinity, thread management)")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "unfacd works")
    set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    include(CPack)
endif()

if(_PACKAGE_TESTS)
    add_subdirectory(tests)
endif()
