/**
 * @file service_file_loader.h
 * @brief service_file_loader
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 28/10/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_SERVICE_FILE_LOADER_H
#define UFSRVCORELIB_SERVICE_FILE_LOADER_H

#include <uflib/uflib_defs.h>
#include <ufsrvcorelib/ufsrv_core/service/file_loader/file_loader_service_descriptor_type.h>

/**
 * @brief Start the file loader service from @p file_loader_service_descriptor.
 *
 * Initialises the service with the descriptor's sizes, logger, optional
 * blacklist and preloads its @c files_registry entries.
 *
 * @note the is_enabled gating is the caller's responsibility.
 *
 * @param file_loader_service_descriptor Pipeline configuration supplied by the owning app.
 * @return Always NULL (pthread start-routine signature).
 */
PUBLIC_API void *
ThreadFileLoaderService(FileLoaderServiceDescriptor *file_loader_service_descriptor);

#endif //UFSRVCORELIB_SERVICE_FILE_LOADER_H
