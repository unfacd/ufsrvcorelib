#
# cmake-modules/ufsrv_config/host/ufsrv_config_bootstrap_host.cmake
#
# Register the ufsrv_config_bootstrap_host target — STRUCTURAL ONLY.
# Creates the deep-tree directory structure and .gitkeep anchors.  It copies NO
# content (invariants + shim are copied by the build-time composition step),
# fetches nothing, and performs no git side effects.
#
# Idempotent — mkdir/touch are no-ops when the structure already exists.
# A direct, independent user command; not chained into configure or build.
#

set(_DPR "${CMAKE_SOURCE_DIR}/src/ufsrv_core/ufsrv_config")
set(_DPU "${CMAKE_SOURCE_DIR}/src/include/ufsrvcorelib/ufsrv_core/ufsrv_config")

add_custom_target(ufsrv_config_bootstrap_host
    # Deep-tree directory structure
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_DPR}/generated"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_DPU}/generated"
    # .gitkeep anchors — keep otherwise-empty dirs trackable (currently disabled)
    #COMMAND ${CMAKE_COMMAND} -E touch "${_DPR}/generated/.gitkeep"
    #COMMAND ${CMAKE_COMMAND} -E touch "${_DPU}/.gitkeep"
    COMMENT "ufsrv_config: bootstrap deep-tree structure (dirs NO .gitkeep anchors)"
    VERBATIM)
