#
# cmake-modules/compose_ufsrv_config.cmake
#
# ufsrv_config composition pipeline — reusable across ufsrvcorelib (self-test,
# N=1) and host servers (multi-library, N>1).
#
# Core design principle: assets are fetched from git repos at configure time.
# The pipeline does NOT assume files exist on the local filesystem.
#
# Contract — set these BEFORE include():
#
#   UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES
#       Semicolon-delimited list of library names in composition order.
#       Example: "ufsrvcorelib;ufnetcorelib"
#
#   For each <name> in UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES:
#     UFSRV_CONFIG_COMPOSER_<name>_REPO
#         Git repo URL.  "LOCAL" as a special value means use the source
#         tree at UFSRV_CONFIG_COMPOSER_SOURCE_DIR directly (dev convenience;
#         the pipeline still copies files into staging for a uniform view).
#     UFSRV_CONFIG_COMPOSER_<name>_REF
#         Git ref (branch / tag / commit).  Ignored when REPO is "LOCAL".
#     UFSRV_CONFIG_COMPOSER_<name>_CATALOGUE
#         Repo-relative path to the cold catalogue .c.input file.
#         Example: "dev/scripts/ufsrv_config_generator/ufsrv_config_generator_fields.c.input"
#
#   UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO
#       Git repo URL for ufsrvcorelib (toolchain: generator, verifier, composer,
#       ufsrv_config module files).  "LOCAL" for self-test.
#   UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF
#       Git ref for toolchain checkout.  Ignored when TOOLCHAIN_REPO is "LOCAL".
#
#   UFSRV_CONFIG_COMPOSER_STAGING_DIR
#       Throw-away staging root.  All git checkouts land here.
#       Example: ${CMAKE_SOURCE_DIR}/dev/scripts/ufsrv_config_generator
#
#   UFSRV_CONFIG_COMPOSER_OUTPUT_DIR
#       Where to emit the composed src/ufsrv_config/ module.
#       Example: ${CMAKE_SOURCE_DIR}/src/ufsrv_config
#
#   UFSRV_CONFIG_COMPOSER_SOURCE_DIR
#       Used only when REPO is "LOCAL" — the repo source tree root.
#       Example: ${CMAKE_CURRENT_SOURCE_DIR}
#
# Exports (set in PARENT_SCOPE by compute_composed_sources()):
#   UFSRV_CONFIG_COMPOSED_SOURCES     — .c files to compile into the target
#   UFSRV_CONFIG_COMPOSED_INCLUDE_DIR — include directory for composed headers
#   UFSRV_CONFIG_COMPOSED_ALL_OUTPUTS — all output files (for add_custom_command)
#

include_guard(GLOBAL)

# ═══════════════════════════════════════════════════════════════════════════════
# Sanity checks
# ═══════════════════════════════════════════════════════════════════════════════

foreach(_v IN ITEMS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES
                  UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO
                  UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF
                  UFSRV_CONFIG_COMPOSER_STAGING_DIR
                  UFSRV_CONFIG_COMPOSER_OUTPUT_DIR
                  UFSRV_CONFIG_COMPOSER_SOURCE_DIR)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "compose_ufsrv_config: ${_v} must be set before include()")
    endif()
endforeach()


list(LENGTH UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES _COMPOSER_NUM_LIBS)
if(_COMPOSER_NUM_LIBS EQUAL 0)
    message(FATAL_ERROR "compose_ufsrv_config: UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES is empty")
endif()

# Validate per-library vars
foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
    foreach(_suffix IN ITEMS REPO REF CATALOGUE)
        if(NOT DEFINED UFSRV_CONFIG_COMPOSER_${_lib_name}_${_suffix})
            message(FATAL_ERROR "compose_ufsrv_config: UFSRV_CONFIG_COMPOSER_${_lib_name}_${_suffix} not set")
        endif()
    endforeach()
endforeach()

message(STATUS "compose_ufsrv_config: ${_COMPOSER_NUM_LIBS} librar${_COMP_LIB_PLURAL} → ${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")

