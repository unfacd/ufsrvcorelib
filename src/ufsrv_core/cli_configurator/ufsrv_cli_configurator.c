/**
 * @file ufsrv_cli_configurator.c
 * @brief CLI argument parser (getopt_long) — validates argv, fills
 *        CommandLineArgsConfig, captures unknowns.
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uflib/standard_defs.h>
#include <uflib/utils_file.h>
#include <ufsrvcorelib/ufsrv_core/cli_configurator/ufsrv_cli_configurator.h>

// Short option characters

#define OPT_HELP              'h'
#define OPT_VERSION           'v'
#define OPT_CONFIG_DIR        'C'
#define OPT_CONFIG_FILE       'c'
#define OPT_SERVER_MODE       'm'
#define OPT_INSTANCE_ID       'i'
#define OPT_DUMP_CONFIG_JSON  'J'
#define OPT_SANDBOX_PROTOCOL  'S'
#define OPT_CPU_AFFINITY_VERIFY 'a'

/* --config=<op> is long-only (no short form); value outside the char range */
#define OPT_CONFIG            256

// Server-mode string table

static const char *mode_daemon_str   = "daemon";
static const char *mode_terminal_str = "terminal";

// Validation callback type

typedef bool (*UfsrvArgValidator)(const char *raw_value, UfsrvArgsResult *err_out);

// forward declarations — called by validators
static int sLookupServerMode(const char *name);
static void sSetResult(UfsrvArgsResult *result, UfsrvArgsErrorType type);

static bool sValidateConfigDir(const char *value, UfsrvArgsResult *err)
{
  if (!IS_STR_LOADED(value)) { sSetResult(err, UFSRV_ARGS_ERR_VALUE_MISSING); return false; }
  FileInfo fi = {0};
  if (GetFileInfo(value, &fi, 0) != 0 || !fi.exists) { sSetResult(err, UFSRV_ARGS_ERR_WRONG_TYPE); return false; }
  if (!fi.is_directory) { sSetResult(err, UFSRV_ARGS_ERR_WRONG_TYPE); return false; }
  return true;
}

static bool sValidateConfigFile(const char *value, UfsrvArgsResult *err)
{
  if (!IS_STR_LOADED(value)) { sSetResult(err, UFSRV_ARGS_ERR_VALUE_MISSING); return false; }
  /* File existence/content is not validated here — the containing directory
     is not known at parse time. Only the argument's presence and length
     (PATH_MAX) are enforced. */
  if (strlen(value) >= PATH_MAX) { sSetResult(err, UFSRV_ARGS_ERR_WRONG_TYPE); return false; }
  return true;
}

static bool sValidateServerMode(const char *value, UfsrvArgsResult *err)
{
  if (!IS_STR_LOADED(value)) { sSetResult(err, UFSRV_ARGS_ERR_VALUE_MISSING); return false; }
  if (sLookupServerMode(value) < 0) { sSetResult(err, UFSRV_ARGS_ERR_WRONG_TYPE); return false; }
  return true;
}

static bool sValidateInstanceId(const char *value, UfsrvArgsResult *err)
{
  if (!IS_STR_LOADED(value)) { sSetResult(err, UFSRV_ARGS_ERR_VALUE_MISSING); return false; }
  char *endptr = NULL;
  errno = 0;
  long parsed = strtol(value, &endptr, 10);
  if (errno == ERANGE || endptr == value || *endptr != '\0' || parsed < 0 || parsed > INT_MAX) {
    sSetResult(err, UFSRV_ARGS_ERR_WRONG_TYPE);
    return false;
  }
  return true;
}

// Error-message table (indexed by UfsrvArgsErrorType)

static const char *error_messages[] = {
  [UFSRV_ARGS_ERR_NONE]          = "no error",
  [UFSRV_ARGS_ERR_MISSING_ARG]   = "missing required argument",
  [UFSRV_ARGS_ERR_WRONG_TYPE]    = "argument has wrong type or value",
  [UFSRV_ARGS_ERR_VALUE_MISSING] = "flag requires a value but none was supplied",
  [UFSRV_ARGS_ERR_UNKNOWN]       = "unknown argument"
};

