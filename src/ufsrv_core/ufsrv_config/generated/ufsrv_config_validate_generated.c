/*
 * ufsrv_config_getters_setters_generated.c — GENERATED, DO NOT EDIT
 *
 * Version:    67
 * Generated:  Fri Nov 27 09:26:14 2026
 * Generator:  aa42f0acec8015b8cfd0669182be202dd836212c
 * Catalogue:  d4bb12e727d8ec36dff666a85d4d064353052fb3
 *   Config fields:   50
 *   Runtime fields:  31
 */

#include <stdlib.h>  /* malloc, free */
#include <string.h>  /* memcpy, strncpy */
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>

// Validation

UfsrvConfigResult
UfsrvConfigValidate(void)
{
    // server_run_mode: range [0, 0]
    {
        int64_t _v = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_SERVER_RUN_MODE)->stored_value.int_val;
        if (_v < 0 || _v > 0) {
        UfsrvConfigResult r = { UFSRV_CONFIG_ERR_RANGE,
                                "server_run_mode: out of range [0, 0]" };
        return r;
        }
    }
    // command_console_port: range [1, 65535]
    {
        int64_t _v = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_COMMAND_CONSOLE_PORT)->stored_value.int_val;
        if (_v < 1 || _v > 65535) {
        UfsrvConfigResult r = { UFSRV_CONFIG_ERR_RANGE,
                                "command_console_port: out of range [1, 65535]" };
        return r;
        }
    }
    // server_cpu_affinity: range [0, 0]
    {
        int64_t _v = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_SERVER_CPU_AFFINITY)->stored_value.int_val;
        if (_v < 0 || _v > 0) {
        UfsrvConfigResult r = { UFSRV_CONFIG_ERR_RANGE,
                                "server_cpu_affinity: out of range [0, 0]" };
        return r;
        }
    }
    // stats_backend_port: range [1, 65535]
    {
        int64_t _v = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_STATS_BACKEND_PORT)->stored_value.int_val;
        if (_v < 1 || _v > 65535) {
        UfsrvConfigResult r = { UFSRV_CONFIG_ERR_RANGE,
                                "stats_backend_port: out of range [1, 65535]" };
        return r;
        }
    }
    // logger_driver: not‑empty
    {
        const char *_v = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_LOGGER_DRIVER)->stored_value.str_val;
        if (_v == NULL || _v[0] == '\0') {
        UfsrvConfigResult r = { UFSRV_CONFIG_ERR_EMPTY,
                                "logger_driver: must not be empty" };
        return r;
        }
    }
    // logger_format: not‑empty
    {
        const char *_v = UfsrvConfigConfFileValuesInstance(
            UFSRV_CONFIG_FIELD_LOGGER_FORMAT)->stored_value.str_val;
        if (_v == NULL || _v[0] == '\0') {
        UfsrvConfigResult r = { UFSRV_CONFIG_ERR_EMPTY,
                                "logger_format: must not be empty" };
        return r;
        }
    }
    UfsrvConfigResult ok = { UFSRV_CONFIG_OK, NULL };
    return ok;
}
