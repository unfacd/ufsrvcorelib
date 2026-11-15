
/**
 * @file utils_runtime.h
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

#ifndef UFSRVCORELIB_UTILS_RUNTIME_H
#define UFSRVCORELIB_UTILS_RUNTIME_H

#include <stdbool.h>
#include "ufsrvcorelib_defs.h"

#include <stddef.h>

#include "ufsrvcorelib/ufsrv_core/system/ufsrv_secret_key_state_enum.h"
#include "ufsrvcorelib/ufsrv_core/system/ufsrv_secret_key_type.h"

/*!
 * Return whether this process is being orchestrated by systemd.
 *
 * Detected via the INVOCATION_ID environment variable, which systemd sets for
 * every service it starts (system and user services alike).  A process started
 * manually, or under another supervisor (SysV init, runit/docker), lacks it.
 * @note see UfsrvRuntimeIsSystemdService() for an alternative API based approach.
 *
 * @return true if the process is running under systemd, false otherwise.
 */
PUBLIC_API bool UfsrvRuntimeIsSystemdOrchestrated(void);

/**
 * @brief Determines if the currently running process is managed by a systemd service unit using sd API.
 *
 * @details This function queries the systemd manager via sd_pid_get_unit() to retrieve
 *          the unit name of the calling process (PID 0). It then checks if the retrieved
 *          unit name ends with the ".service" suffix. This is the definitive way to
 *          distinguish between a process launched via `systemctl start` and a process
 *          launched manually from a shell, even if systemd is running as PID 1.
 *
 * @return true  If the process is actively running inside a systemd `.service` unit.
 * @return false If the process was started manually, is part of a scope/slice, or if
 *               sd_pid_get_unit() fails (e.g., systemd is not the init system).
 *
 * @note See UfsrvRuntimeIsSystemdOrchestrated() for an alternative, lightweight approach.
 * @note This function requires linking against libsystemd (e.g., `-lsystemd`).
 * @note Memory allocated internally by sd_pid_get_unit() is properly freed within
 *       this function; the caller does not need to manage any memory.
 */
PUBLIC_API bool UfsrvRuntimeIsSystemdService();

/*!
 * Return whether the systemd credential store is available to this process.
 *
 * True when systemd has mounted a credentials directory for the unit (the unit
 * declares LoadCredential=/SetCredential=/LoadCredentialEncrypted=) and exposed
 * it via the CREDENTIALS_DIRECTORY environment variable.
 *
 * @return true if $CREDENTIALS_DIRECTORY is set and non-empty, false otherwise.
 */
PUBLIC_API bool UfsrvRuntimeIsSystemdCredentialsDirectoryAvailable(void);

/**
 * @brief Retrieves the filesystem path to the systemd credentials directory.
 *
 * @details When a service is orchestrated by systemd with the `LoadCredential=` or
 *          `SetCredential=` directives, systemd creates a temporary, isolated directory
 *          containing those credentials and exposes its path via the `CREDENTIALS_DIRECTORY`
 *          environment variable. This function safely retrieves that path.
 *
 * @return const char * Pointer to the credential directory path on success.
 * @return NULL         If the environment variable is not set or empty. An error
 *                      is automatically logged to syslog in this case.
 *
 * @note The returned pointer references the process environment. The caller
 *       MUST NOT free() or modify the returned string.
 */
PUBLIC_API const char * UfsrvRuntimeGetSystemdCredentialsDirectory(void);

/*!
 * Load the secret key from the file at .secret_key_path.
 *
 * The key file must contain a single line of key material (hex or text),
 * NUL-terminated into .secret_key_serialised (at most UFSRV_SECRET_KEY_SZ-1 characters).
 * The caller sets .secret_key_path before calling; the caller retains ownership
 * of .secret_key_path and .secret_key_serialised (both are fixed arrays).
 *
 * @param secret_key_provided On input: .secret_key_path set.  On success:
 *   .secret_key populated with the key material.
 * @param secret_key_path_and_filename
 * @return UFSRV_SECRET_KEY_STATE_OK on success, otherwise the failure state.
 */
PUBLIC_API UfsrvSecretKeyState
UfsrvRuntimeProvideSecretKey(UfsrvSecretKey *secret_key_provided, const char *secret_key_path_and_filename);

#endif //UFSRVCORELIB_UTILS_RUNTIME_H
