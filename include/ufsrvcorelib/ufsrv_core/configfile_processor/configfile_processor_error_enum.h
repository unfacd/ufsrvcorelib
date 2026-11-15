/**
 * @file configfile_processor_error_enum.h
 * @brief Error codes for the configfile_processor module.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_ERROR_ENUM_H
#define UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_ERROR_ENUM_H

/*!
 * Error codes returned by configfile_processor operations.
 *
 * Every accessor function sets the handle's last_error field.  Callers
 * check ConfFileGetLastError() after any operation to determine success
 * or the failure category.
 */
typedef enum ConfFileError {
  CONF_FILE_OK                   = 0,  ///< Operation succeeded
  CONF_FILE_ERR_LUA              ,  ///< Lua runtime error (load / parse / execute)
  CONF_FILE_ERR_SCOPE_NOT_FOUND  ,  ///< Named scope table does not exist or is not a table
  CONF_FILE_ERR_KEY_NOT_FOUND    ,  ///< Key does not exist in scope (resolves to nil)
  CONF_FILE_ERR_TYPE_MISMATCH    ,  ///< Value exists but is not the expected Lua type
  CONF_FILE_ERR_ARGUMENT         ,  ///< NULL handle, scope, key, or out-of-bounds index
  CONF_FILE_ERR_MEMORY           ,  ///< malloc / calloc / strdup failure
  CONF_FILE_ERR_NOT_INITIALISED  ,  ///< Handle is NULL or lua_State was never created
  CONF_FILE_ERR_INDEX_OUT_OF_RANGE,  ///< Array / iteration index out of bounds
  CONF_FILE_LUA_ERR_LUA_STATE       ///< lua state initialisation error
} ConfFileError;

#endif /* UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_ERROR_ENUM_H */
