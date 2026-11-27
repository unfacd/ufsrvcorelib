#ifdef HAVE_CONFIG_UFNETCORELIB_H
# include "config_ufnetcorelib.h"
#endif

#include <stdbool.h>

#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>

#include "generated/data_ufsrv_config_field_tables_generated.c"

void
TestBootstrapConfigStores(void)
{
  UfsrvConfigRuntimeStoreInit(ufsrv_runtime_field_table, ufsrv_runtime_field_defaults_table);
  UfsrvConfigInflateRuntimeStore();
  UfsrvConfigConfFileStoreInit(ufsrv_config_field_table, ufsrv_config_field_defaults_table);
  UfsrvConfigInflateConfigStore();
  UfsrvConfigRuntimeStorePopulateDefaults(ufsrv_runtime_field_defaults_table, false);
  UfsrvConfigConfFileStorePopulateDefaults(ufsrv_config_field_defaults_table, false);
}

void
TestResetConfigStoreDefaults(void)
{
  UfsrvConfigRuntimeStorePopulateDefaults(ufsrv_runtime_field_defaults_table, false);
  UfsrvConfigConfFileStorePopulateDefaults(ufsrv_config_field_defaults_table, false);
}
