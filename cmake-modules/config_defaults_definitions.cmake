
#provide defaults for configuration items which the user did not pass value for via -D
#
#IMPORTANT dont forget to add corresponding entries in config_ufsrvcorelib.h.in CONFIG_DEFAULT_UFSRV_SECRET_KEY -> #define CONFIG_DEFAULT_UFSRV_SECRET_KEY "@CONFIG_DEFAULT_UFSRV_SECRET_KEY@"
#
if(NOT DEFINED CONFIG_DEFAULT_MAX_CORES)
    set(CONFIG_DEFAULT_MAX_CORES "256" CACHE STRING "Default value for theoretical max number of supported cpu cores.")
endif()

if(NOT DEFINED CONFIG_DEFAULT_CONFIGFILE_VALIDATOR)
    set(CONFIG_DEFAULT_CONFIGFILE_VALIDATOR "core_schema_validator.lua" CACHE STRING "Lua schema validation script for lua-base configuration files")
endif()

if(NOT DEFINED CONFIG_DEFAULT_CONFIGFILE_CORE_SCHEMA)
    set(CONFIG_DEFAULT_CONFIGFILE_CORE_SCHEMA "core_schema_ufsrv.lua" CACHE STRING "Lua schema definition for lua-base configuration files")
endif()

if(NOT DEFINED CONFIG_DEFAULT_CONFIGFILE_ADJUNCT_SCHEMA)
    set(CONFIG_DEFAULT_CONFIGFILE_ADJUNCT_SCHEMA "adjunct_schema_ufsrv.lua" CACHE STRING "Lua adjunct schema definition for lua-base configuration files")
endif()

if(NOT DEFINED CONFIG_DEFAULT_UFSRV_SECRET_KEY)
    set(CONFIG_DEFAULT_UFSRV_SECRET_KEY "ufsrv_secret_key" CACHE STRING "Application-wide key")
endif()

if(NOT DEFINED CONFIG_DEFAULT_UFSRV_SECRET_KEY_SZ)
    set(CONFIG_DEFAULT_UFSRV_SECRET_KEY_SZ "256" CACHE STRING "Application-wide key size in bytes")
endif()

if(NOT DEFINED CONFIG_DEFAULT_UFSRV_SECRETS_DIRECTORY)
    set(CONFIG_DEFAULT_UFSRV_SECRETS_DIRECTORY "secrets" CACHE STRING "Directory store location for application-wide secrets")
endif()


