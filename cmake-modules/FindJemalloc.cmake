# FindJemalloc.cmake — locate the jemalloc allocator.
#
# Result variables:
#
#   Jemalloc_FOUND        TRUE when both the header and the library were found
#   JEMALLOC_INCLUDE_DIRS include directories, for target_include_directories
#   JEMALLOC_LIBRARIES    the library, for target_link_libraries
#   JEMALLOC_VERSION      read from the header's JEMALLOC_VERSION
#
# /opt is offered as a hint when it carries a copy, so a build node provisioned
# with the fleet payloads resolves without the caller naming it; a distro
# package resolves through the default search.  Nothing is searched
# NO_DEFAULT_PATH, so neither is a silent fallback for the other.

set(_Jemalloc_include_hints)
if(EXISTS /opt/include/jemalloc/jemalloc.h)
    list(APPEND _Jemalloc_include_hints /opt/include)
endif()
if(JEMALLOC_ROOT)
    list(APPEND _Jemalloc_include_hints "${JEMALLOC_ROOT}/include")
endif()

find_path(JEMALLOC_INCLUDE_DIR
    NAMES jemalloc/jemalloc.h
    HINTS ${_Jemalloc_include_hints}
)

set(_Jemalloc_library_hints)
if(EXISTS /opt/lib/libjemalloc.so OR EXISTS /opt/lib/libjemalloc.a)
    list(APPEND _Jemalloc_library_hints /opt/lib)
endif()
if(JEMALLOC_ROOT)
    list(APPEND _Jemalloc_library_hints "${JEMALLOC_ROOT}/lib")
endif()

find_library(JEMALLOC_LIBRARY
    NAMES jemalloc
    HINTS ${_Jemalloc_library_hints}
)

if(JEMALLOC_INCLUDE_DIR AND EXISTS "${JEMALLOC_INCLUDE_DIR}/jemalloc/jemalloc.h")
    file(STRINGS "${JEMALLOC_INCLUDE_DIR}/jemalloc/jemalloc.h" _Jemalloc_version_line
         REGEX "^#define[ \t]+JEMALLOC_VERSION[ \t]+\"")
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" JEMALLOC_VERSION "${_Jemalloc_version_line}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Jemalloc
    REQUIRED_VARS JEMALLOC_LIBRARY JEMALLOC_INCLUDE_DIR
    VERSION_VAR JEMALLOC_VERSION
)

if(Jemalloc_FOUND)
    set(JEMALLOC_LIBRARIES "${JEMALLOC_LIBRARY}")
    set(JEMALLOC_INCLUDE_DIRS "${JEMALLOC_INCLUDE_DIR}")
endif()

mark_as_advanced(JEMALLOC_ROOT JEMALLOC_INCLUDE_DIR JEMALLOC_LIBRARY)
