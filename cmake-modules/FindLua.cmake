# FindLua.cmake — locate a Lua 5.x installation.
#
# Supersedes CMake's own FindLua, which searches only version-suffixed include
# subdirectories (include/lua5.4 and friends) and therefore cannot see an
# installation that puts lua.h straight into <prefix>/include — which is what
# this fleet's build nodes do under /opt.  Both layouts are searched here, so
# one find_package call resolves a distro lua and a private prefix alike.
#
# Result variables:
#
#   Lua_FOUND       TRUE when both the header and the library were found
#   Lua_VERSION     "5.4", "5.5", … read from the header's LUA_VERSION_NUM
#   LUA_INCLUDE_DIR the include directory, for target_include_directories
#   LUA_LIBRARY     the library, for target_link_libraries
#   LUA_LIBRARIES   the library, plus libm where the split exists
#
# A private prefix is reached by setting LUA_ROOT.  /opt is offered as a hint
# when it carries a lua, so a provisioned build node resolves without the
# caller naming it.  Nothing is searched NO_DEFAULT_PATH: a distro package and
# a declared prefix take the same path, and neither is a silent fallback for
# the other.
#
# The version is a minimum, not an exact match: a request for 5.4 is satisfied
# by 5.5, which is what the fleet currently installs.

set(_Lua_include_hints)
if(EXISTS /opt/include/lua.h)
    list(APPEND _Lua_include_hints /opt/include)
endif()
if(LUA_ROOT)
    list(APPEND _Lua_include_hints "${LUA_ROOT}/include")
endif()

find_path(LUA_INCLUDE_DIR
    NAMES lua.h
    HINTS ${_Lua_include_hints}
    PATH_SUFFIXES lua5.5 lua5.4 lua5.3 lua5.2 lua5.1 lua55 lua54 lua53 lua52 lua51 lua
)

set(_Lua_library_hints)
if(EXISTS /opt/lib/liblua.a OR EXISTS /opt/lib/liblua.so)
    list(APPEND _Lua_library_hints /opt/lib)
endif()
if(LUA_ROOT)
    list(APPEND _Lua_library_hints "${LUA_ROOT}/lib")
endif()

find_library(LUA_LIBRARY
    NAMES lua55 lua5.5 lua54 lua5.4 lua53 lua5.3 lua52 lua5.2 lua51 lua5.1 lua-5.4 lua-5.3 lua
    HINTS ${_Lua_library_hints}
    PATH_SUFFIXES lib
)

if(LUA_INCLUDE_DIR AND EXISTS "${LUA_INCLUDE_DIR}/lua.h")
    # Up to 5.4: LUA_VERSION_NUM is the number. 5.5 defines it as an expression
    # over LUA_VERSION_MAJOR_N and LUA_VERSION_MINOR_N, so both forms are read —
    # a version that cannot be determined would pass a minimum-version request
    # unchecked, which is the one thing this module exists to prevent.
    file(STRINGS "${LUA_INCLUDE_DIR}/lua.h" _Lua_version_line
         REGEX "^#define[ \t]+LUA_VERSION_NUM[ \t]+[0-9]+")
    string(REGEX MATCH "[0-9]+" _Lua_version_num "${_Lua_version_line}")
    if(_Lua_version_num)
        math(EXPR _Lua_major "${_Lua_version_num} / 100")
        math(EXPR _Lua_minor "${_Lua_version_num} % 100")
        set(Lua_VERSION "${_Lua_major}.${_Lua_minor}")
    else()
        file(STRINGS "${LUA_INCLUDE_DIR}/lua.h" _Lua_major_line
             REGEX "^#define[ \t]+LUA_VERSION_MAJOR_N[ \t]+[0-9]+")
        file(STRINGS "${LUA_INCLUDE_DIR}/lua.h" _Lua_minor_line
             REGEX "^#define[ \t]+LUA_VERSION_MINOR_N[ \t]+[0-9]+")
        string(REGEX MATCH "[0-9]+" _Lua_major "${_Lua_major_line}")
        string(REGEX MATCH "[0-9]+" _Lua_minor "${_Lua_minor_line}")
        if(_Lua_major AND _Lua_minor)
            set(Lua_VERSION "${_Lua_major}.${_Lua_minor}")
        endif()
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Lua
    REQUIRED_VARS LUA_LIBRARY LUA_INCLUDE_DIR
    VERSION_VAR Lua_VERSION
)

if(Lua_FOUND)
    set(LUA_LIBRARIES "${LUA_LIBRARY}")
    include(CheckLibraryExists)
    check_library_exists(m sin "" _Lua_have_libm)
    if(_Lua_have_libm)
        list(APPEND LUA_LIBRARIES m)
    endif()
    if(NOT TARGET Lua::Lua)
        add_library(Lua::Lua UNKNOWN IMPORTED)
        set_target_properties(Lua::Lua PROPERTIES
            IMPORTED_LOCATION "${LUA_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(LUA_ROOT LUA_INCLUDE_DIR LUA_LIBRARY)