// Helpers

static void
sSetResult(UfsrvArgsResult *result, UfsrvArgsErrorType type)
{
  result->state      = UFSRV_ARGS_ERROR;
  result->error_type = type;
  result->message    = error_messages[type];
}

static int
sLookupServerMode(const char *name)
{
  if (strcmp(name, mode_daemon_str)   == 0) return SERVER_PROCESS_MODE_DAEMON;
  if (strcmp(name, mode_terminal_str) == 0) return SERVER_PROCESS_MODE_TERMINAL;
  if (strcmp(name, "systemd") == 0)         return SERVER_PROCESS_MODE_SYSTEMD;
  return -1;  /* unknown */
}

static int
sLookupConfigOp(const char *name)
{
  if (strcmp(name, "verify") == 0)        return CLI_CONFIGFILE_OP_VERIFY;
  if (strcmp(name, "verify-strict") == 0) return CLI_CONFIGFILE_OP_VERIFY_STRICT;
  if (strcmp(name, "generate") == 0)      return CLI_CONFIGFILE_OP_GENERATE;
  return -1;  /* unknown */
}

static bool
sValidateConfigOp(const char *value, UfsrvArgsResult *err)
{
  if (!IS_STR_LOADED(value)) { sSetResult(err, UFSRV_ARGS_ERR_VALUE_MISSING); return false; }
  if (sLookupConfigOp(value) < 0) { sSetResult(err, UFSRV_ARGS_ERR_WRONG_TYPE); return false; }
  return true;
}

// Unknown-arg slab

static void
sUnknownArgsInit(UfsrvUnknownArgs *ua, size_t capacity)
{
  ua->capacity   = capacity;
  ua->count      = 0;
  ua->is_present = false;
  if (capacity > 0) ua->args = calloc(capacity, sizeof(UfsrvUnknownArg));
}

static void
sUnknownArgsAdd(UfsrvUnknownArgs *ua, const char *flag, const char *value)
{
  if (!ua->args || ua->count >= ua->capacity) return;
  ua->args[ua->count].flag  = flag;
  ua->args[ua->count].value = value;
  ua->count++;
  ua->is_present = true;
}

static char *sBuildDefaultDumpPath(const char *argv0)
{
  if (!IS_STR_LOADED(argv0)) goto fallback;
  char *copy = strdup(argv0);
  if (!copy) goto fallback;
  char *base = basename(copy);
  if (!IS_STR_LOADED(base)) { free(copy); goto fallback; }
  char *dot = strrchr(base, '.');
  if (dot) *dot = '\0';
  char *result = NULL;
  if (asprintf(&result, "./%s_config_dump.json", base) < 0) { free(copy); goto fallback; }
  free(copy);
  return result;
fallback:
  return strdup("./ufsrv_config_dump.json");
}