# ═══════════════════════════════════════════════════════════════════════════════
# Helper — checkout a single file from a git repo into a destination directory
# ═══════════════════════════════════════════════════════════════════════════════

function(_composer_checkout_file _repo _ref _repo_path _dest_dir _label)
    if(_repo STREQUAL "LOCAL")
        set(_src "${UFSRV_CONFIG_COMPOSER_SOURCE_DIR}/${_repo_path}")
        if(NOT EXISTS "${_src}")
            message(FATAL_ERROR "compose_ufsrv_config: LOCAL file not found: ${_src} (${_label})")
        endif()
        get_filename_component(_fname "${_repo_path}" NAME)
        file(COPY "${_src}" DESTINATION "${_dest_dir}")
        message(STATUS "compose_ufsrv_config:   [local] ${_label}")
    else()
        message(STATUS "compose_ufsrv_config:   [git] ${_label} ← ${_repo}@${_ref}")
        # Try git archive first (single connection, no clone needed)
        execute_process(
            COMMAND git archive --remote="${_repo}" "${_ref}" -- "${_repo_path}"
            OUTPUT_FILE "${_dest_dir}/_checkout.tar"
            RESULT_VARIABLE _arc_res
            ERROR_QUIET
            TIMEOUT 60
        )
        if(_arc_res EQUAL 0)
            # git archive preserves the repo_path hierarchy; strip leading
            # components so the file lands directly in _dest_dir
            string(REGEX REPLACE "[^/]+" "" _strip "${_repo_path}")
            string(LENGTH "${_strip}" _strip_len)
            if(_strip_len GREATER 0)
                math(EXPR _strip_count "${_strip_len} - 1")
                execute_process(
                    COMMAND tar -xf "${_dest_dir}/_checkout.tar" -C "${_dest_dir}"
                        --strip-components=${_strip_count}
                    RESULT_VARIABLE _tar_res
                )
            else()
                execute_process(
                    COMMAND tar -xf "${_dest_dir}/_checkout.tar" -C "${_dest_dir}"
                    RESULT_VARIABLE _tar_res
                )
            endif()
            file(REMOVE "${_dest_dir}/_checkout.tar")
            if(NOT _tar_res EQUAL 0)
                message(FATAL_ERROR "compose_ufsrv_config: tar extract failed for ${_label}")
            endif()
            message(STATUS "compose_ufsrv_config:     [archive] OK")
        else()
            # Fallback: shallow bare clone + git show
            message(STATUS "compose_ufsrv_config:     archive unavailable, trying shallow clone...")
            string(RANDOM LENGTH 8 _rand)
            set(_tmp_clone "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/_tmp_clone_${_rand}")
            file(REMOVE_RECURSE "${_tmp_clone}")
            execute_process(
                COMMAND git clone --depth=1 --bare --branch="${_ref}" "${_repo}" "${_tmp_clone}"
                RESULT_VARIABLE _clone_res
                TIMEOUT 120
            )
            if(NOT _clone_res EQUAL 0)
                file(REMOVE_RECURSE "${_tmp_clone}")
                message(FATAL_ERROR "compose_ufsrv_config: clone failed for ${_repo} (${_label})")
            endif()
            get_filename_component(_fname "${_repo_path}" NAME)
            execute_process(
                COMMAND git -C "${_tmp_clone}" show "HEAD:${_repo_path}"
                OUTPUT_FILE "${_dest_dir}/${_fname}"
                RESULT_VARIABLE _show_res
                TIMEOUT 30
            )
            file(REMOVE_RECURSE "${_tmp_clone}")
            if(NOT _show_res EQUAL 0)
                message(FATAL_ERROR "compose_ufsrv_config: git show failed for ${_repo_path} (${_label})")
            endif()
            message(STATUS "compose_ufsrv_config:     [clone] OK")
        endif()
    endif()
endfunction()

