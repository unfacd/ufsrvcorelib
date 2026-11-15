/**
 * @file utils_cpu.c
 * @brief Implementation of the CPU affinity subsystem.
 *
 * Copyright (C) 2015-2026 unfacd works
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <syslog.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

#include "config_defs_ufsrvcorelib.h"
#include "utils_cpu_priv.h"
#include "cpu_affinity_descriptor_type.h"
#include "cpu_affinity_policy_enum.h"
#include "ufsrv_thread_cpu_affinity_descriptor_type.h"
#include "ufsrv_thread_descriptor_type.h"
#include "utils_cpu.h"

/* ── gettid() fallback for glibc < 2.30 ─────────────────────────────────── */

#if defined(__linux__) && defined(__GLIBC__) && __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
static pid_t
sGetTid(void)
{
  return (pid_t)syscall(SYS_gettid);
}
#else
static pid_t
sGetTid(void)
{
  return gettid();
}
#endif

/* ── Internal state ─────────────────────────────────────────────────────── */

static long                 sFakeCpuCount      = -1;

/* ── CPU count query ────────────────────────────────────────────────────── */

static long
sGetCpuCount(void)
{
  if (sFakeCpuCount >= 0)  return sFakeCpuCount;
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return (n < 1) ? 1 : n;
}
static CpuAffinityPolicy    sActivePolicy      = CPU_AFFINITY_SYSTEM_OVERRIDE;
static size_t              sListenerWorkers   = 0;   /* effective listener pool */
static size_t              sWorkerPool        = 0;   /* effective worker pool (may be capped) */
static size_t              sAuxWorkers        = 0;   /* effective aux pool (unpinned) */
static size_t              sCpuCount          = 0;

/* ── Core assignment tables (populated by Init) ─────────────────────────── */

static size_t  sListenerCore   = (size_t)-1;
static size_t  sDelegatorCore  = (size_t)-1;

/* worker_core[i] = assigned core for WORKER i, or (size_t)-1.
 * Sized dynamically in Init() to the requested pool — indexed by worker
 * number, which under user_override may exceed the cpu count. */
static size_t *sWorkerCores    = NULL;
static size_t  sWorkerCoresCap = 0;

/* ── Defaults ───────────────────────────────────────────────────────────── */

void
UfsrvCpuAffinityDefaults(CpuAffinityPolicy *policy_out,
                         CpuAffinityDescriptor *desc_out)
{
  if (policy_out)  *policy_out = CPU_AFFINITY_SYSTEM_OVERRIDE;
  if (desc_out) {
    desc_out->listener_worker_pool_sz = 1;
    desc_out->worker_pool_sz          = 2;
    desc_out->aux_worker_pool_sz      = 2;
  }
}

/* ── Provider injection ─────────────────────────────────────────────────── */

void
UfsrvCpuAffinityFakeCpuCount(long count)
{
  sFakeCpuCount = count;
}

/* ── State accessors ────────────────────────────────────────────────────── */

CpuAffinityPolicy
UfsrvCpuAffinityActivePolicy(void)
{
  return sActivePolicy;
}

size_t
UfsrvCpuAffinityEffectiveWorkerPool(void)
{
  return sWorkerPool;
}

size_t
UfsrvCpuAffinityEffectiveAuxWorkers(void)
{
  return sAuxWorkers;
}

/* ── Policy string helpers ──────────────────────────────────────────────── */

CpuAffinityPolicy
CpuAffinityPolicyFromString(const char *value)
{
  if (!value)                              return CPU_AFFINITY_SYSTEM_OVERRIDE;
  if (strcasecmp(value, "managed") == 0)    return CPU_AFFINITY_MANAGED;
  if (strcasecmp(value, "user_override") == 0) return CPU_AFFINITY_USER_OVERRIDE;
  if (strcasecmp(value, "system_override") == 0) return CPU_AFFINITY_SYSTEM_OVERRIDE;

  syslog(LOG_WARNING, "Unrecognised CPU affinity policy '%s' — "
         "falling back to system_override (no CPU pinning)", value);
  return CPU_AFFINITY_SYSTEM_OVERRIDE;
}

