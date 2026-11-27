#include <gtest/gtest.h>

extern "C" {
#include <ufsrvcorelib/ufsrv_core/cli_configurator/command_line_args_config_type.h>

void        TestBootstrapConfigStores(void);
void        TestResetConfigStoreDefaults(void);
void        TestFakeSetCredentialsDirectory(const char *directory);
const char *TestFakeCredentialPathRead(void);

void        ConfigBootstrapCliDefaults(const CommandLineArgsConfig *cli_configuration_provided);
void        ConfigFinalizeSensitiveConfig(void);
}

namespace {

constexpr const char *kConfigDir       = "/opt/ufsrv/ufsrvcorelib";
constexpr const char *kDerivedPath     = "/opt/ufsrv/ufsrvcorelib/etc/secrets/ufsrv_secret_key";
constexpr const char *kCompiledDefault = "/opt/ufsrv/etc/secrets/ufsrv_secret_key";

class ConfigCliDefaultsTests : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { TestBootstrapConfigStores(); }

  void SetUp() override {
    TestResetConfigStoreDefaults();
    TestFakeSetCredentialsDirectory(nullptr);
  }
};

TEST_F(ConfigCliDefaultsTests, ConfigDirPathReachesCredentialReader) {
  CommandLineArgsConfig cli = {};
  cli.config_dir = kConfigDir;

  ConfigBootstrapCliDefaults(&cli);
  ConfigFinalizeSensitiveConfig();

  EXPECT_STREQ(TestFakeCredentialPathRead(), kDerivedPath);
}

TEST_F(ConfigCliDefaultsTests, CompiledDefaultReachesCredentialReaderWithoutConfigDir) {
  CommandLineArgsConfig cli = {};

  ConfigBootstrapCliDefaults(&cli);
  ConfigFinalizeSensitiveConfig();

  EXPECT_STREQ(TestFakeCredentialPathRead(), kCompiledDefault);
}

TEST_F(ConfigCliDefaultsTests, CredentialsDirectoryWinsOverConfigDir) {
  CommandLineArgsConfig cli = {};
  cli.config_dir = kConfigDir;
  TestFakeSetCredentialsDirectory("/run/credentials/ufsrvcorelib.service");

  ConfigBootstrapCliDefaults(&cli);
  ConfigFinalizeSensitiveConfig();

  EXPECT_STREQ(TestFakeCredentialPathRead(), "/run/credentials/ufsrvcorelib.service/ufsrv_secret_key");
}

}
