/**
 * @file shared_tables_type.h
 * @brief Read-only registries every shard may consult.
 *
 * The fields land with the engine. Until then the type is opaque on purpose:
 * shards receive a const pointer and the registries themselves are owned by
 * the main thread.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRVCORELIB_SHARED_TABLES_TYPE_H
#define UFSRVCORELIB_SHARED_TABLES_TYPE_H

typedef struct SfuSharedTables SfuSharedTables;

#endif /* UFSRVCORELIB_SHARED_TABLES_TYPE_H */
