/**
 * @file utils_cpu.h
 * @brief Public API for the CPU affinity subsystem.
 *
 * Isolated module under ufsrvcorelib.  Zero dependency on host config —
 * the consumer supplies the policy and desired worker counts.  Dependency
 * surface is POSIX + stdlib only.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRV_CORE_SYSTEM_UTILS_CPU_H
#define UFSRV_CORE_SYSTEM_UTILS_CPU_H

#include <stdbool.h>
#include "ufsrvcorelib_defs.h"
#include "cpu_affinity_policy_enum.h"
#include "ufsrv_thread_cpu_affinity_descriptor_type.h"
#include "ufsrv_thread_descriptor_type.h"
#include "cpu_affinity_descriptor_type.h"

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the CPU affinity subsystem.
 *
 * The consumer supplies the policy and the desired pool sizes via @p desc.
 * The module queries cpu_count, computes the layout, stores it internally,
 * and returns a descriptor so the server knows the final thread counts.
 *
 * Must be called once at startup, before any thread calls Pin().
 * Not thread-safe during initialisation.
 *
 * @param policy  User-chosen policy (from config, parsed by host)
 * @param desc    Desired pool sizes (listener/worker/aux), or NULL (all-zero)
 * @return Heap-allocated descriptor (single slab, flexible array member).
 *         The caller owns the pointer and must free() it once.
 *         Returns NULL on allocation failure.
 *
 * @code{.c}
 * CpuAffinityDescriptor in = { .listener_worker_pool_sz = 1,
 *                              .worker_pool_sz = 4, .aux_worker_pool_sz = 2 };
 * UfsrvThreadCpuAffinityDescriptor *out =
 *     UfsrvCpuAffinityInit(CPU_AFFINITY_MANAGED, &in);
 * free(out);
 * @endcode
 */
PUBLIC_API UfsrvThreadCpuAffinityDescriptor *
UfsrvCpuAffinityInit(CpuAffinityPolicy policy, const CpuAffinityDescriptor *desc);

/* ── Per-thread pinning ─────────────────────────────────────────────────── */

/**
 * @brief Pin the calling thread to its assigned core.
 *
 * Call from each thread's entry function after UfsrvCpuAffinityInit().
 * No-op if the policy is SYSTEM_OVERRIDE or the thread type has no
 * assigned core.
 *
 * Uses sched_setaffinity(gettid(), ...) internally.
 *
 * @param type       Which thread type this is
 * @param idx        Worker index (0..N-1), only meaningful for WORKER
 */
PUBLIC_API void UfsrvCpuAffinityPin(UfsrvThreadType type, int idx);

/* ── Descriptor accessors ───────────────────────────────────────────────── */

/**
 * @brief Number of logical CPUs on this machine.
 * @param desc  Descriptor from UfsrvCpuAffinityInit()
 */
PUBLIC_API size_t UfsrvThreadCpuAffinityDescriptorCoresSz(
    const UfsrvThreadCpuAffinityDescriptor *desc);

/**
 * @brief Effective listener pool size.
 * @param desc  Descriptor from UfsrvCpuAffinityInit()
 */
PUBLIC_API size_t UfsrvThreadCpuAffinityDescriptorListenerSz(
    const UfsrvThreadCpuAffinityDescriptor *desc);

/**
 * @brief Effective main worker pool size (may differ from user config).
 * @param desc  Descriptor from UfsrvCpuAffinityInit()
 */
PUBLIC_API size_t UfsrvThreadCpuAffinityDescriptorWorkerSz(
    const UfsrvThreadCpuAffinityDescriptor *desc);

/**
 * @brief Effective aux pool size.
 * @param desc  Descriptor from UfsrvCpuAffinityInit()
 */
PUBLIC_API size_t UfsrvThreadCpuAffinityDescriptorAuxSz(
    const UfsrvThreadCpuAffinityDescriptor *desc);

/**
 * @brief Active policy.
 * @param desc  Descriptor from UfsrvCpuAffinityInit()
 */
PUBLIC_API CpuAffinityPolicy UfsrvThreadCpuAffinityDescriptorPolicy(
    const UfsrvThreadCpuAffinityDescriptor *desc);

/**
 * @brief Number of entries in the thread_types array.
 * @param desc  Descriptor from UfsrvCpuAffinityInit()
 */
PUBLIC_API size_t UfsrvThreadCpuAffinityDescriptorThreadTypesCount(
    const UfsrvThreadCpuAffinityDescriptor *desc);

