# packages/hiredis — vendored hiredis 0.14.0 (BSD-3-Clause).
#
# Redis C client, copied verbatim from ufsrvcli/packages/hiredis/ so ufsrvcorelib
# can link synchronous Redis support without pulling the whole ufnetcorelib
# subtree. See README.md for provenance and update instructions.
#
# Only the vendored sources are built here; the header include dir is PUBLIC so
# consumers can `#include <hiredis.h>`.

add_library(hiredis STATIC
    async.c
    dict.c
    hiredis.c
    net.c
    read.c
    sds.c
    sockcompat.c
    sslio.c
)

target_include_directories(hiredis
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# hiredis is linked into a shared/static lib and eventually an executable.
set_property(TARGET hiredis PROPERTY POSITION_INDEPENDENT_CODE ON)

# Instrument hiredis with the repo-wide build options (clang flags, sanitizers)
# so it is sanitized consistently with the rest of the build.
if(COMMAND ufsrv_apply_build_options)
    ufsrv_apply_build_options(hiredis)
endif()

# hiredis's fmacros.h redefines _XOPEN_SOURCE/_POSIX_C_SOURCE itself, which
# collides with the repo-wide -D_GNU_SOURCE; silence the benign -Wmacro-redefined
# warning for this vendored third-party target only.
target_compile_options(hiredis PRIVATE -Wno-macro-redefined)
