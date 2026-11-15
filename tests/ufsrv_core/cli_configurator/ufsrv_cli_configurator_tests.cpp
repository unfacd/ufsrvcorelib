/**
 * @file ufsrv_configurator_tests.cpp
 * @brief Unit tests for the CLI argument parser (UfsrvConfigParseArgs /
 *        UfsrvConfigArgsDestruct).
 */

#include <gtest/gtest.h>
#include <limits.h>
#include <unistd.h>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/cli_configurator/ufsrv_cli_configurator.h>
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

class ParseArgsTest : public ::testing::Test {
protected:
  CommandLineArgsConfig cfg;

  void SetUp() override    { memset(&cfg, 0, sizeof(cfg)); }
  void TearDown() override { UfsrvConfigArgsDestruct(&cfg); }

  void parse(std::initializer_list<const char *> args) {
    std::vector<const char *> argv = {"ufsrv"};
    argv.insert(argv.end(), args.begin(), args.end());
    argv.push_back(nullptr);
    UfsrvConfigParseArgs((int)argv.size() - 1, const_cast<char **>(argv.data()), &cfg);
  }
};

/* ── Boolean flags ──────────────────────────────────────────────────────── */

TEST_F(ParseArgsTest, HelpLong) {
  parse({"--help"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.help_requested);
  EXPECT_FALSE(cfg.version_requested);
}

TEST_F(ParseArgsTest, HelpShort) {
  parse({"-h"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.help_requested);
}

TEST_F(ParseArgsTest, VersionLong) {
  parse({"--version"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.version_requested);
  EXPECT_FALSE(cfg.help_requested);
}

TEST_F(ParseArgsTest, VersionShort) {
  parse({"-v"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.version_requested);
}

TEST_F(ParseArgsTest, CombinedShortFlags) {
  parse({"-hv"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.help_requested);
  EXPECT_TRUE(cfg.version_requested);
}

/* ── String arguments ───────────────────────────────────────────────────── */

TEST_F(ParseArgsTest, ConfigDirLongSpace) {
  parse({"--config-dir", "/tmp"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_dir, "/tmp");
}

TEST_F(ParseArgsTest, ConfigDirLongEquals) {
  parse({"--config-dir=/tmp"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_dir, "/tmp");
}

TEST_F(ParseArgsTest, ConfigDirShort) {
  parse({"-C", "/tmp"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_dir, "/tmp");
}

TEST_F(ParseArgsTest, ConfigFileLong) {
  parse({"--config-file", "/etc/hostname"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_file, "/etc/hostname");
}

TEST_F(ParseArgsTest, ConfigFileLongEquals) {
  parse({"--config-file=/etc/hostname"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_file, "/etc/hostname");
}

TEST_F(ParseArgsTest, ConfigFileShort) {
  parse({"-c", "/etc/hostname"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_file, "/etc/hostname");
}

/* ── Server mode ────────────────────────────────────────────────────────── */

TEST_F(ParseArgsTest, ServerModeDaemonLong) {
  parse({"--server-mode", "daemon"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.server_process_mode, SERVER_PROCESS_MODE_DAEMON);
}

TEST_F(ParseArgsTest, ServerModeTerminalLong) {
  parse({"--server-mode", "terminal"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.server_process_mode, SERVER_PROCESS_MODE_TERMINAL);
}

TEST_F(ParseArgsTest, ServerModeSystemdLong) {
  parse({"--server-mode", "systemd"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.server_process_mode, SERVER_PROCESS_MODE_SYSTEMD);
}

TEST_F(ParseArgsTest, ServerModeShort) {
  parse({"-m", "terminal"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.server_process_mode, SERVER_PROCESS_MODE_TERMINAL);
}

TEST_F(ParseArgsTest, ServerModeInvalid) {
  parse({"--server-mode", "bogus"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

/* ── Error: missing value ───────────────────────────────────────────────── */

TEST_F(ParseArgsTest, MissingValueLong) {
  parse({"--server-mode"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

TEST_F(ParseArgsTest, MissingValueShort) {
  parse({"-m"});  // trailing flag with nothing after
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

/* ── Unknown arguments captured ─────────────────────────────────────────── */

TEST_F(ParseArgsTest, UnknownLongFlag) {
  parse({"--foo", "bar"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.unknown_args.is_present);
  EXPECT_EQ(cfg.unknown_args.count, 2u);
  EXPECT_STREQ(cfg.unknown_args.args[0].flag, "--foo");
  EXPECT_STREQ(cfg.unknown_args.args[1].flag, "bar");
}

TEST_F(ParseArgsTest, UnknownShortFlag) {
  parse({"-x"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.unknown_args.is_present);
  EXPECT_EQ(cfg.unknown_args.count, 1u);
  EXPECT_STREQ(cfg.unknown_args.args[0].flag, "-x");
  EXPECT_EQ(cfg.unknown_args.args[0].value, nullptr);
}

/* ── Zero‑value fields mean "not supplied" ──────────────────────────────── */

TEST_F(ParseArgsTest, EmptyArgsYieldsDefaults) {
  parse({});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_FALSE(cfg.help_requested);
  EXPECT_FALSE(cfg.version_requested);
  EXPECT_EQ(cfg.config_dir, nullptr);
  EXPECT_EQ(cfg.config_file, nullptr);
  EXPECT_EQ(cfg.server_process_mode, SERVER_PROCESS_MODE_DEFAULT);  // enum 0 = "not specified"
  EXPECT_FALSE(cfg.config_op_requested);
  EXPECT_FALSE(cfg.dump_config_json_requested);
  EXPECT_EQ(cfg.dump_config_json_file, nullptr);
  EXPECT_FALSE(cfg.sandbox_protocol);
  EXPECT_FALSE(cfg.cpu_affinity_verify_requested);
  EXPECT_FALSE(cfg.unknown_args.is_present);
}

/* ── Destructor safety ──────────────────────────────────────────────────── */

TEST_F(ParseArgsTest, DestructOnZeroInitIsNoop) {
  CommandLineArgsConfig empty = {};
  UfsrvConfigArgsDestruct(&empty);  // must not crash
  SUCCEED();
}

TEST_F(ParseArgsTest, DestructWithNullCfg) {
  UfsrvConfigArgsDestruct(nullptr);  // line 184 guard: !cfg → return
  SUCCEED();
}

TEST_F(ParseArgsTest, DoubleDestructIsNoop) {
  parse({"--help"});
  UfsrvConfigArgsDestruct(&cfg);     // first — frees slab, zeroes cfg
  UfsrvConfigArgsDestruct(&cfg);     // second — free(NULL) + memset, no‑op
  SUCCEED();
}

/* ── Null / zero guard paths ────────────────────────────────────────────── */

TEST_F(ParseArgsTest, NullCfgDoesNotCrash) {
  char *dummy_argv[] = { (char *)"ufsrv", nullptr };
  UfsrvConfigParseArgs(1, dummy_argv, nullptr);  // line 95 guard: !out → return
  SUCCEED();
}

TEST_F(ParseArgsTest, ZeroArgcDoesNotCrash) {
  char *dummy_argv[] = { (char *)"ufsrv", nullptr };
  UfsrvConfigParseArgs(0, dummy_argv, &cfg);     // line 95 guard: argc < 1 → return
  /* cfg untouched — still zeroed from SetUp */
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);    // default (0) == OK
}

TEST_F(ParseArgsTest, NullArgvDoesNotCrash) {
  UfsrvConfigParseArgs(1, nullptr, &cfg);        // line 95 guard: !argv → return
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);    // cfg untouched
}

/* ── Server mode edge cases ─────────────────────────────────────────────── */

TEST_F(ParseArgsTest, ServerModeEmptyString) {
  parse({"--server-mode", ""});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

TEST_F(ParseArgsTest, ServerModePartialMatch) {
  /* lookup_server_mode uses strcmp (exact), not prefix match */
  parse({"--server-mode", "daem"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, ServerModeCaseSensitive) {
  parse({"--server-mode", "DAEMON"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, ServerModeLongEquals) {
  parse({"--server-mode=terminal"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.server_process_mode, SERVER_PROCESS_MODE_TERMINAL);
}

/* ── String argument edge cases ─────────────────────────────────────────── */

TEST_F(ParseArgsTest, ConfigDirEmptyString) {
  parse({"--config-dir", ""});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

TEST_F(ParseArgsTest, ConfigFileEmptyString) {
  parse({"--config-file", ""});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

/* ── Short flag with attached value (no space) ──────────────────────────── */

TEST_F(ParseArgsTest, ConfigDirShortAttached) {
  /* getopt supports -Cvalue with no separating space */
  parse({"-C/tmp"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_dir, "/tmp");
}

TEST_F(ParseArgsTest, ConfigFileShortAttached) {
  parse({"-c/etc/hostname"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_file, "/etc/hostname");
}

/* ── Non‑option arguments (captured after flags are consumed) ───────────── */

TEST_F(ParseArgsTest, NonOptionArgsAfterFlags) {
  /* getopt stops at the first non‑option argument.  Remaining argv entries
     are captured by the while (optind < argc) loop (line 170). */
  parse({"--help", "foo", "bar"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.help_requested);
  EXPECT_TRUE(cfg.unknown_args.is_present);
  /* "foo" and "bar" are captured as unknown args */
  EXPECT_GE(cfg.unknown_args.count, 2u);
}

TEST_F(ParseArgsTest, NonOptionArgsBareWords) {
  /* Bare words without any recognised flags */
  parse({"alpha", "beta"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.unknown_args.is_present);
  EXPECT_GE(cfg.unknown_args.count, 2u);
}

/* ── Unknown long flag with = value syntax ──────────────────────────────── */

TEST_F(ParseArgsTest, UnknownLongFlagWithEqualsValue) {
  /* --foo=bar — getopt returns '?' for --foo, optarg points to "bar"
     when = is used.  The parser stores argv[optind-1] ("--foo=bar")
     as a single entry. */
  parse({"--foo=bar"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.unknown_args.is_present);
  EXPECT_GE(cfg.unknown_args.count, 1u);
}

/* ── Missing value for config‑dir / config‑file (not just server‑mode) ──── */

TEST_F(ParseArgsTest, MissingValueConfigDir) {
  parse({"--config-dir"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

TEST_F(ParseArgsTest, MissingValueConfigFile) {
  parse({"--config-file"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

/* ── All recognised flags together ──────────────────────────────────────── */

TEST_F(ParseArgsTest, AllFlagsCombined) {
  parse({"--help", "--version", "--config-dir", "/tmp",
         "--config-file", "/etc/hostname", "--server-mode", "daemon"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.help_requested);
  EXPECT_TRUE(cfg.version_requested);
  EXPECT_STREQ(cfg.config_dir, "/tmp");
  EXPECT_STREQ(cfg.config_file, "/etc/hostname");
  EXPECT_EQ(cfg.server_process_mode, SERVER_PROCESS_MODE_DAEMON);
}

/* ── Repeated flags (last wins) ─────────────────────────────────────────── */

TEST_F(ParseArgsTest, RepeatedServerModeLastWins) {
  parse({"--server-mode", "daemon", "--server-mode", "terminal"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.server_process_mode, SERVER_PROCESS_MODE_TERMINAL);
}

TEST_F(ParseArgsTest, RepeatedConfigDirLastWins) {
  parse({"--config-dir", "/tmp", "--config-dir", "/dev"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_STREQ(cfg.config_dir, "/dev");
}

/* ── optind reset across repeated calls (line 97: optind = 1) ───────────── */

TEST_F(ParseArgsTest, RepeatedParseCallResetsGetopt) {
  /* First parse — extract --help */
  parse({"--help"});
  EXPECT_TRUE(cfg.help_requested);

  /* Destruct + zero */
  UfsrvConfigArgsDestruct(&cfg);
  memset(&cfg, 0, sizeof(cfg));

  /* Second parse with new args — must not be polluted by prior state */
  parse({"--version"});
  EXPECT_TRUE(cfg.version_requested);
  EXPECT_FALSE(cfg.help_requested);   /* not from prior call */
}

// ── Validator rejection paths ─────────────────────────────────────────────

TEST_F(ParseArgsTest, ConfigDirNonexistent) {
  parse({"--config-dir", "/nonexistent"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, ConfigDirNotDirectory) {
  parse({"--config-dir", "/dev/null"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, ConfigFileTooLong) {
  std::string overlong(PATH_MAX + 1, 'x');
  parse({"--config-file", overlong.c_str()});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

// ── Stress test — maximum arguments ────────────────────────────────────────

TEST_F(ParseArgsTest, MaxArgsStress) {
  const int N = 64;
  std::vector<char *> argv;
  argv.push_back(strdup("ufsrv"));
  for (int i = 0; i < N; i++) {
    argv.push_back(strdup("--help"));
    argv.push_back(strdup("--foo"));
  }
  argv.push_back(nullptr);

  UfsrvConfigParseArgs((int)argv.size() - 1, argv.data(), &cfg);
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.help_requested);
  EXPECT_TRUE(cfg.unknown_args.is_present);
  EXPECT_EQ(cfg.unknown_args.count, (size_t)N);

  for (char *s : argv) free(s);
}

// ── New flags: --config=<op> ────────────────────────────────────────────────

TEST_F(ParseArgsTest, ConfigOpVerify) {
  parse({"--config=verify"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.config_op_requested);
  EXPECT_EQ(cfg.config_file_op, CLI_CONFIGFILE_OP_VERIFY);
}

TEST_F(ParseArgsTest, ConfigOpVerifySpaceSeparated) {
  parse({"--config", "verify"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.config_op_requested);
  EXPECT_EQ(cfg.config_file_op, CLI_CONFIGFILE_OP_VERIFY);
}

TEST_F(ParseArgsTest, ConfigOpVerifyStrict) {
  parse({"--config=verify-strict"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.config_op_requested);
  EXPECT_EQ(cfg.config_file_op, CLI_CONFIGFILE_OP_VERIFY_STRICT);
}

TEST_F(ParseArgsTest, ConfigOpGenerate) {
  parse({"--config=generate"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.config_op_requested);
  EXPECT_EQ(cfg.config_file_op, CLI_CONFIGFILE_OP_GENERATE);
}

TEST_F(ParseArgsTest, ConfigOpInvalid) {
  parse({"--config=bogus"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, ConfigOpCaseSensitive) {
  parse({"--config=VERIFY"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, ConfigOpMissingValue) {
  parse({"--config"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

TEST_F(ParseArgsTest, ConfigOpNotRequested) {
  parse({});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_FALSE(cfg.config_op_requested);
}

// ── New flags: --dump-config-json ───────────────────────────────────────────

TEST_F(ParseArgsTest, DumpConfigJsonRequestedNoArg) {
  parse({"--dump-config-json"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.dump_config_json_requested);
  EXPECT_TRUE(cfg.dump_config_json_path_is_default);
  EXPECT_NE(cfg.dump_config_json_file, nullptr);
  EXPECT_NE(strstr(cfg.dump_config_json_file, "_config_dump.json"), nullptr);
}

TEST_F(ParseArgsTest, DumpConfigJsonWithPath) {
  parse({"--dump-config-json=/tmp/ufsrv_test_out.json"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.dump_config_json_requested);
  EXPECT_FALSE(cfg.dump_config_json_path_is_default);
  EXPECT_STREQ(cfg.dump_config_json_file, "/tmp/ufsrv_test_out.json");
}

TEST_F(ParseArgsTest, DumpConfigJsonFileExists) {
  parse({"--dump-config-json=/etc/hostname"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, DumpConfigJsonDirNotWritable) {
  parse({"--dump-config-json=/root/ufsrv_test_out.json"});
  if (geteuid() != 0) {
    EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
    EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
  }
}

TEST_F(ParseArgsTest, DumpConfigJsonNotRequested) {
  parse({});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_FALSE(cfg.dump_config_json_requested);
  EXPECT_EQ(cfg.dump_config_json_file, nullptr);
}

// ── New flags: --sandbox-protocol ────────────────────────────────────────────

TEST_F(ParseArgsTest, SandboxProtocolNotSupplied) {
  parse({});
  EXPECT_FALSE(cfg.sandbox_protocol);
}

TEST_F(ParseArgsTest, SandboxProtocolEnabled) {
  parse({"--sandbox-protocol"});
  EXPECT_TRUE(cfg.sandbox_protocol);
}

TEST_F(ParseArgsTest, SandboxProtocolWithOtherFlags) {
  parse({"--sandbox-protocol", "--help"});
  EXPECT_TRUE(cfg.sandbox_protocol);
  EXPECT_TRUE(cfg.help_requested);
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
}

// ── Combined diagnostic flags ────────────────────────────────────────────────

TEST_F(ParseArgsTest, CombinedDiagnosticFlags) {
  parse({"--config=verify", "--dump-config-json"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.config_op_requested);
  EXPECT_TRUE(cfg.dump_config_json_requested);
}

// ── New flags: --cpu-affinity-verify ─────────────────────────────────────

TEST_F(ParseArgsTest, CpuAffinityVerifyNotSupplied) {
  parse({});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_FALSE(cfg.cpu_affinity_verify_requested);
}

TEST_F(ParseArgsTest, CpuAffinityVerifyRequested) {
  parse({"--cpu-affinity-verify"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.cpu_affinity_verify_requested);
}

TEST_F(ParseArgsTest, CpuAffinityVerifyWithOtherFlags) {
  parse({"--cpu-affinity-verify", "--help"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_TRUE(cfg.cpu_affinity_verify_requested);
  EXPECT_TRUE(cfg.help_requested);
}

// ── Instance ID (--instance-id / -i) ─────────────────────────────────────

TEST_F(ParseArgsTest, InstanceIdLong) {
  parse({"--instance-id", "5"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.instance_id, 5);
}

TEST_F(ParseArgsTest, InstanceIdShort) {
  parse({"-i", "7"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.instance_id, 7);
}

TEST_F(ParseArgsTest, InstanceIdLongEquals) {
  parse({"--instance-id=3"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.instance_id, 3);
}

TEST_F(ParseArgsTest, InstanceIdShortAttached) {
  parse({"-i9"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.instance_id, 9);
}

TEST_F(ParseArgsTest, InstanceIdNotSupplied) {
  parse({});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_OK);
  EXPECT_EQ(cfg.instance_id, 0);
}

TEST_F(ParseArgsTest, InstanceIdInvalid) {
  parse({"--instance-id", "abc"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, InstanceIdNegative) {
  parse({"--instance-id", "-5"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, InstanceIdTrailingGarbage) {
  parse({"--instance-id", "12abc"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

TEST_F(ParseArgsTest, InstanceIdMissingValue) {
  parse({"-i"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_VALUE_MISSING);
}

TEST_F(ParseArgsTest, InstanceIdOverflow) {
  parse({"--instance-id", "99999999999999999999"});
  EXPECT_EQ(cfg.result.state, UFSRV_ARGS_ERROR);
  EXPECT_EQ(cfg.result.error_type, UFSRV_ARGS_ERR_WRONG_TYPE);
}

// ── UfsrvConfigPrintUsage ───────────────────────────────────────────────────

static int sCaptureUsage(const char *prog, const char *extra, char *buf, size_t bufsz)
{
  const char *tmpfile = "/tmp/ufsrv_test_usage.txt";
  int saved = dup(STDOUT_FILENO);
  if (saved < 0) return -1;
  FILE *cap = freopen(tmpfile, "w", stdout);
  if (!cap) { close(saved); return -1; }
  UfsrvConfigPrintUsage(stdout, prog, extra);
  fflush(stdout);
  dup2(saved, STDOUT_FILENO);
  close(saved);
  FILE *f = fopen(tmpfile, "r");
  if (!f) return -1;
  size_t n = fread(buf, 1, bufsz - 1, f);
  buf[n] = '\0';
  fclose(f);
  unlink(tmpfile);
  return (int)n;
}

TEST_F(ParseArgsTest, PrintUsageWritesToStdout) {
  char buf[4096] = {0};
  int n = sCaptureUsage("testprog", NULL, buf, sizeof(buf));
  ASSERT_GT(n, 0);
  EXPECT_NE(strstr(buf, "Usage: testprog"), nullptr);
  EXPECT_NE(strstr(buf, "--help"), nullptr);
  EXPECT_NE(strstr(buf, "--version"), nullptr);
}

TEST_F(ParseArgsTest, PrintUsageNullGuards) {
  UfsrvConfigPrintUsage(NULL, "prog", NULL);
  UfsrvConfigPrintUsage(stdout, NULL, NULL);
  SUCCEED();
}

TEST_F(ParseArgsTest, PrintUsageIncludesAllFlags) {
  char buf[4096] = {0};
  sCaptureUsage("prog", NULL, buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "--help"), nullptr);
  EXPECT_NE(strstr(buf, "--version"), nullptr);
  EXPECT_NE(strstr(buf, "--config-file"), nullptr);
  EXPECT_NE(strstr(buf, "--config-dir"), nullptr);
  EXPECT_NE(strstr(buf, "--server-mode"), nullptr);
  EXPECT_NE(strstr(buf, "--sandbox-protocol"), nullptr);
  EXPECT_NE(strstr(buf, "--config "), nullptr);  // --config <op> (distinct from --config-file/-dir)
  EXPECT_NE(strstr(buf, "--dump-config-json"), nullptr);
  EXPECT_NE(strstr(buf, "--cpu-affinity-verify"), nullptr);
}

TEST_F(ParseArgsTest, PrintUsageDiagnosticSection) {
  char buf[4096] = {0};
  sCaptureUsage("prog", NULL, buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "Diagnostic options:"), nullptr);
}

TEST_F(ParseArgsTest, PrintUsageExtraUsageAppended) {
  char buf[4096] = {0};
  sCaptureUsage("prog", "  -x  extra flag", buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "Application-specific options:"), nullptr);
  EXPECT_NE(strstr(buf, "-x"), nullptr);
}

TEST_F(ParseArgsTest, PrintUsageEmptyExtraNoHeader) {
  char buf[4096] = {0};
  sCaptureUsage("prog", NULL, buf, sizeof(buf));
  EXPECT_EQ(strstr(buf, "Application-specific options:"), nullptr);
}

TEST_F(ParseArgsTest, PrintUsageDefaultsListed) {
  char buf[4096] = {0};
  sCaptureUsage("prog", NULL, buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "(default: ufsrv.conf.lua)"), nullptr);
  EXPECT_NE(strstr(buf, "(default: /opt/ufsrv/etc)"), nullptr);
  EXPECT_NE(strstr(buf, "(default: daemon)"), nullptr);
}

TEST_F(ParseArgsTest, PrintUsageSandboxProtocol) {
  char buf[4096] = {0};
  sCaptureUsage("prog", NULL, buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "--sandbox-protocol"), nullptr);
}

TEST_F(ParseArgsTest, PrintUsageOptionalArgNotation) {
  char buf[4096] = {0};
  sCaptureUsage("prog", NULL, buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "[=<path>]"), nullptr);
}
