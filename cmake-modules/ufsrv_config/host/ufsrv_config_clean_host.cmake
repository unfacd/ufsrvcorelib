#
# cmake-modules/ufsrv_config/host/ufsrv_config_clean_host.cmake
#
# Two-set clean for the ufsrv_config composition subsystem.
#
#   Set #1 — COMPOSITION_INPUT_ASSETS (untracked): everything under
#     dev/scripts/ufsrv_config_generator/ EXCEPT the tracked .gitkeep anchor,
#     plus the build-time clones, scratch dir, and generator binaries.
#     → target: ufsrv_config_clean_host
#
#   Set #2 — GENERATED + INVARIANT assets copied in-source (tracked): the deep
#     trees src/ufsrv_core/ and src/include/ufsrvcorelib/.
#     → target: ufsrv_config_clean_tracked  (git rm -r -f)
#
# Always available, not gated behind UFSRV_HOST_COMPOSE_CONFIG.
#

find_program(GIT_EXECUTABLE git REQUIRED)

set(_STG "${CMAKE_SOURCE_DIR}/dev/scripts/ufsrv_config_generator")

# ── Set #1: COMPOSITION_INPUT_ASSETS (untracked) ──────────────────────────────
set(_STAGING_TOOLS
    "${_STG}/ufsrv_config_generator.c"
    "${_STG}/ufsrv_config_verify.py"
    "${_STG}/ufsrv_config.c"
    "${_STG}/ufsrv_config.h"
    "${_STG}/ufsrv_config_generic_callbacks.c"
    "${_STG}/ufsrv_config_generic_callbacks_priv.h")

set(_input_cmds
    # everything under ufsrv_config_generator/ except the tracked .gitkeep anchor
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_STAGING_TOOLS}
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_STG}/ufsrvcorelib"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_STG}/ufnetcorelib"
    # build-time clones + scratch + generator binaries
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/ufsrv_config_staging"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/ufsrv_config_composed"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/host")

add_custom_target(ufsrv_config_clean_host
    ${_input_cmds}
    COMMENT "ufsrv_config: remove composition input assets (staging + clones + scratch + binaries)"
    VERBATIM)

# ── Set #2: tracked in-source assets (git rm) ────────────────────────────────
add_custom_target(ufsrv_config_clean_tracked
        # tracked assets — git-correct path (stages the deletion)
        COMMAND ${GIT_EXECUTABLE} -C "${CMAKE_SOURCE_DIR}" rm -r -f --ignore-unmatch
        "src/ufsrv_core"
        "src/include/ufsrvcorelib"
        # untracked leftovers + now-empty dirs — filesystem path
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_SOURCE_DIR}/src/ufsrv_core"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_SOURCE_DIR}/src/include/ufsrvcorelib"
        COMMENT "ufsrv_config: remove tracked + residual in-source assets"
        VERBATIM)
