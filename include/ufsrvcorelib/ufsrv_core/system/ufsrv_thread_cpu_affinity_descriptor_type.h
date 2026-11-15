/**
 * @file ufsrv_thread_cpu_affinity_descriptor_type.h
 * @brief Top-level CPU affinity layout descriptor — returned by UfsrvCpuAffinityInit().
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_THREAD_CPU_AFFINITY_DESCRIPTOR_TYPE_H
#define UFSRV_THREAD_CPU_AFFINITY_DESCRIPTOR_TYPE_H

#include <stddef.h>
#include "cpu_affinity_policy_enum.h"
#include "ufsrv_thread_descriptor_type.h"

/*!
 * Complete CPU affinity layout — returned by UfsrvCpuAffinityInit().
 *
 * The server uses @p listener_worker_pool_sz, @p worker_pool_sz and
 * @p aux_worker_pool_sz to determine how many threads to create.  These may
 * differ from the user-requested counts when the policy caps pools to
 * available cores.
 *
 * Allocated as a single slab by Init().  The caller frees the entire
 * descriptor with a single free().
 *
 * @p thread_types is a flexible array member — its length is given by
 * @p thread_types_count.
 */
typedef struct {
  size_t                    cores_sz;               ///< logical CPUs on this machine
  size_t                    listener_worker_pool_sz;///< effective listener pool
  size_t                    worker_pool_sz;         ///< effective main worker pool (may be capped)
  size_t                    aux_worker_pool_sz;     ///< effective aux pool (unpinned)
  CpuAffinityPolicy         policy;                 ///< active policy
  size_t                    thread_types_count;     ///< entries in thread_types[]
  UfsrvThreadDescriptor     thread_types[];         ///< flexible array member
} UfsrvThreadCpuAffinityDescriptor;

#endif /* UFSRV_THREAD_CPU_AFFINITY_DESCRIPTOR_TYPE_H */
