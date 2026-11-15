/**
 * @file configfile_processor_type.h
 * @brief Opaque handle type for the configfile_processor module.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_TYPE_H
#define UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_TYPE_H

/*!
 * Opaque handle for the configfile processor.
 *
 * Allocated by ConfFileInit(), destroyed by ConfFileDestroy().  The handle
 * owns the lua_State and internal error state.  Consumers never dereference
 * this pointer or include lua.h — they interact exclusively through the
 * typed accessor functions declared in configfile_processor.h.
 */
typedef struct ConfFileOpaque *ConfFile;

#endif /* UFSRV_CORE_CONFIGFILE_PROCESSOR_CONFIGFILE_PROCESSOR_TYPE_H */
