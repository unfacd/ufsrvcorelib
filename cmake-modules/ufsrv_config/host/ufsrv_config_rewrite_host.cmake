#
# cmake-modules/ufsrv_config/host/ufsrv_config_rewrite_host.cmake
#
# Rewrites generated-asset includes in installed generated .c files so they
# resolve to the host's LOCAL deep tree, not the ufsrvcorelib subtree or the
# flat composed layout.  Invoked after the composer runs, one file at a time
# via cmake -DFILE=<path> -P.
#
# Input formats handled (generator original + join-script flat):
#   <ufsrvcorelib/.../generated/*.c>  →  "*.c"           (Rule 1)
#   <ufsrvcorelib/.../generated/*.h>  →  LOCAL deep      (Rule 2)
#   "generated/*.h"                   →  LOCAL deep      (Rule 3)
#   "generated/*.c"                   →  "*.c"           (Rule 4)
#
# Canonical type headers (no "generated/" in path) pass through untouched.
#

cmake_minimum_required(VERSION 3.20)

if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "ufsrv_config_rewrite: FILE='${FILE}' not found")
endif()

file(READ "${FILE}" _content)

# Rule 1: angle-bracket generated .c → sibling quoted
string(REGEX REPLACE
    "#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/([^>]+\\.c)>"
    "#include \"\\1\""
    _content "${_content}")

# Rule 2: angle-bracket generated .h → LOCAL deep quoted
string(REGEX REPLACE
    "#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/([^>]+\\.h)>"
    "#include \"ufsrv_core/ufsrv_config/generated/\\1\""
    _content "${_content}")

# Rule 3: flat quoted generated .h (join-script or N=1 master include) → LOCAL deep
string(REGEX REPLACE
    "#include \"generated/([^\"]+\\.h)\""
    "#include \"ufsrv_core/ufsrv_config/generated/\\1\""
    _content "${_content}")

# Rule 4: flat quoted generated .c (join-script or N=1 master include) → sibling
string(REGEX REPLACE
    "#include \"generated/([^\"]+\\.c)\""
    "#include \"\\1\""
    _content "${_content}")

file(WRITE "${FILE}" "${_content}")
message(STATUS "ufsrv_config_rewrite: ${FILE} — generated includes → LOCAL")