/* ── Internal: compute managed-policy layout ────────────────────────────── */

static void
sComputeManagedLayout(const CpuAffinityDescriptor *desc)
{
  size_t listener = desc ? desc->listener_worker_pool_sz : 0;
  size_t worker   = desc ? desc->worker_pool_sz : 0;

  long cpu_count = sGetCpuCount();
  if (cpu_count < 1)  cpu_count = 1;
  sCpuCount = (size_t)cpu_count;

  /* Listener and aux pools are honoured as-is; aux is always unpinned */
  sListenerWorkers = listener;
  sAuxWorkers      = desc ? desc->aux_worker_pool_sz : 0;

  if (cpu_count == 1) {
    /* Everything on core 0; managed caps the worker pool to the single core */
    sListenerCore  = 0;
    sDelegatorCore = 0;
    sWorkerPool = (worker > 0) ? 1 : 0;
    if (worker > sWorkerPool)
      syslog(LOG_WARNING, "CPU affinity (managed): capped worker pool "
             "from %zu to %zu (single core)", worker, sWorkerPool);
    for (size_t i = 0; i < sWorkerPool; i++)
      sWorkerCores[i] = 0;
    return;
  }

  /* Dedicated cores for listener and delegator */
  sListenerCore  = 0;
  sDelegatorCore = 1;

  size_t available = (size_t)(cpu_count - 2);
  if (available < 1)  available = 1;

  /* Cap worker pool to available cores (min 1 guarantee) */
  sWorkerPool = (worker > available) ? available : worker;

  if (sWorkerPool < worker)
    syslog(LOG_WARNING, "CPU affinity (managed): capped worker pool "
           "from %zu to %zu (%ld cores available, 2 reserved)",
           worker, sWorkerPool, cpu_count);

  /* Assign workers to worker cores: base 2 when cores >= 3 (0,1 are the
   * listener/delegator), else base 1 so a 2-core box never gets core 2. */
  size_t worker_base = (cpu_count >= 3) ? 2 : 1;
  for (size_t i = 0; i < sWorkerPool && i < sWorkerCoresCap; i++)
    sWorkerCores[i] = worker_base + i;

  syslog(LOG_INFO, "CPU affinity (managed): %ld cores, %zu worker pool, "
         "%zu aux workers (unpinned)", cpu_count, sWorkerPool, sAuxWorkers);
}

/* ── Internal: compute user_override-policy layout ──────────────────────── */

static void
sComputeUserOverrideLayout(const CpuAffinityDescriptor *desc)
{
  size_t listener = desc ? desc->listener_worker_pool_sz : 0;
  size_t worker   = desc ? desc->worker_pool_sz : 0;

  long cpu_count = sGetCpuCount();
  if (cpu_count < 1)  cpu_count = 1;
  sCpuCount = (size_t)cpu_count;

  /* User counts always honoured */
  sListenerWorkers = listener;
  sWorkerPool      = worker;
  sAuxWorkers      = desc ? desc->aux_worker_pool_sz : 0;

  if (cpu_count == 1) {
    sListenerCore  = 0;
    sDelegatorCore = 0;
    for (size_t i = 0; i < sWorkerPool && i < sWorkerCoresCap; i++)
      sWorkerCores[i] = 0;
    syslog(LOG_WARNING, "CPU affinity (user_override): single core — "
           "all threads grouped on core 0");
    return;
  }

  sListenerCore  = 0;
  sDelegatorCore = 1;

  size_t worker_cores = (size_t)(cpu_count - 2);
  if (worker_cores < 1)  worker_cores = 1;
  size_t worker_base = (cpu_count >= 3) ? 2 : 1;

  /* Grouped: distribute workers across available cores */
  for (size_t i = 0; i < sWorkerPool && i < sWorkerCoresCap; i++)
    sWorkerCores[i] = worker_base + (i % worker_cores);

  syslog(LOG_INFO, "CPU affinity (user_override): %ld cores, %zu worker pool "
         "grouped across %zu worker core(s)", cpu_count, sWorkerPool,
         worker_cores);
}

