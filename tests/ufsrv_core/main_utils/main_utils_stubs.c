#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <config_ufsrvcorelib.h>

#include <ufsrvcorelib/ufsrv_core/configfile_processor/configfile_processor.h>
#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend.h>
#include <ufsrvcorelib/ufsrv_core/system/ufsrv_secret_key_type.h>
#include <ufsrvcorelib/ufsrv_core/system/utils_runtime.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_secret.h>

#define TEST_FAKE_PATH_SZ 512

static const char *fake_credentials_directory;
static char        fake_credential_path_read[TEST_FAKE_PATH_SZ];

void
TestFakeSetCredentialsDirectory(const char *directory)
{
  fake_credentials_directory = directory;
}

const char *
TestFakeCredentialPathRead(void)
{
  return fake_credential_path_read;
}

const char *
UfsrvRuntimeGetSystemdCredentialsDirectory(void)
{
  return fake_credentials_directory;
}

int
SecretConfigReadKeyFromCredential(unsigned char key[SECRET_KEY_BYTES], const char *credentials_directory_and_filename)
{
  memset(fake_credential_path_read, 0, sizeof(fake_credential_path_read));
  if (credentials_directory_and_filename)
    snprintf(fake_credential_path_read, sizeof(fake_credential_path_read), "%s", credentials_directory_and_filename);

  memset(key, 0, SECRET_KEY_BYTES);
  return 0;
}

int
DropRootPrivileges(const char *username, const char *chroot_dir, void (*on_success)(void))
{
  (void)username;
  (void)chroot_dir;
  (void)on_success;
  return 0;
}

ConfFile
ConfFileValidate(const ConfigFileValidatorDescriptor *config_file_validator_descriptor)
{
  (void)config_file_validator_descriptor;
  return NULL;
}

int
ConfFileTransferToConfigValues(ConfFile cp, const ConfFileSymbolTableEntry config_file_symbols_map[], const void *associative_lookup_map, size_t associative_lookup_map_sz, void (*on_error)(const ConfFileSymbolTableEntry *entry, const char *message))
{
  (void)cp;
  (void)config_file_symbols_map;
  (void)associative_lookup_map;
  (void)associative_lookup_map_sz;
  (void)on_error;
  return 0;
}

void
ConfFileDestroy(ConfFile cp)
{
  (void)cp;
}

const char *
ConfigFileCoreSchema(void)
{
  return "core.lua";
}

const char *
ConfigFileSchemaValidator(void)
{
  return "validator.lua";
}

BufferDescriptor *
InstrumentationBackendAsString(const InstrumentationBackend *backend, BufferDescriptor *buffer_provided)
{
  (void)backend;
  return buffer_provided;
}
