/**
 * @file config_snapshot_type.h
 * @brief Immutable configuration view shared by every shard.
 *
 * The fields land with the engine. Until then the type is opaque on purpose:
 * a shard only ever holds a const pointer to the snapshot the main thread
 * built, and no consumer needs its layout.
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

#ifndef UFSRVCORELIB_CONFIG_SNAPSHOT_TYPE_H
#define UFSRVCORELIB_CONFIG_SNAPSHOT_TYPE_H

typedef struct SfuConfigSnapshot SfuConfigSnapshot;

#endif /* UFSRVCORELIB_CONFIG_SNAPSHOT_TYPE_H */