/* ── Internal: system_override layout ───────────────────────────────────── */

static void
sComputeSystemOverrideLayout(const CpuAffinityDescriptor *desc)
{
  long cpu_count = sGetCpuCount();
  if (cpu_count < 1)  cpu_count = 1;
  sCpuCount = (size_t)cpu_count;

  /* Pass-through: user counts unchanged, nothing pinned */
  sListenerWorkers = desc ? desc->listener_worker_pool_sz : 0;
  sWorkerPool      = desc ? desc->worker_pool_sz : 0;
  sAuxWorkers      = desc ? desc->aux_worker_pool_sz : 0;
  sListenerCore    = (size_t)-1;
  sDelegatorCore   = (size_t)-1;
  for (size_t i = 0; i < sWorkerPool && i < sWorkerCoresCap; i++)
    sWorkerCores[i] = (size_t)-1;
}

/* ── Public: Init ───────────────────────────────────────────────────────── */

UfsrvThreadCpuAffinityDescriptor *
UfsrvCpuAffinityInit(CpuAffinityPolicy policy, const CpuAffinityDescriptor *desc)
{
  size_t worker = desc ? desc->worker_pool_sz : 0;

  /* (Re)size the worker-core table to the requested pool.  It is indexed by
   * worker number, so it must hold at least worker entries — under
   * user_override the count is honoured as-is and may exceed cpu_count. */
  size_t need = (worker > 0) ? worker : 1;
  if (need > sWorkerCoresCap) {
    size_t *grown = realloc(sWorkerCores, need * sizeof(*sWorkerCores));
    if (!grown)  return NULL;
    sWorkerCores    = grown;
    sWorkerCoresCap = need;
  }

  sActivePolicy = policy;

  /* Reset all assignments */
  sListenerCore  = (size_t)-1;
  sDelegatorCore = (size_t)-1;
  memset(sWorkerCores, 0xff, sWorkerCoresCap * sizeof(*sWorkerCores));

  switch (policy) {
    case CPU_AFFINITY_MANAGED:
      sComputeManagedLayout(desc);
      break;
    case CPU_AFFINITY_USER_OVERRIDE:
      sComputeUserOverrideLayout(desc);
      break;
    case CPU_AFFINITY_SYSTEM_OVERRIDE:
    default:
      sComputeSystemOverrideLayout(desc);
      break;
  }

  /* Compute the exact number of thread-type entries */
  size_t n_entries = 2;  /* listener, delegator — always present */

  /* Count worker groups (grouped by consecutive same-core assignment) */
  size_t worker_groups = 0;
  if (sWorkerPool > 0) {
    worker_groups = 1;
    for (size_t i = 1; i < sWorkerPool; i++) {
      if (sWorkerCores[i] != sWorkerCores[i - 1])
        worker_groups++;
    }
  }
  n_entries += worker_groups;

  /* Aux workers */
  if (sAuxWorkers > 0)  n_entries++;

  /* Single slab: descriptor + flexible array */
  size_t alloc_sz = sizeof(UfsrvThreadCpuAffinityDescriptor)
                    + n_entries * sizeof(UfsrvThreadDescriptor);
  UfsrvThreadCpuAffinityDescriptor *out = calloc(1, alloc_sz);
  if (!out)  return NULL;

  out->cores_sz                = sCpuCount;
  out->listener_worker_pool_sz = sListenerWorkers;
  out->worker_pool_sz          = sWorkerPool;
  out->aux_worker_pool_sz      = sAuxWorkers;
  out->policy                  = sActivePolicy;
  out->thread_types_count      = n_entries;

  size_t idx = 0;

  /* Listener (accept loop) */
  out->thread_types[idx++] = (UfsrvThreadDescriptor){
    .type  = UFSRV_THREAD_LISTENER,
    .core  = sListenerCore,
    .count = sListenerWorkers
  };

  /* Delegator (implicit control-plane thread) */
  out->thread_types[idx++] = (UfsrvThreadDescriptor){
    .type  = UFSRV_THREAD_DELEGATOR,
    .core  = sDelegatorCore,
    .count = 1
  };

  /* Workers — group by core for compact output */
  if (sWorkerPool > 0) {
    size_t prev_core = sWorkerCores[0];
    size_t count     = 1;
    for (size_t i = 1; i < sWorkerPool; i++) {
      if (sWorkerCores[i] == prev_core) {
        count++;
      } else {
        out->thread_types[idx++] = (UfsrvThreadDescriptor){
          .type  = UFSRV_THREAD_WORKER,
          .core  = prev_core,
          .count = count
        };
        prev_core = sWorkerCores[i];
        count     = 1;
      }
    }
    out->thread_types[idx++] = (UfsrvThreadDescriptor){
      .type  = UFSRV_THREAD_WORKER,
      .core  = prev_core,
      .count = count
    };
  }

  /* Aux workers (always unpinned) */
  if (sAuxWorkers > 0) {
    out->thread_types[idx++] = (UfsrvThreadDescriptor){
      .type  = UFSRV_THREAD_AUX,
      .core  = (size_t)-1,
      .count = sAuxWorkers
    };
  }

  return out;
}