# ═══════════════════════════════════════════════════════════════════════════════
# Helper — resolve a module file across staging and local fallback locations
# ═══════════════════════════════════════════════════════════════════════════════

function(_composer_find_module_file _fname _out_var)
    set(_src "")

    # 1. Staging dir — primary: files fetched from toolchain repo via git
    if(EXISTS "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/${_fname}")
        set(_src "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/${_fname}")
    endif()

    # 2. Local src tree — fallback for "LOCAL" toolchain mode
    if(_src STREQUAL "")
        set(_local_src "${UFSRV_CONFIG_COMPOSER_SOURCE_DIR}/src/ufsrv_core/ufsrv_config/${_fname}")
        if(EXISTS "${_local_src}")
            set(_src "${_local_src}")
        endif()
    endif()

    # 3. Local include tree — for ufsrv_config.h specifically
    if(_src STREQUAL "")
        set(_local_inc "${UFSRV_CONFIG_COMPOSER_SOURCE_DIR}/include/ufsrvcorelib/ufsrv_core/ufsrv_config/${_fname}")
        if(EXISTS "${_local_inc}")
            set(_src "${_local_inc}")
        endif()
    endif()

    set(${_out_var} "${_src}" PARENT_SCOPE)
endfunction()

# ═══════════════════════════════════════════════════════════════════════════════
# Bootstrap — configure-time git checkout of all assets
# ═══════════════════════════════════════════════════════════════════════════════

function(compose_ufsrv_config_bootstrap)
    message(STATUS "compose_ufsrv_config: bootstrap — fetching assets into ${UFSRV_CONFIG_COMPOSER_STAGING_DIR}")

    # ── Toolchain assets land in staging root (mirrors original repo layout) ──
    set(_t_repo "${UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REPO}")
    set(_t_ref  "${UFSRV_CONFIG_COMPOSER_TOOLCHAIN_REF}")
    set(_stg     "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}")

    # generator + verifier
    _composer_checkout_file("${_t_repo}" "${_t_ref}"
        "dev/scripts/ufsrv_config_generator/ufsrv_config_generator.c"
        "${_stg}" "generator.c")
    _composer_checkout_file("${_t_repo}" "${_t_ref}"
        "dev/scripts/ufsrv_config_generator/ufsrv_config_verify.py"
        "${_stg}" "verifier.py")

    # invariant ufsrv_config module files
    _composer_checkout_file("${_t_repo}" "${_t_ref}"
        "src/ufsrv_core/ufsrv_config/ufsrv_config.c"
        "${_stg}" "ufsrv_config.c")
    _composer_checkout_file("${_t_repo}" "${_t_ref}"
        "src/ufsrv_core/ufsrv_config/ufsrv_config_generic_callbacks.c"
        "${_stg}" "generic_callbacks.c")
    _composer_checkout_file("${_t_repo}" "${_t_ref}"
        "src/ufsrv_core/ufsrv_config/ufsrv_config_generic_callbacks_priv.h"
        "${_stg}" "generic_callbacks_priv.h")
    _composer_checkout_file("${_t_repo}" "${_t_ref}"
        "include/ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h"
        "${_stg}" "ufsrv_config.h")

    message(STATUS "compose_ufsrv_config: bootstrap complete")
endfunction()

# ═══════════════════════════════════════════════════════════════════════════════
# Composition function — called at build time via cmake -P
# ═══════════════════════════════════════════════════════════════════════════════