static bool sValidateDumpOutput(const char *path, UfsrvArgsResult *err)
{
  if (!IS_STR_LOADED(path)) { sSetResult(err, UFSRV_ARGS_ERR_VALUE_MISSING); return false; }
  if (access(path, F_OK) == 0) { sSetResult(err, UFSRV_ARGS_ERR_WRONG_TYPE); return false; }
  char path_copy[PATH_MAX];
  strncpy(path_copy, path, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';
  char *dir = dirname(path_copy);
  if (access(dir, W_OK) != 0) { sSetResult(err, UFSRV_ARGS_ERR_WRONG_TYPE); return false; }
  return true;
}

// Public API

void
UfsrvConfigParseArgs(int argc, char **argv, CommandLineArgsConfig *out)
{
  if (!out || argc < 1 || !argv) return;

  optind = 1;  /* reset getopt state for repeatable calls */

  // getopt_long option table

  static struct option long_options[] = {
    { "help",             no_argument,       NULL, OPT_HELP              },
    { "version",          no_argument,       NULL, OPT_VERSION           },
    { "config-dir",       required_argument, NULL, OPT_CONFIG_DIR        },
    { "config-file",      required_argument, NULL, OPT_CONFIG_FILE       },
    { "server-mode",      required_argument, NULL, OPT_SERVER_MODE       },
    { "instance-id",      required_argument, NULL, OPT_INSTANCE_ID       },
    { "config",           required_argument, NULL, OPT_CONFIG            },
    { "dump-config-json", optional_argument, NULL, OPT_DUMP_CONFIG_JSON  },
    { "sandbox-protocol", no_argument,       NULL, OPT_SANDBOX_PROTOCOL  },
    { "cpu-affinity-verify", no_argument,    NULL, OPT_CPU_AFFINITY_VERIFY },
    { NULL, 0, NULL, 0 }
  };

  // Pre-allocate unknown-arg slab

  size_t max_unknowns = (argc > 1) ? (size_t)(argc - 1) : 0;
  sUnknownArgsInit(&out->unknown_args, max_unknowns);

  // Parse loop

  opterr = 0;  /* suppress getopt's own error messages — we report via result */

  int opt;
  while ((opt = getopt_long(argc, argv, ":hvC:c:m:i:J::Sa", long_options, NULL)) != -1) {

    switch (opt) {

    case OPT_HELP:
      out->help_requested = true;
      break;

    case OPT_VERSION:
      out->version_requested = true;
      break;

    case OPT_CONFIG_DIR:
      if (sValidateConfigDir(optarg, &out->result))
        out->config_dir = optarg;
      else
        return;
      break;

    case OPT_CONFIG_FILE:
      if (sValidateConfigFile(optarg, &out->result))
        out->config_file = optarg;
      else
        return;
      break;

    case OPT_SERVER_MODE:
      if (!sValidateServerMode(optarg, &out->result))
        return;
      out->server_process_mode = (enum ServerProcessMode)sLookupServerMode(optarg);
      break;

    case OPT_INSTANCE_ID:
      if (!sValidateInstanceId(optarg, &out->result))
        return;
      out->instance_id = (int)strtol(optarg, NULL, 10);
      break;

    case OPT_CONFIG:
      if (!sValidateConfigOp(optarg, &out->result))
        return;
      out->config_op_requested = true;
      out->config_file_op = (enum CliConfigFileOp)sLookupConfigOp(optarg);
      break;

    case OPT_DUMP_CONFIG_JSON:
      out->dump_config_json_requested = true;
      if (optarg) {
        if (!sValidateDumpOutput(optarg, &out->result))
          return;
        out->dump_config_json_file = optarg;
        out->dump_config_json_path_is_default = false;
      } else {
        out->dump_config_json_file = sBuildDefaultDumpPath(argv[0]);
        out->dump_config_json_path_is_default = true;
      }
      break;

    case OPT_SANDBOX_PROTOCOL:
      out->sandbox_protocol = true;
      break;

    case OPT_CPU_AFFINITY_VERIFY:
      out->cpu_affinity_verify_requested = true;
      break;

    case ':':
      // getopt_long returns ':' when a required_argument is missing
      sSetResult(&out->result, UFSRV_ARGS_ERR_VALUE_MISSING);
      return;

    case '?':
    default:
      sUnknownArgsAdd(&out->unknown_args, argv[optind - 1], NULL);
      break;
    }
  }

  // Non-option arguments (stuff after "--" or bare words)

  while (optind < argc) {
    sUnknownArgsAdd(&out->unknown_args, argv[optind++], NULL);
  }

  // Success

  out->result.state      = UFSRV_ARGS_OK;
  out->result.error_type = UFSRV_ARGS_ERR_NONE;
  out->result.message    = error_messages[UFSRV_ARGS_ERR_NONE];
}

void
UfsrvConfigArgsDestruct(CommandLineArgsConfig *cfg)
{
  if (!cfg) return;
  if (cfg->dump_config_json_path_is_default) free((char *)cfg->dump_config_json_file);
  free(cfg->unknown_args.args);
  memset(cfg, 0, sizeof(*cfg));
}

// UsageEntry — single source of truth for flag documentation

typedef struct {
  const char *short_opt;
  const char *long_opt;
  const char *arg_name;
  const char *description;
  const char *default_val;
} UsageEntry;

#define USAGE_FLAG_WIDTH 34

void
UfsrvConfigPrintUsage(FILE *stream, const char *program_name, const char *extra_usage)
{
  if (!stream || !program_name) return;

  static const UsageEntry entries[] = {
    // Options section
    { NULL, NULL,               NULL,          "Options",                                NULL              },
    { "-h", "--help",           NULL,          "Display this help message and exit",     NULL              },
    { "-v", "--version",        NULL,          "Display version information and exit",   NULL              },
    { "-c", "--config-file",    "<file>",      "Path to configuration file",             "ufsrv.conf.lua"  },
    { "-C", "--config-dir",     "<dir>",       "Path to runtime root configuration directory",        "/opt/ufsrv"  },
    { "-m", "--server-mode",    "<mode>",      "Server run mode: 'daemon' (background) or", NULL           },
    { NULL, NULL,               "'terminal' (foreground)", NULL,                         "daemon"          },
    { "-i", "--instance-id",   "<id>",        "Enter instance id for this server (ideally sequential)", NULL },
    { NULL, "--sandbox-protocol",NULL,         "Enable sandbox protocol mode",           NULL              },
    // Diagnostic options section
    { NULL, NULL,               NULL,          "Diagnostic options",                     NULL              },
    { NULL, "--config",         "<op>",        "Config file operation: 'verify', 'verify-strict', or 'generate'", NULL },
    { NULL, "--dump-config-json","[=<path>]",  "Dump resolved configuration to JSON",   NULL              },
    { NULL, NULL,               "(default: <prog>_config_dump.json)", NULL,              NULL              },
    { NULL, "--cpu-affinity-verify",NULL,      "Verify CPU affinity layout and recommend a policy", NULL },
    { NULL, NULL, NULL, NULL, NULL }  // sentinel
  };

  fprintf(stream, "Usage: %s [OPTIONS]\n\n", program_name);
  fprintf(stream, "ufsrv server — command-line arguments.\n");

  for (const UsageEntry *e = entries; e->long_opt || e->short_opt || e->description || e->arg_name; e++) {

    // Section header
    if (!e->short_opt && !e->long_opt && e->description) {
      fprintf(stream, "\n%s:\n", e->description);
      continue;
    }

    // Continuation line
    if (!e->description) {
      fprintf(stream, "%-*s  %s", USAGE_FLAG_WIDTH, "", e->arg_name ? e->arg_name : "");
      if (e->default_val)
        fprintf(stream, " (default: %s)", e->default_val);
      fprintf(stream, "\n");
      continue;
    }

    // Build flag column
    char flag_col[USAGE_FLAG_WIDTH + 1];
    memset(flag_col, ' ', USAGE_FLAG_WIDTH);
    flag_col[USAGE_FLAG_WIDTH] = '\0';

    int pos = 0;
    if (e->short_opt) {
      flag_col[pos++] = ' '; flag_col[pos++] = ' ';
      pos += snprintf(&flag_col[pos], USAGE_FLAG_WIDTH - pos, "%s", e->short_opt);
      if (e->long_opt) flag_col[pos++] = ',';
    } else {
      pos = 4;
    }

    if (e->long_opt)
      pos += snprintf(&flag_col[pos], USAGE_FLAG_WIDTH - pos, " %s", e->long_opt);
    if (e->arg_name)
      pos += snprintf(&flag_col[pos], USAGE_FLAG_WIDTH - pos, " %s", e->arg_name);

    // Print flag + description + default
    fprintf(stream, "%.*s %s", USAGE_FLAG_WIDTH, flag_col, e->description);
    if (e->default_val)
      fprintf(stream, " (default: %s)", e->default_val);
    fprintf(stream, "\n");
  }

  // Application-specific extras
  if (IS_STR_LOADED(extra_usage))
    fprintf(stream, "\nApplication-specific options:\n%s", extra_usage);
}