/* ── Internal: core lookup shared by Pin() and CoreFor() ────────────────── */

static size_t
sCoreFor(UfsrvThreadType type, int idx)
{
  if (sActivePolicy == CPU_AFFINITY_SYSTEM_OVERRIDE)
    return (size_t)-1;

  switch (type) {
    case UFSRV_THREAD_LISTENER:       return sListenerCore;
    case UFSRV_THREAD_DELEGATOR:      return sDelegatorCore;
    case UFSRV_THREAD_WORKER:
      if (idx >= 0 && (size_t)idx < sWorkerCoresCap)
        return sWorkerCores[idx];
      return (size_t)-1;
    case UFSRV_THREAD_AUX:
    default:
      return (size_t)-1;  /* unpinned — no-op */
  }
}

/* ── Public: Pin ────────────────────────────────────────────────────────── */

void
UfsrvCpuAffinityPin(UfsrvThreadType type, int idx)
{
  size_t core = sCoreFor(type, idx);
  if (core == (size_t)-1)
    return;

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET((int)core, &set);
  sched_setaffinity(sGetTid(), sizeof(cpu_set_t), &set);
}

/* ── Public: CoreFor ────────────────────────────────────────────────────── */

size_t
UfsrvCpuAffinityCoreFor(UfsrvThreadType type, int idx)
{
  return sCoreFor(type, idx);
}

/* ── Descriptor accessors ───────────────────────────────────────────────── */

size_t
UfsrvThreadCpuAffinityDescriptorCoresSz(
    const UfsrvThreadCpuAffinityDescriptor *desc)
{
  return desc ? desc->cores_sz : 0;
}

size_t
UfsrvThreadCpuAffinityDescriptorListenerSz(
    const UfsrvThreadCpuAffinityDescriptor *desc)
{
  return desc ? desc->listener_worker_pool_sz : 0;
}

size_t
UfsrvThreadCpuAffinityDescriptorWorkerSz(
    const UfsrvThreadCpuAffinityDescriptor *desc)
{
  return desc ? desc->worker_pool_sz : 0;
}

size_t
UfsrvThreadCpuAffinityDescriptorAuxSz(
    const UfsrvThreadCpuAffinityDescriptor *desc)
{
  return desc ? desc->aux_worker_pool_sz : 0;
}