function(compose_ufsrv_config)
    file(MAKE_DIRECTORY "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")

    # ── 1. Compose — join/copy per-library generated output ──────────────────
    # At this point the generator has already run (at build time, before the
    # composer custom command).  Each library's generated files are in
    # staging/<lib>/generated/.

    set(_gen_dirs "")
    foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
        set(_lib_gen "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/${_lib_name}/generated")
        list(APPEND _gen_dirs "${_lib_gen}")
    endforeach()

    set(_COMPOSER_GEN_SUBDIR "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}/generated")

    if(_COMPOSER_NUM_LIBS EQUAL 1)
        # ── N=1 fast path ───────────────────────────────────────────────────
        list(GET _gen_dirs 0 _single_gen_dir)
        message(STATUS "compose_ufsrv_config: N=1 — composing from ${_single_gen_dir}")

        file(MAKE_DIRECTORY "${_COMPOSER_GEN_SUBDIR}")
        file(COPY "${_single_gen_dir}/ufsrv_config_field_enums_generated.h"
             DESTINATION "${_COMPOSER_GEN_SUBDIR}")
        file(COPY "${_single_gen_dir}/ufsrv_config_accessor_decls_generated.h"
             DESTINATION "${_COMPOSER_GEN_SUBDIR}")
        file(COPY "${_single_gen_dir}/data_ufsrv_config_field_tables_generated.c"
             DESTINATION "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")
        file(COPY "${_single_gen_dir}/ufsrv_config_accessor_defs_generated.c"
             DESTINATION "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")
        file(COPY "${_single_gen_dir}/ufsrv_config_validate_generated.c"
             DESTINATION "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")

        file(WRITE "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}/ufsrv_config_getters_setters_generated.c"
            "/*\n"
            " * Master include — pulls in all composed generated source files.\n"
            " * Generated by compose_ufsrv_config.cmake (N=1).\n"
            " */\n\n"
            "#include \"generated/ufsrv_config_field_enums_generated.h\"\n"
            "#include \"generated/ufsrv_config_accessor_decls_generated.h\"\n"
            "#include \"data_ufsrv_config_field_tables_generated.c\"\n"
            "#include \"ufsrv_config_accessor_defs_generated.c\"\n"
            "#include \"ufsrv_config_validate_generated.c\"\n"
        )

        if(EXISTS "${_single_gen_dir}/data_ufsrv_config_literal_to_field_map_priv_generated.h")
            file(COPY "${_single_gen_dir}/data_ufsrv_config_literal_to_field_map_priv_generated.h"
                 DESTINATION "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")
        endif()
        if(EXISTS "${_single_gen_dir}/data_ufsrv_config_dispatch_table_priv_generated.h")
            file(COPY "${_single_gen_dir}/data_ufsrv_config_dispatch_table_priv_generated.h"
                 DESTINATION "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")
        endif()
    else()
        # ── N>1: Python join script ─────────────────────────────────────────
        if(NOT DEFINED UFSRV_CONFIG_COMPOSER_JOIN_SCRIPT)
            set(UFSRV_CONFIG_COMPOSER_JOIN_SCRIPT
                "${CMAKE_CURRENT_LIST_DIR}/compose_ufsrv_config_join.py")
        endif()
        if(NOT EXISTS "${UFSRV_CONFIG_COMPOSER_JOIN_SCRIPT}")
            message(FATAL_ERROR "compose_ufsrv_config: join script not found: ${UFSRV_CONFIG_COMPOSER_JOIN_SCRIPT}")
        endif()

        set(_join_args "--output" "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")
        foreach(_lib_name IN LISTS UFSRV_CONFIG_COMPOSER_LIBRARY_NAMES)
            set(_lib_gen "${UFSRV_CONFIG_COMPOSER_STAGING_DIR}/${_lib_name}/generated")
            list(APPEND _join_args "--gen-dir" "${_lib_name}=${_lib_gen}")
        endforeach()
        if(DEFINED UFSRV_CONFIG_COMPOSER_LITERALS_HEADER)
            list(APPEND _join_args "--literals-header" "${UFSRV_CONFIG_COMPOSER_LITERALS_HEADER}")
        endif()

        message(STATUS "compose_ufsrv_config: N=${_COMPOSER_NUM_LIBS} — invoking join script...")
        execute_process(
            COMMAND python3 "${UFSRV_CONFIG_COMPOSER_JOIN_SCRIPT}" ${_join_args}
            RESULT_VARIABLE _join_result
            OUTPUT_VARIABLE _join_out
            ERROR_VARIABLE _join_err
            TIMEOUT 120
        )
        if(NOT _join_result EQUAL 0)
            message(FATAL_ERROR "compose_ufsrv_config: join script failed (${_join_result})\n${_join_out}\n${_join_err}")
        endif()
        message(STATUS "compose_ufsrv_config: ${_join_out}")
    endif()

    # ── 4. Copy invariant ufsrv_config module files into composed output ─────
    # Resolved from staging (git checkout) with local fallback for dev mode.

    _composer_find_module_file("ufsrv_config_generic_callbacks_priv.h" _src)
    if(NOT _src STREQUAL "")
        file(COPY "${_src}" DESTINATION "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")
    else()
        message(WARNING "compose_ufsrv_config: generic_callbacks_priv.h not found")
    endif()

    _composer_find_module_file("ufsrv_config.h" _src)
    if(NOT _src STREQUAL "")
        file(COPY "${_src}" DESTINATION "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")
    else()
        message(WARNING "compose_ufsrv_config: ufsrv_config.h not found")
    endif()

    _composer_find_module_file("ufsrv_config.c" _src)
    if(NOT _src STREQUAL "")
        file(READ "${_src}" _content)
        string(REPLACE
            "ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h"
            "generated/ufsrv_config_field_enums_generated.h"
            _content "${_content}")
        string(REPLACE
            "ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h"
            "generated/ufsrv_config_accessor_decls_generated.h"
            _content "${_content}")
        string(REPLACE
            "<ufsrv_core/ufsrv_config/ufsrv_config.h>"
            "\"ufsrv_config.h\""
            _content "${_content}")
        file(WRITE "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}/ufsrv_config.c" "${_content}")
    else()
        message(WARNING "compose_ufsrv_config: ufsrv_config.c not found")
    endif()

    _composer_find_module_file("ufsrv_config_generic_callbacks.c" _src)
    if(NOT _src STREQUAL "")
        file(READ "${_src}" _content)
        string(REPLACE
            "<ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>"
            "\"ufsrv_config.h\""
            _content "${_content}")
        string(REPLACE
            "<ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>"
            "\"generated/ufsrv_config_field_enums_generated.h\""
            _content "${_content}")
        file(WRITE "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}/ufsrv_config_generic_callbacks.c" "${_content}")
    else()
        message(WARNING "compose_ufsrv_config: generic_callbacks.c not found")
    endif()

    message(STATUS "compose_ufsrv_config: done — ${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}/")