/**
 * @brief Thread type descriptor at index (bounds-checked).
 * @param desc   Descriptor from UfsrvCpuAffinityInit()
 * @param index  Index into the thread_types array
 * @return Pointer to descriptor, or NULL if index is out of bounds
 */
PUBLIC_API const UfsrvThreadDescriptor *
UfsrvThreadCpuAffinityDescriptorThreadTypeAt(
    const UfsrvThreadCpuAffinityDescriptor *desc,
    size_t                                 index);

/* ── Defaults ───────────────────────────────────────────────────────────── */

/**
 * @brief Return safe default configuration values.
 *
 * When the user does not configure server_cpu_affinity or the pool sizes,
 * the server injects these defaults before calling Init().
 *
 * @param policy_out  [out] Default policy (SYSTEM_OVERRIDE)
 * @param desc_out    [out] Default pool sizes (listener/worker/aux), or NULL
 */
PUBLIC_API void UfsrvCpuAffinityDefaults(
    CpuAffinityPolicy      *policy_out,
    CpuAffinityDescriptor  *desc_out);

/* ── Observability ──────────────────────────────────────────────────────── */

/**
 * @brief Number of logical CPUs on this machine (no descriptor needed).
 */
PUBLIC_API int  UfsrvCpuAffinityCpuCount(void);

/**
 * @brief Core assigned to a given thread type/index, or (size_t)-1 if unpinned.
 *
 * Mirrors the assignment applied by UfsrvCpuAffinityPin() without touching
 * the calling thread's affinity.  Returns (size_t)-1 when the active policy
 * is SYSTEM_OVERRIDE or the role has no assigned core.
 *
 * @param type  Which thread type this is
 * @param idx   Worker index (0..N-1), only meaningful for WORKER
 */
PUBLIC_API size_t UfsrvCpuAffinityCoreFor(UfsrvThreadType type, int idx);

/**
 * @brief Serialise a descriptor to a single-line JSON string.
 *
 * The caller owns the returned string (heap-allocated via malloc).
 * Use free() to release.  Returns NULL on allocation failure.
 *
 * @param desc  Descriptor from UfsrvCpuAffinityInit()
 * @return Heap-allocated JSON string, or NULL on failure
 *
 * @code{.c}
 * char *json = UfsrvCpuAffinityDescribe(&desc);
 * if (json) { syslog(LOG_INFO, "CPU affinity: %s", json); free(json); }
 * @endcode
 */
PUBLIC_API char *UfsrvCpuAffinityDescribe(const UfsrvThreadCpuAffinityDescriptor *desc);

/**
 * @brief Recommend a CPU-affinity policy and worker counts for this host.
 *
 * Queries the logical CPU count and applies an industry best-practice
 * heuristic (reserve core 0 = listener and core 1 = delegator; pin the main
 * worker pool 1:1 to the remaining cores; leave aux workers unpinned).  The
 * caller supplies the pool sizes via @p desc — the module
 * never reads host configuration itself.  Returns a human-readable report as
 * a heap-allocated string; the caller owns it and must free() it.  NULL is
 * returned on allocation failure.
 *
 * Intended to be invoked from the server's main() when --cpu-affinity-verify
 * is supplied:
 *
 * @code{.c}
 * if (cfg.cpu_affinity_verify_requested) {
 *   CpuAffinityDescriptor desc = {
 *     .listener_worker_pool_sz = UfsrvConfigListenerWorkersThreadPool(),
 *     .worker_pool_sz          = UfsrvConfigSessionWorkersThreadPool()
 *                                + UfsrvConfigUfsrvWorkersThreadPool(),
 *     .aux_worker_pool_sz      = 0,  // timer / fileloader / etc.
 *   };
 *   char *r = UfsrvCpuAffinityRecommend(&desc);
 *   if (r) { printf("%s", r); free(r); }
 *   return EXIT_SUCCESS;
 * }
 * @endcode
 *
 * @param desc  Pool sizes observed by the caller, or NULL (all-zero)
 * @return Heap-allocated recommendation report, or NULL on failure
 */
PUBLIC_API char *UfsrvCpuAffinityRecommend(const CpuAffinityDescriptor *desc);

/* ── Policy string helpers ──────────────────────────────────────────────── */

/**
 * @brief Parse a string into a policy enum.
 *
 * Case-insensitive.  Unknown or NULL values return SYSTEM_OVERRIDE.
 *
 * @param value  String from config, or NULL
 * @return Parsed enum value (safe default on invalid input)
 */
PUBLIC_API CpuAffinityPolicy CpuAffinityPolicyFromString(const char *value);

#endif /* UFSRV_CORE_SYSTEM_UTILS_CPU_H */