CpuAffinityPolicy
UfsrvThreadCpuAffinityDescriptorPolicy(
    const UfsrvThreadCpuAffinityDescriptor *desc)
{
  return desc ? desc->policy : CPU_AFFINITY_SYSTEM_OVERRIDE;
}

size_t
UfsrvThreadCpuAffinityDescriptorThreadTypesCount(
    const UfsrvThreadCpuAffinityDescriptor *desc)
{
  return desc ? desc->thread_types_count : 0;
}

const UfsrvThreadDescriptor *
UfsrvThreadCpuAffinityDescriptorThreadTypeAt(
    const UfsrvThreadCpuAffinityDescriptor *desc,
    size_t                                 index)
{
  if (!desc || index >= desc->thread_types_count)  return NULL;
  return &desc->thread_types[index];
}

/* ── Public: CpuCount ───────────────────────────────────────────────────── */

int
UfsrvCpuAffinityCpuCount(void)
{
  long n = sGetCpuCount();
  return (n < 1) ? 1 : (int)n;
}

/* ── Public: Describe (JSON) ─────────────────────────────────────────────── */

char *
UfsrvCpuAffinityDescribe(const UfsrvThreadCpuAffinityDescriptor *desc)
{
  if (!desc)  return NULL;

  /* Estimate buffer size */
  size_t sz = 512 + (desc->thread_types_count * 80);
  char *buf = malloc(sz);
  if (!buf)  return NULL;

  int off = snprintf(buf, sz,
      "{\"cores_sz\":%zu,\"policy\":\"%s\","
      "\"listener_worker_pool_sz\":%zu,\"worker_pool_sz\":%zu,"
      "\"aux_worker_pool_sz\":%zu,\"thread_types\":[",
      desc->cores_sz,
      CpuAffinityPolicyToLiteral(desc->policy),
      desc->listener_worker_pool_sz,
      desc->worker_pool_sz,
      desc->aux_worker_pool_sz);

  for (size_t i = 0; i < desc->thread_types_count; i++) {
    const UfsrvThreadDescriptor *td = &desc->thread_types[i];
    const char *type_str;
    switch (td->type) {
      case UFSRV_THREAD_LISTENER:  type_str = "listener";  break;
      case UFSRV_THREAD_DELEGATOR: type_str = "delegator"; break;
      case UFSRV_THREAD_WORKER:    type_str = "worker";    break;
      case UFSRV_THREAD_AUX:       type_str = "aux";       break;
      default:                     type_str = "unknown";   break;
    }

    off += snprintf(buf + off, sz - (size_t)off,
        "%s{\"type\":\"%s\",\"core\":%zu,\"count\":%zu}",
        (i > 0) ? "," : "",
        type_str,
        td->core,
        td->count);
  }

  snprintf(buf + off, sz - (size_t)off, "]}");
  return buf;
}

/* ── Recommendation (industry best-practice heuristic) ──────────────────── */

void
UfsrvCpuAffinityComputeAdvice(long cpu_count, const CpuAffinityDescriptor *desc,
                              UfsrvCpuAffinityAdvice *advice_out)
{
  if (!advice_out)  return;

  if (cpu_count < 1)  cpu_count = 1;
  long pinnable = cpu_count - 2;
  if (pinnable < 0)  pinnable = 0;

  size_t listener = desc ? desc->listener_worker_pool_sz : 0;
  size_t worker   = desc ? desc->worker_pool_sz : 0;
  size_t aux      = desc ? desc->aux_worker_pool_sz : 0;

  advice_out->cpu_count      = cpu_count;
  advice_out->pinnable_cores = pinnable;
  advice_out->oversubscribed = ((long)worker > pinnable);

  /* Listener (reserved on core 0) and aux (unpinned) are honoured as-is */
  advice_out->listener_worker_pool_sz = listener;
  advice_out->aux_worker_pool_sz      = aux;

  if (cpu_count <= 2) {
    /* Too few cores: reserving two leaves nothing for the worker pool. */
    advice_out->policy         = CPU_AFFINITY_SYSTEM_OVERRIDE;
    advice_out->worker_pool_sz = worker;   /* unchanged — no pinning */
    return;
  }

  if (!advice_out->oversubscribed) {
    /* Main workers fit 1:1 on dedicated cores. */
    advice_out->policy         = CPU_AFFINITY_MANAGED;
    advice_out->worker_pool_sz = worker;
    return;
  }

  /* Oversubscribed: best practice sizes the hot path to the cores. */
  advice_out->policy         = CPU_AFFINITY_MANAGED;
  advice_out->worker_pool_sz = (pinnable > 0) ? (size_t)pinnable : 1;
}

