# FindSystemd.cmake — locate libsystemd (the sd-login calls).
#
# Result variables:
#
#   Systemd_FOUND        TRUE when both the header and the library were found
#   SYSTEMD_INCLUDE_DIRS include directories, for target_include_directories
#   SYSTEMD_LIBRARIES    the library, for target_link_libraries
#
# The sd-login header carries no version macro, so none is reported.
#
# /opt is offered as a hint when it carries a copy, so a build node provisioned
# with the fleet payloads resolves without the caller naming it; a distro
# package resolves through the default search.  Nothing is searched
# NO_DEFAULT_PATH, so neither is a silent fallback for the other.

set(_Systemd_include_hints)
if(EXISTS /opt/include/systemd/sd-login.h)
    list(APPEND _Systemd_include_hints /opt/include)
endif()
if(SYSTEMD_ROOT)
    list(APPEND _Systemd_include_hints "${SYSTEMD_ROOT}/include")
endif()

find_path(SYSTEMD_INCLUDE_DIR
    NAMES systemd/sd-login.h
    HINTS ${_Systemd_include_hints}
)

set(_Systemd_library_hints)
if(EXISTS /opt/lib/libsystemd.so OR EXISTS /opt/lib/libsystemd.a)
    list(APPEND _Systemd_library_hints /opt/lib)
endif()
if(SYSTEMD_ROOT)
    list(APPEND _Systemd_library_hints "${SYSTEMD_ROOT}/lib")
endif()

find_library(SYSTEMD_LIBRARY
    NAMES systemd
    HINTS ${_Systemd_library_hints}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Systemd
    REQUIRED_VARS SYSTEMD_LIBRARY SYSTEMD_INCLUDE_DIR
)

if(Systemd_FOUND)
    set(SYSTEMD_LIBRARIES "${SYSTEMD_LIBRARY}")
    set(SYSTEMD_INCLUDE_DIRS "${SYSTEMD_INCLUDE_DIR}")
endif()

mark_as_advanced(SYSTEMD_ROOT SYSTEMD_INCLUDE_DIR SYSTEMD_LIBRARY)
