/**
 * @file ufsrv_thread_descriptor_type.h
 * @brief Thread type enum and per-thread descriptor for CPU affinity assignment.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_THREAD_DESCRIPTOR_TYPE_H
#define UFSRV_THREAD_DESCRIPTOR_TYPE_H

#include <stddef.h>

/*!
 * Thread types known to the CPU affinity system.
 *
 * The pinning strategy treats each type differently:
 * - LISTENER / DELEGATOR are control-plane threads pinned to dedicated cores
 *   (0 and 1 respectively).
 * - WORKERs are the hot path — pinned under all strategies.
 * - AUX threads are typically left unpinned (OS decides).
 */
typedef enum {
  UFSRV_THREAD_LISTENER  = 0,  ///< accept() loop — core 0
  UFSRV_THREAD_DELEGATOR = 1,  ///< epoll dispatch — core 1
  UFSRV_THREAD_WORKER    = 2,  ///< main worker pool — hot path (pinned)
  UFSRV_THREAD_AUX       = 3   ///< aux workers (timer/fileloader) — unpinned
} UfsrvThreadType;

/*!
 * Assignment of one thread or thread class to a CPU core.
 *
 * For the control-plane threads (LISTENER, DELEGATOR), @p count is the pool
 * size (typically 1).  For the WORKER pool, @p count reflects the number of
 * threads assigned to this core under grouped strategies.
 *
 * @p core is (size_t)-1 when the thread is not pinned (OS scheduler decides).
 */
typedef struct {
  UfsrvThreadType  type;   ///< what kind of thread
  size_t           core;   ///< assigned core, or (size_t)-1 if unpinned
  size_t           count;  ///< how many threads of this type
} UfsrvThreadDescriptor;

#endif /* UFSRV_THREAD_DESCRIPTOR_TYPE_H */