endfunction()

# ═══════════════════════════════════════════════════════════════════════════════
# Configure-time helper — exports composed source list without doing I/O.
# ═══════════════════════════════════════════════════════════════════════════════

function(compute_composed_sources)
    set(_out_dir "${UFSRV_CONFIG_COMPOSER_OUTPUT_DIR}")

    set(_sources
        "${_out_dir}/ufsrv_config.c"
        "${_out_dir}/ufsrv_config_generic_callbacks.c"
        "${_out_dir}/ufsrv_config_getters_setters_generated.c"
    )

    set(_all_outputs
        ${_sources}
        "${_out_dir}/generated/ufsrv_config_field_enums_generated.h"
        "${_out_dir}/generated/ufsrv_config_accessor_decls_generated.h"
        "${_out_dir}/data_ufsrv_config_field_tables_generated.c"
        "${_out_dir}/ufsrv_config_accessor_defs_generated.c"
        "${_out_dir}/ufsrv_config_validate_generated.c"
        "${_out_dir}/data_ufsrv_config_literal_to_field_map_priv_generated.h"
        "${_out_dir}/data_ufsrv_config_dispatch_table_priv_generated.h"
    )

    set(UFSRV_CONFIG_COMPOSED_SOURCES "${_sources}" PARENT_SCOPE)
    set(UFSRV_CONFIG_COMPOSED_INCLUDE_DIR "${_out_dir}" PARENT_SCOPE)
    set(UFSRV_CONFIG_COMPOSED_ALL_OUTPUTS "${_all_outputs}" PARENT_SCOPE)
endfunction()