char *
UfsrvCpuAffinityRecommend(const CpuAffinityDescriptor *desc)
{
  UfsrvCpuAffinityAdvice advice;
  UfsrvCpuAffinityComputeAdvice(sGetCpuCount(), desc, &advice);

  const char *policy_str = CpuAffinityPolicyToLiteral(advice.policy);

  size_t listener = desc ? desc->listener_worker_pool_sz : 0;
  size_t worker   = desc ? desc->worker_pool_sz : 0;
  size_t aux      = desc ? desc->aux_worker_pool_sz : 0;

  char buf[1536];
  size_t cap = sizeof(buf);
  int off = 0;

  off += snprintf(buf + off, cap - (size_t)off,
      "CPU affinity recommendation (industry best practice)\n");
  off += snprintf(buf + off, cap - (size_t)off,
      "  Logical CPUs:          %ld\n", advice.cpu_count);
  off += snprintf(buf + off, cap - (size_t)off,
      "  Pinnable worker cores: %ld  (core 0 = listener, core 1 = delegator)\n",
      advice.pinnable_cores);
  off += snprintf(buf + off, cap - (size_t)off,
      "  Configured pools:      listener=%zu  worker=%zu  aux=%zu\n",
      listener, worker, aux);
  off += snprintf(buf + off, cap - (size_t)off,
      "  Recommended policy:    %s\n", policy_str);

  if (worker > 0 && advice.worker_pool_sz != worker)
    off += snprintf(buf + off, cap - (size_t)off,
        "  Advised worker counts: worker=%zu (capped from %zu)  listener=%zu  aux=%zu\n",
        advice.worker_pool_sz, worker, advice.listener_worker_pool_sz,
        advice.aux_worker_pool_sz);
  else
    off += snprintf(buf + off, cap - (size_t)off,
        "  Advised worker counts: worker=%zu  listener=%zu  aux=%zu\n",
        advice.worker_pool_sz, advice.listener_worker_pool_sz,
        advice.aux_worker_pool_sz);

  if (advice.policy == CPU_AFFINITY_SYSTEM_OVERRIDE)
    off += snprintf(buf + off, cap - (size_t)off,
        "  Reason:                <= 2 cores — pinning offers no benefit\n");
  else if (advice.oversubscribed)
    off += snprintf(buf + off, cap - (size_t)off,
        "  Reason:                worker pool exceeds pinnable cores — capped to avoid oversubscription\n");
  else
    off += snprintf(buf + off, cap - (size_t)off,
        "  Reason:                worker pool fits 1:1 on dedicated cores\n");

  off += snprintf(buf + off, cap - (size_t)off,
      "  Notes:\n");
  off += snprintf(buf + off, cap - (size_t)off,
      "    - listener is reserved on core 0; aux workers are left unpinned (OS-scheduled)\n");
  if (advice.oversubscribed)
    off += snprintf(buf + off, cap - (size_t)off,
        "    - alternative: user_override keeps all workers grouped (suitable for I/O-bound workloads)\n");
  off += snprintf(buf + off, cap - (size_t)off,
      "    - based on logical CPUs; SMT siblings are counted separately\n");

  return strdup(buf);
}
