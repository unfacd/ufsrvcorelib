/**
 * @file utils_runtime.c
 * @brief utils_runtime
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 1/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <systemd/sd-login.h>
#include "ufsrvcorelib/ufsrv_core/system/utils_runtime.h"

#include <standard_defs.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/syslog.h>

PUBLIC_API bool
UfsrvRuntimeIsSystemdOrchestrated(void)
{
  const char *invocation_id = getenv("INVOCATION_ID");
  return invocation_id && *invocation_id;
}

PUBLIC_API bool
UfsrvRuntimeIsSystemdService() {
  char *unit_name = NULL;
  const int ret = sd_pid_get_unit(0, &unit_name);   // Pass 0 to check the current process (PID 0 means "self")

  if (ret < 0 || unit_name == NULL) {
    return false;
  }

  bool is_service = (strstr(unit_name, ".service") != NULL);  // Check if the unit is a service (e.g., "myapp.service")

  free(unit_name);

  return is_service;
}

PUBLIC_API bool
UfsrvRuntimeIsSystemdCredentialsDirectoryAvailable(void)
{
  const char *dir = getenv("CREDENTIALS_DIRECTORY");
  return dir && *dir;
}

PUBLIC_API const char *
UfsrvRuntimeGetSystemdCredentialsDirectory(void)
{
  const char *dir = getenv("CREDENTIALS_DIRECTORY");
  if (IS_STR_LOADED(dir)) {
    return dir;
  }

  syslog(LOG_ERR, ">>> %s: CREDENTIALS_DIRECTORY not set — systemd credential store unavailable", __func__);
  return NULL;
}

PUBLIC_API UfsrvSecretKeyState
UfsrvRuntimeProvideSecretKey(UfsrvSecretKey *secret_key_provided, const char *secret_key_path_and_filename)
{
  if (!IS_STR_LOADED(secret_key_path_and_filename))
    return UFSRV_SECRET_KEY_STATE_NO_PATH;

  secret_key_provided->secret_key_serialised[0] = '\0';

  FILE *f = fopen(secret_key_path_and_filename, "r");
  if (!f)
    return UFSRV_SECRET_KEY_STATE_FILE_NOT_FOUND;

  if (fgets(secret_key_provided->secret_key_serialised, SECRET_KEY_BYTES, f) == NULL) {
    fclose(f);
    return UFSRV_SECRET_KEY_STATE_EMPTY;
  }
  fclose(f);

  secret_key_provided->secret_key_serialised[strcspn((const char *)secret_key_provided->secret_key_serialised, "\r\n")] = '\0';

  if (!secret_key_provided->secret_key_serialised[0])
    return UFSRV_SECRET_KEY_STATE_EMPTY;

  return UFSRV_SECRET_KEY_STATE_OK;
}