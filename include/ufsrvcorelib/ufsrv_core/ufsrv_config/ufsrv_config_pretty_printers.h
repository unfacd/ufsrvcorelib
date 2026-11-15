/**
 * @file ufsrv_config_pretty_printers.h
 * @brief ufsrv_config_pretty_printers
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 30/08/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_UFSRV_CONFIG_PRETTY_PRINTERS_H
#define UFSRVCORELIB_UFSRV_CONFIG_PRETTY_PRINTERS_H

#include <uflib/buffer_descriptor/buffer_descriptor_type.h>

#include "ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_field_descriptor.h"

PUBLIC_API int JsonFormatConfigStore(UfsrvConfigFieldEntry *table, size_t table_sz, enum PrettyPrintScope scope, BufferDescriptor *buffer_descriptor_provided);
PUBLIC_API int UfsrvConfigFieldTablePrettyPrint(UfsrvConfigFieldEntry *table, size_t table_sz, enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *value_slots, BufferDescriptor *out);

#endif //UFSRVCORELIB_UFSRV_CONFIG_PRETTY_PRINTERS_H
