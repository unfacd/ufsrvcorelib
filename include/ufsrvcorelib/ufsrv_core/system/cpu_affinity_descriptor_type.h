
/**
 * @file cpu_affinity_descriptor_type.h
 * @brief cpu_affinity_descriptor_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 9/09/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_CPU_AFFINITY_DESCRIPTOR_TYPE_H
#define UFSRVCORELIB_CPU_AFFINITY_DESCRIPTOR_TYPE_H

#include <stddef.h>

typedef struct CpuAffinityDescriptor CpuAffinityDescriptor;

struct CpuAffinityDescriptor
{
  size_t listener_worker_pool_sz; ///< primary listener thread
  size_t worker_pool_sz;  ///< main worker threads (in ufsrv class servers that maybe combination of session and ufsrv worker threads)
  size_t aux_worker_pool_sz;  ///< individual workers: TimerThread, Fileloader etc...
};

#endif //UFSRVCORELIB_CPU_AFFINITY_DESCRIPTOR_TYPE_H
