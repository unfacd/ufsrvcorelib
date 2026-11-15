/**
 * @file ufsrv_cli_configurator.h
 * @brief Public API for CLI argument parsing — UfsrvConfigParseArgs and UfsrvConfigArgsDestruct.
 */

#ifndef UFSRVCORELIB_UFSRV_CORE_CLI_CONFIGURATOR_UFSRV_CLI_CONFIGURATOR_H
#define UFSRVCORELIB_UFSRV_CORE_CLI_CONFIGURATOR_UFSRV_CLI_CONFIGURATOR_H

#include <stdio.h>   /* FILE — used by UfsrvConfigPrintUsage */

#include <ufsrvcorelib/ufsrv_core/cli_configurator/command_line_args_config_type.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>

/**
 * @brief Parse command-line arguments into a preallocated CommandLineArgsConfig.
 *
 * Caller allocates and zero-initialises @p out.  This function never exit()s —
 * errors are reported through @p out->result.  The caller owns control‑flow
 * decisions (help, version, exit).
 *
 * @param argc  Argument count (from main)
 * @param argv  Argument vector (from main)
 * @param out   Preallocated, zero‑filled config struct to populate
 *
 * @code{.c}
 * int main(int argc, char **argv) {
 *   CommandLineArgsConfig cfg = {0};
 *
 *   UfsrvConfigParseArgs(argc, argv, &cfg);
 *
 *   if (cfg.result.state == UFSRV_ARGS_ERROR) {
 *     fprintf(stderr, "%s\n", cfg.result.message);
 *     return EXIT_FAILURE;
 *   }
 *   if (cfg.help_requested)    { print_help();    return EXIT_SUCCESS; }
 *   if (cfg.version_requested) { print_version(); return EXIT_SUCCESS; }
 *
 *   // apply overrides — CLI values win over Lua defaults
 *   if (cfg.config_dir)  apply_config_dir(cfg.config_dir);
 *   if (cfg.config_file) apply_config_file(cfg.config_file);
 *
 *   UfsrvConfigArgsDestruct(&cfg);
 *   return EXIT_SUCCESS;
 * }
 * @endcode
 */
PUBLIC_API void UfsrvConfigParseArgs(int argc, char **argv, CommandLineArgsConfig *out);

/**
 * @brief Free internal allocations and zero the struct.
 *
 * Safe to call on a zero‑initialised config (no‑op).  After return the struct
 * is back in its initial state and can be reused or discarded.
 */
PUBLIC_API void UfsrvConfigArgsDestruct(CommandLineArgsConfig *cfg);

/**
 * @brief Print a formatted usage message listing all recognised command-line options.
 *
 * Prints to @p stream (typically stdout).  Includes the program name, a brief
 * description, and all recognised flags with their short/long forms, argument
 * names, and default values.  Flags are grouped into three sections: Options,
 * Diagnostic options, and (if @p extra_usage is non‑NULL and non‑empty)
 * Application-specific options.
 *
 * @param stream       Output stream — stdout for normal use, stderr for errors
 * @param program_name argv[0] — displayed in the "Usage:" line
 * @param extra_usage  Optional application‑specific flag descriptions, or NULL
 *
 * @code{.c}
 * if (cfg.help_requested) {
 *     UfsrvConfigPrintUsage(stdout, argv[0],
 *         "  -i <id>     Server ID override (legacy)\n"
 *         "  -s <class>  Server class name (legacy)\n");
 *     UfsrvConfigArgsDestruct(&cfg);
 *     return false;
 * }
 * @endcode
 */
PUBLIC_API void UfsrvConfigPrintUsage(FILE *stream, const char *program_name, const char *extra_usage);

#endif /* UFSRVCORELIB_UFSRV_CORE_CLI_CONFIGURATOR_UFSRV_CLI_CONFIGURATOR_H */
