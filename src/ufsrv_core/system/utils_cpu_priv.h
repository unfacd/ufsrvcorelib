/**
 * @file utils_cpu_priv.h
 * @brief Private internals for the CPU affinity module.
 *
 * Copyright (C) 2015-2026 unfacd works
 */

#ifndef UFSRV_CORE_SYSTEM_UTILS_CPU_PRIV_H
#define UFSRV_CORE_SYSTEM_UTILS_CPU_PRIV_H

#include <stddef.h>
#include <stdbool.h>
#include "cpu_affinity_policy_enum.h"
#include "ufsrv_thread_cpu_affinity_descriptor_type.h"
#include "cpu_affinity_descriptor_type.h"

/* ── CPU count override (dependency injection for testing) ──────────────── */

/**
 * @brief Override the CPU count returned by the module.
 *
 * When @p count is >= 0, all internal queries return this value instead
 * of calling sysconf(_SC_NPROCESSORS_ONLN).  Set to a negative value
 * (e.g. -1) to restore the default behaviour.
 *
 * Called only during testing.
 */
void UfsrvCpuAffinityFakeCpuCount(long count);

/* ── Internal state access (exposed for tests only) ─────────────────────── */

/** Currently active policy (set by Init, read by Pin). */
CpuAffinityPolicy UfsrvCpuAffinityActivePolicy(void);

/** Effective main worker pool size after Init. */
size_t UfsrvCpuAffinityEffectiveWorkerPool(void);

/** Effective aux pool size after Init. */
size_t UfsrvCpuAffinityEffectiveAuxWorkers(void);

/* ── Recommendation (industry best-practice heuristic) ──────────────────── */

/*!
 * Result of computing a CPU-affinity recommendation for a given host.
 *
 * @p worker_pool_sz is the advised count for the hot path (may be capped
 * under oversubscription); @p listener_worker_pool_sz / @p aux_worker_pool_sz
 * are returned unchanged (listener is reserved on core 0; aux is unpinned).
 */
typedef struct {
  CpuAffinityPolicy policy;                  ///< recommended policy
  size_t            listener_worker_pool_sz; ///< advised listener pool
  size_t            worker_pool_sz;          ///< advised main worker pool (may be capped)
  size_t            aux_worker_pool_sz;      ///< advised aux pool (unpinned)
  bool              oversubscribed;          ///< main worker pool > pinnable cores
  long              cpu_count;               ///< logical CPUs observed
  long              pinnable_cores;          ///< cores available for the main worker pool
} UfsrvCpuAffinityAdvice;

/**
 * @brief Compute a recommended policy + worker counts for a given core count.
 *
 * Pure, deterministic, and independent of the module's Init state — exposed
 * for test injection (cpu_count is passed explicitly).  @p desc may be NULL,
 * which is treated as all-zero pools.
 */
void UfsrvCpuAffinityComputeAdvice(long cpu_count,
                                   const CpuAffinityDescriptor *desc,
                                   UfsrvCpuAffinityAdvice *advice_out);

#endif /* UFSRV_CORE_SYSTEM_UTILS_CPU_PRIV_H */
