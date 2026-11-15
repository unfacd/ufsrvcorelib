# FindJsonC.cmake — locate json-c.
#
# Result variables:
#
#   JsonC_FOUND        TRUE when both the header and the library were found
#   JSONC_INCLUDE_DIRS include directories, for target_include_directories
#   JSONC_LIBRARIES    the library, for target_link_libraries
#   JSONC_VERSION      read from the header's JSON_C_VERSION
#
# /opt is offered as a hint when it carries a copy, so a build node provisioned
# with the fleet payloads resolves without the caller naming it; a distro
# package resolves through the default search.  Nothing is searched
# NO_DEFAULT_PATH, so neither is a silent fallback for the other.

set(_JsonC_include_hints)
if(EXISTS /opt/include/json-c/json.h)
    list(APPEND _JsonC_include_hints /opt/include)
endif()
if(JSONC_ROOT)
    list(APPEND _JsonC_include_hints "${JSONC_ROOT}/include")
endif()

find_path(JSONC_INCLUDE_DIR
    NAMES json-c/json.h
    HINTS ${_JsonC_include_hints}
)

set(_JsonC_library_hints)
if(EXISTS /opt/lib/libjson-c.so OR EXISTS /opt/lib/libjson-c.a)
    list(APPEND _JsonC_library_hints /opt/lib)
endif()
if(JSONC_ROOT)
    list(APPEND _JsonC_library_hints "${JSONC_ROOT}/lib")
endif()

find_library(JSONC_LIBRARY
    NAMES json-c
    HINTS ${_JsonC_library_hints}
)

if(JSONC_INCLUDE_DIR AND EXISTS "${JSONC_INCLUDE_DIR}/json-c/json_c_version.h")
    file(STRINGS "${JSONC_INCLUDE_DIR}/json-c/json_c_version.h" _JsonC_version_line
         REGEX "^#define[ \t]+JSON_C_VERSION[ \t]+\"[0-9.]+\"")
    string(REGEX MATCH "[0-9]+\\.[0-9]+" JSONC_VERSION "${_JsonC_version_line}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JsonC
    REQUIRED_VARS JSONC_LIBRARY JSONC_INCLUDE_DIR
    VERSION_VAR JSONC_VERSION
)

if(JsonC_FOUND)
    set(JSONC_LIBRARIES "${JSONC_LIBRARY}")
    set(JSONC_INCLUDE_DIRS "${JSONC_INCLUDE_DIR}")
endif()

mark_as_advanced(JSONC_ROOT JSONC_INCLUDE_DIR JSONC_LIBRARY)
