/**
 * @file command_line_args_config_type.h
 * @brief CLI argument processing types — result codes, error types, server mode,
 *        unknown‑arg capture, and the top‑level CommandLineArgsConfig struct.
 */

#ifndef UFSRVCORELIB_UFSRV_CORE_CLI_CONFIGURATOR_COMMAND_LINE_ARGS_CONFIG_TYPE_H
#define UFSRVCORELIB_UFSRV_CORE_CLI_CONFIGURATOR_COMMAND_LINE_ARGS_CONFIG_TYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <ufsrvcorelib/ufsrv_core/system/server_process_mode_enum.h>

/* ── Processing state ───────────────────────────────────────────────────── */

/*! Result of parsing a single argument or the entire argv vector. */
typedef enum
{
  UFSRV_ARGS_OK    = 0, ///< processing completed successfully
  UFSRV_ARGS_ERROR = 1  ///< an error occurred — see error_type
} UfsrvArgsState;

/*! Specific error category reported by the parser. */
typedef enum
{
  UFSRV_ARGS_ERR_NONE          = 0, ///< no error
  UFSRV_ARGS_ERR_MISSING_ARG   = 1, ///< a required argument was not supplied
  UFSRV_ARGS_ERR_WRONG_TYPE    = 2, ///< argument has wrong type or value format
  UFSRV_ARGS_ERR_VALUE_MISSING = 3, ///< flag expects a value but none was given (e.g. --port without a number)
  UFSRV_ARGS_ERR_UNKNOWN       = 4  ///< unrecognised flag
} UfsrvArgsErrorType;

enum CliConfigFileOp
{
  CLI_CONFIGFILE_OP_VERIFY =  0, ///< --config=verify
  CLI_CONFIGFILE_OP_VERIFY_STRICT, ///< --config=verify-strict
  CLI_CONFIGFILE_OP_GENERATE, ///< --config=generate

};

/*! Self‑contained result describing parser outcome. */
typedef struct
{
  UfsrvArgsState     state;      ///< OK or ERROR
  UfsrvArgsErrorType error_type; ///< what kind of error (valid when state == ERROR)
  const char *       message;    ///< human‑readable description keyed by error_type
} UfsrvArgsResult;


/* ── Unknown‑argument capture ───────────────────────────────────────────── */

/*! A single unrecognised flag with its optional value. */
typedef struct
{
  const char *flag;  ///< the flag as supplied on the command line (e.g. "--foo")
  const char *value; ///< the value following the flag, or NULL if flag‑only
} UfsrvUnknownArg;

/*! Collection of unknown / unregistered CLI arguments discovered during parsing. */
typedef struct
{
  bool             is_present; ///< true if at least one unknown argument was found
  UfsrvUnknownArg *args;       ///< single calloc'd slab, capacity entries
  size_t           count;      ///< number of populated entries
  size_t           capacity;   ///< total allocated entries
} UfsrvUnknownArgs;

/* ── Top‑level config struct ────────────────────────────────────────────── */

/*!
 * Result of parsing the server command line.
 *
 * The caller pre‑allocates and zero‑initialises one instance, then passes it
 * to UfsrvConfigParseArgs().  Zero‑valued fields indicate "not supplied";
 * the consumer falls back to Lua‑config or built‑in defaults.
 *
 * Unknown flags are collected in @p unknown_args so the consumer can warn or
 * ignore them without losing information.
 */
typedef struct
{
  UfsrvArgsResult result; ///< parser outcome — check .state first

  bool                   help_requested;      ///< --help was supplied
  bool                   version_requested;   ///< --version was supplied
  const char *           config_dir;          ///< --config-dir path, or NULL
  const char *           config_file;         ///< --config-file path, or NULL
  enum ServerProcessMode server_process_mode; ///< --server-mode value,
  int                    instance_id;         ///< --instance-id value, or 0 if not supplied

  bool                 config_op_requested;     ///< --config was supplied
  enum CliConfigFileOp config_file_op;          ///< --config operation (valid when config_op_requested)
  bool        dump_config_json_requested;       ///< --dump-config-json was supplied
  const char *dump_config_json_file;            ///< --dump-config-json path, or NULL if default wanted
  bool        dump_config_json_path_is_default; ///< true if path was allocated by module (must free)
  bool        sandbox_protocol;                 ///< --sandbox-protocol was supplied
  bool        cpu_affinity_verify_requested;    ///< --cpu-affinity-verify was supplied

  UfsrvUnknownArgs unknown_args; ///< unrecognised flags captured here
} CommandLineArgsConfig;

#endif /* UFSRVCORELIB_UFSRV_CORE_CLI_CONFIGURATOR_COMMAND_LINE_ARGS_CONFIG_TYPE_H */
