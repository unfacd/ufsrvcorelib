/*
 * ufsrv_config_stubs_as_string_generated.c — GENERATED (one-off), DO NOT
 * EDIT.  Rename (drop _generated) once the stubs are hand-maintained.
 */

#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <uflib/adt/adt_distinct_array_type.h>
#include <uflib/collection_descriptor_type.h>
#include <uflib/utils_collection_str.h>
#include <uflib/logger/logger.h>

#include <stdbool.h>

#include "ufsrv_config_stubs_as_string_priv.h"
#include <ufsrvcorelib/ufsrv_core/system/cpu_affinity_policy_enum.h>
#include <ufsrvcorelib/ufsrv_core/system/server_run_mode_enum.h>
#include <ufsrvcorelib/ufsrv_core/system/server_process_mode_enum.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_store_operating_state_enum.h>
#include "ufsrv_config_generic_callbacks_priv.h"

BufferDescriptor *
UfsrvConfigConfFileStoreOperatingStateAsString_enum_UfsrvConfigStoreOperatingState(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  if (!buffer_provided || !value_ptr) return NULL;
  UfsrvConfigStoreOperatingState state = (UfsrvConfigStoreOperatingState)value_ptr->stored_value.int_val;
  if (BufferDescriptorAppendFormatted(buffer_provided, "%s", UfsrvConfigStoreOperatingStateToLiteral(state)) != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }
  return buffer_provided;
}

BufferDescriptor *
UfsrvConfigRuntimeStoreOperatingStateAsString_enum_UfsrvConfigStoreOperatingState(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  if (!buffer_provided || !value_ptr) return NULL;
  UfsrvConfigStoreOperatingState state = (UfsrvConfigStoreOperatingState)value_ptr->stored_value.int_val;
  if (BufferDescriptorAppendFormatted(buffer_provided, "%s", UfsrvConfigStoreOperatingStateToLiteral(state)) != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }
  return buffer_provided;
}

BufferDescriptor *
UfsrvConfigServerProcessModeAsString_enum_ServerProcessMode(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  if (!buffer_provided || !value_ptr) return NULL;
  enum ServerProcessMode mode = (enum ServerProcessMode)value_ptr->stored_value.int_val;
  if (BufferDescriptorAppendFormatted(buffer_provided, "%s", ServerProcessModeToLiteral(mode)) != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }
  return buffer_provided;
}

BufferDescriptor *
UfsrvConfigServerRunModeAsString_enum_ServerRunMode(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  if (!buffer_provided || !value_ptr) return NULL;
  ServerRunMode mode = (ServerRunMode)value_ptr->stored_value.int_val;
  if (BufferDescriptorAppendFormatted(buffer_provided, "%s", ServerRunModeToLiteral(mode)) != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }
  return buffer_provided;
}

BufferDescriptor *
UfsrvConfigServerCpuAffinityAsString_enum_CpuAffinityPolicy(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  if (!buffer_provided || !value_ptr) return NULL;
  CpuAffinityPolicy policy = (CpuAffinityPolicy)value_ptr->stored_value.int_val;
  if (BufferDescriptorAppendFormatted(buffer_provided, "%s",CpuAffinityPolicyToLiteral(policy)) != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }
  return buffer_provided;
}

BufferDescriptor *
UfsrvConfigLuaHandleAsString_lua_State_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  if (!buffer_provided || !value_ptr) return NULL;

  if (BufferDescriptorAppendFormatted(buffer_provided, "lua_handle:%p", (unsigned long long)value_ptr->stored_value.ptr_val) != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }
  return buffer_provided;
}

#include "ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend.h"
BufferDescriptor *
UfsrvConfigInstrumentationBackendAsString_InstrumentationBackend_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  if (!buffer_provided || !value_ptr) return NULL;

  InstrumentationBackend *backend = (InstrumentationBackend *)value_ptr->stored_value.ptr_val;
  if (IS_PRESENT(backend) && BufferDescriptorAppendFormatted(buffer_provided, "%s", InstrumentationBackendAsString(backend, buffer_provided)) != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }
  return buffer_provided;
}

