/**
 * @file ufsrv_config_pretty_printers.h
 * @brief ufsrv_config_pretty_printers
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_UFSRV_CONFIG_PRETTY_PRINTERS_H
#define UFSRVCORELIB_UFSRV_CONFIG_PRETTY_PRINTERS_H

#include "ufsrv_core/ufsrv_config/ufsrv_config_field_descriptor.h"
#include "ufsrv_core/ufsrv_config/ufsrv_config_value_type.h"

char * PrettyPrint_enum(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen, enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided);

#endif //UFSRVCORELIB_UFSRV_CONFIG_PRETTY_PRINTERS_H