BufferDescriptor *
UfsrvConfigInstrumentationBackendServerAsString_struct_sockaddr_in(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for struct sockaddr_in. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigSessionsDelegatorAsString_SessionsDelegator_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for SessionsDelegator *. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigWorkDelegatorPipeAsString_InstanceHolderForSession_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for InstanceHolderForSession *. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigPipefdsAsString_int_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for int *. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigSslCommandConsoleAsString_SSL_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for SSL *. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigSslCtxAsString_SSL_CTX_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for SSL_CTX *. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigSslUserCtxAsString_SSL_CTX_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for SSL_CTX *. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigThreadHandleTimerDelegatorAsString_pthread_t(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for pthread_t. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigFileLoaderServiceAsString_FileLoaderServiceDescriptor(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for UfsrvConfigFileLoaderService. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigOauthSettingsAsString_UfsrvConfigOauth(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for UfsrvConfigOauth. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigUfsrvResultAsString_UFSRVResult(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for UFSRVResult. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigThreadsSubsystemUfsrvOnceAsString_pthread_once_t(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for pthread_once_t. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

BufferDescriptor *
UfsrvConfigSslMutexesAsString_pthread_mutex_t_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  /* TODO: hand-author string formatting for pthread_key_t. */
  (void)field_index;
  (void)value_ptr;
  (void)buffer_provided;
  return NULL;
}

/* Shared renderer for DistinctArray-backed fields — emits the stored
 * (NUL-terminated) entries as a JSON array, e.g. ["aa:bb","cc:dd"]. */
static BufferDescriptor *
sDistinctArrayAsString(UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  if (!buffer_provided || !value_ptr) return NULL;

  DistinctArray *da = (DistinctArray *)value_ptr->stored_value.blob_val;
  if (!da) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }

  size_t count = da->stored_value_idx ? da->stored_value_idx->size : 0;

  if (BufferDescriptorAppendFormatted(buffer_provided, "[") != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }

  bool first = true;
  for (size_t i = 0; i < count; i++) {
    uint8_t *item = da->stored_value_idx->value_index[i];
    if (!item) continue;
    if (!first && BufferDescriptorAppendFormatted(buffer_provided, ",") != 0) {
      BufferDescriptorRelease(buffer_provided);
      return NULL;
    }
    if (BufferDescriptorAppendFormatted(buffer_provided, "\"%s\"", (char *)item) != 0) {
      BufferDescriptorRelease(buffer_provided);
      return NULL;
    }
    first = false;
  }

  if (BufferDescriptorAppendFormatted(buffer_provided, "]") != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }

  return buffer_provided;
}

BufferDescriptor *
UfsrvConfigSslCommandConsoleAllowListGodAsString_DistinctArray(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  return sDistinctArrayAsString(value_ptr, buffer_provided);
}

BufferDescriptor *
UfsrvConfigSslCommandConsoleAllowListBrowserAsString_DistinctArray(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  return sDistinctArrayAsString(value_ptr, buffer_provided);
}

BufferDescriptor *
UfsrvConfigSslCommandConsoleDenyListAsString_DistinctArray(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  return sDistinctArrayAsString(value_ptr, buffer_provided);
}

/* Render a CollectionDescriptor of strings as a JSON array through uflib's
 * describe interface (escaping included). */
static BufferDescriptor *
sCollectionDescriptorAsString(UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  if (!buffer_provided || !value_ptr) return NULL;

  const CollectionDescriptor *collection = (const CollectionDescriptor *)value_ptr->stored_value.blob_val;
  if (!collection) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }

  char *json = CollectionFromStringDescribeAsJson(collection);
  if (!json) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }

  int rc = BufferDescriptorAppendFormatted(buffer_provided, "%s", json);
  free(json);
  if (rc != 0) {
    BufferDescriptorRelease(buffer_provided);
    return NULL;
  }

  return buffer_provided;
}

BufferDescriptor *
UfsrvConfigFileloaderBlackListedFilesRegistryAsString_CollectionDescriptor(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  return sCollectionDescriptorAsString(value_ptr, buffer_provided);
}

BufferDescriptor *
UfsrvConfigFileloaderFilesRegistryAsString_CollectionDescriptor(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  (void)field_index;
  return sCollectionDescriptorAsString(value_ptr, buffer_provided);
}

BufferDescriptor *
UfsrvConfigUfLoggerAsString_UfLogger_pointer(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)
{
  UfLogger *logger;

  (void)field_index;
  if (!buffer_provided || !value_ptr) return NULL;

  logger = (UfLogger *)value_ptr->stored_value.ptr_val;
  if (!IS_PRESENT(logger)) return buffer_provided;

  return UfLoggerDescribeConfiguration(logger, buffer_provided);
}
