/**
 * @file cpu_affinity_policy_test.cpp
 * @brief gtest suite for the CPU affinity module.
 */

#include "gtest/gtest.h"

extern "C" {
#include "utils_cpu.h"
#include "cpu_affinity_policy_enum.h"
#include "ufsrv_thread_descriptor_type.h"
#include "ufsrv_thread_cpu_affinity_descriptor_type.h"
#include "cpu_affinity_descriptor_type.h"
#include "utils_cpu_priv.h"
}

#include <cstring>
#include <cstdlib>

/* ── Test fixture ───────────────────────────────────────────────────────── */

class CpuAffinityTest : public ::testing::Test {
protected:
  void SetUp() override {
    /* Reset to real CPU count before each test */
    UfsrvCpuAffinityFakeCpuCount(-1);
  }

  void TearDown() override {
    UfsrvCpuAffinityFakeCpuCount(-1);
  }

  /* Helper: Init with an inline descriptor (listener, worker, aux) */
  static UfsrvThreadCpuAffinityDescriptor *
  Init(CpuAffinityPolicy policy, size_t listener, size_t worker, size_t aux)
  {
    CpuAffinityDescriptor d = { listener, worker, aux };
    return UfsrvCpuAffinityInit(policy, &d);
  }

  /* Helper: count thread type entries matching a given type */
  static size_t
  CountByType(const UfsrvThreadCpuAffinityDescriptor *desc,
              UfsrvThreadType type)
  {
    size_t count = 0;
    for (size_t i = 0; i < desc->thread_types_count; i++) {
      if (desc->thread_types[i].type == type)
        count += desc->thread_types[i].count;
    }
    return count;
  }

  /* Helper: find first entry of given type, or NULL */
  static const UfsrvThreadDescriptor *
  FindByType(const UfsrvThreadCpuAffinityDescriptor *desc,
             UfsrvThreadType type)
  {
    for (size_t i = 0; i < desc->thread_types_count; i++) {
      if (desc->thread_types[i].type == type)
        return &desc->thread_types[i];
    }
    return nullptr;
  }
};

/* ══════════════════════════════════════════════════════════════════════════
   Group 1: Policy enum FromString / ToLiteral
   ══════════════════════════════════════════════════════════════════════════ */

TEST_F(CpuAffinityTest, ManagedFromString)
{
  EXPECT_EQ(CpuAffinityPolicyFromString("managed"), CPU_AFFINITY_MANAGED);
}

TEST_F(CpuAffinityTest, UserOverrideFromString)
{
  EXPECT_EQ(CpuAffinityPolicyFromString("user_override"),
            CPU_AFFINITY_USER_OVERRIDE);
}

TEST_F(CpuAffinityTest, SystemOverrideFromString)
{
  EXPECT_EQ(CpuAffinityPolicyFromString("system_override"),
            CPU_AFFINITY_SYSTEM_OVERRIDE);
}

TEST_F(CpuAffinityTest, CaseInsensitiveFromString)
{
  EXPECT_EQ(CpuAffinityPolicyFromString("MANAGED"), CPU_AFFINITY_MANAGED);
  EXPECT_EQ(CpuAffinityPolicyFromString("Managed"), CPU_AFFINITY_MANAGED);
  EXPECT_EQ(CpuAffinityPolicyFromString("USER_OVERRIDE"),
            CPU_AFFINITY_USER_OVERRIDE);
}

TEST_F(CpuAffinityTest, NullFromString)
{
  EXPECT_EQ(CpuAffinityPolicyFromString(nullptr),
            CPU_AFFINITY_SYSTEM_OVERRIDE);
}

TEST_F(CpuAffinityTest, UnknownFromString)
{
  EXPECT_EQ(CpuAffinityPolicyFromString("garbage"),
            CPU_AFFINITY_SYSTEM_OVERRIDE);
  EXPECT_EQ(CpuAffinityPolicyFromString(""),
            CPU_AFFINITY_SYSTEM_OVERRIDE);
}

TEST_F(CpuAffinityTest, ToLiteralRoundTrip)
{
  EXPECT_STREQ(CpuAffinityPolicyToLiteral(CPU_AFFINITY_MANAGED), "managed");
  EXPECT_STREQ(CpuAffinityPolicyToLiteral(CPU_AFFINITY_USER_OVERRIDE),
               "user_override");
  EXPECT_STREQ(CpuAffinityPolicyToLiteral(CPU_AFFINITY_SYSTEM_OVERRIDE),
               "system_override");
}

/* ══════════════════════════════════════════════════════════════════════════
   Group 2: system_override — all pinning disabled
   ══════════════════════════════════════════════════════════════════════════ */

TEST_F(CpuAffinityTest, SystemOverrideReturnsInputCounts)
{
  auto *desc = Init(CPU_AFFINITY_SYSTEM_OVERRIDE, 1, 4, 3);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 4u);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorAuxSz(desc), 3u);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorPolicy(desc),
            CPU_AFFINITY_SYSTEM_OVERRIDE);
  free(desc);
}

TEST_F(CpuAffinityTest, SystemOverrideAllThreadsUnpinned)
{
  auto *desc = Init(CPU_AFFINITY_SYSTEM_OVERRIDE, 1, 2, 1);
  for (size_t i = 0; i < desc->thread_types_count; i++) {
    EXPECT_EQ(desc->thread_types[i].core, (size_t)-1);
  }
  free(desc);
}

TEST_F(CpuAffinityTest, SystemOverrideWithZeroWorkers)
{
  auto *desc = Init(CPU_AFFINITY_SYSTEM_OVERRIDE, 1, 0, 0);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 0u);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorAuxSz(desc), 0u);
  free(desc);
}

TEST_F(CpuAffinityTest, SystemOverridePinIsNoop)
{
  auto *desc = Init(CPU_AFFINITY_SYSTEM_OVERRIDE, 1, 1, 0);
  /* Pin should not crash or hang — it's a no-op under system_override */
  UfsrvCpuAffinityPin(UFSRV_THREAD_LISTENER, 0);
  UfsrvCpuAffinityPin(UFSRV_THREAD_WORKER, 0);
  free(desc);
  SUCCEED();
}

/* ══════════════════════════════════════════════════════════════════════════
   Group 3: managed policy
   ══════════════════════════════════════════════════════════════════════════ */

TEST_F(CpuAffinityTest, Managed1to1SufficientCores)
{
  UfsrvCpuAffinityFakeCpuCount(8);
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 2);

  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 2u);

  /* Listener on core 0 */
  auto *a = FindByType(desc, UFSRV_THREAD_LISTENER);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->core, 0u);

  /* Delegator on core 1 */
  auto *d = FindByType(desc, UFSRV_THREAD_DELEGATOR);
  ASSERT_NE(d, nullptr);
  EXPECT_EQ(d->core, 1u);

  /* Workers on cores 2, 3 */
  EXPECT_EQ(CountByType(desc, UFSRV_THREAD_WORKER), 2u);

  free(desc);
}

TEST_F(CpuAffinityTest, ManagedCapsWorkerPool)
{
  UfsrvCpuAffinityFakeCpuCount(4);
  /* 4 cores, 10 workers requested -> capped to 2 (cores - listener - delegator) */
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 10, 5);

  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 2u);
  /* Aux workers unchanged (unpinned under managed) */
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorAuxSz(desc), 5u);

  free(desc);
}

TEST_F(CpuAffinityTest, ManagedSingleCoreFallback)
{
  UfsrvCpuAffinityFakeCpuCount(1);
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 4, 3);

  /* Managed caps to max(1, cpu_count - 2) = 1 on a single core */
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 1u);

  /* Everything on core 0 */
  auto *a = FindByType(desc, UFSRV_THREAD_LISTENER);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->core, 0u);

  auto *d = FindByType(desc, UFSRV_THREAD_DELEGATOR);
  ASSERT_NE(d, nullptr);
  EXPECT_EQ(d->core, 0u);

  /* The single worker is also on core 0 */
  auto *w = FindByType(desc, UFSRV_THREAD_WORKER);
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->core, 0u);

  free(desc);
}

TEST_F(CpuAffinityTest, ManagedTwoCores)
{
  UfsrvCpuAffinityFakeCpuCount(2);
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 4, 0);

  /* listener core0, delegator core1 */
  auto *a = FindByType(desc, UFSRV_THREAD_LISTENER);
  EXPECT_EQ(a->core, 0u);
  auto *d = FindByType(desc, UFSRV_THREAD_DELEGATOR);
  EXPECT_EQ(d->core, 1u);

  /* Managed caps to 1 worker, grouped onto core 1 (never core 2 — out of range) */
  auto *w = FindByType(desc, UFSRV_THREAD_WORKER);
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->core, 1u);

  free(desc);
}

TEST_F(CpuAffinityTest, UserOverrideTwoCoresWorkersInRange)
{
  UfsrvCpuAffinityFakeCpuCount(2);
  auto *desc = Init(CPU_AFFINITY_USER_OVERRIDE, 1, 5, 0);

  /* User counts honoured */
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 5u);

  /* On a 2-core box every worker core must be a real core (0 or 1) */
  size_t total = 0;
  for (size_t i = 0; i < desc->thread_types_count; i++) {
    if (desc->thread_types[i].type == UFSRV_THREAD_WORKER) {
      EXPECT_LE(desc->thread_types[i].core, 1u);
      total += desc->thread_types[i].count;
    }
  }
  EXPECT_EQ(total, 5u);

  free(desc);
}

TEST_F(CpuAffinityTest, UserOverrideManyWorkersNoOverflow)
{
  UfsrvCpuAffinityFakeCpuCount(4);
  /* 300 workers exceeds the former fixed 256-entry table — must not overflow */
  auto *desc = Init(CPU_AFFINITY_USER_OVERRIDE, 1, 300, 0);
  ASSERT_NE(desc, nullptr);

  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 300u);

  size_t total = 0;
  for (size_t i = 0; i < desc->thread_types_count; i++) {
    if (desc->thread_types[i].type == UFSRV_THREAD_WORKER) {
      EXPECT_LT(desc->thread_types[i].core, 4u);  /* within fake 4 cores */
      total += desc->thread_types[i].count;
    }
  }
  EXPECT_EQ(total, 300u);

  free(desc);
}

TEST_F(CpuAffinityTest, ManagedZeroWorkersRequested)
{
  UfsrvCpuAffinityFakeCpuCount(8);
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 0, 0);

  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 0u);
  EXPECT_EQ(CountByType(desc, UFSRV_THREAD_WORKER), 0u);

  free(desc);
}

TEST_F(CpuAffinityTest, ManagedUsesRealCpuCount)
{
  /* No fake — reads actual sysconf */
  UfsrvCpuAffinityFakeCpuCount(-1);
  int real_cores = UfsrvCpuAffinityCpuCount();
  EXPECT_GE(real_cores, 1);

  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 0);
  ASSERT_NE(desc, nullptr);

  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorCoresSz(desc),
            (size_t)real_cores);

  free(desc);
}

/* ══════════════════════════════════════════════════════════════════════════
   Group 4: user_override policy
   ══════════════════════════════════════════════════════════════════════════ */

TEST_F(CpuAffinityTest, UserOverrideHonoursPools)
{
  UfsrvCpuAffinityFakeCpuCount(4);
  auto *desc = Init(CPU_AFFINITY_USER_OVERRIDE, 1, 10, 5);

  /* NOT capped — user counts honoured */
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 10u);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorAuxSz(desc), 5u);

  free(desc);
}

TEST_F(CpuAffinityTest, UserOverrideGroupedLayout)
{
  UfsrvCpuAffinityFakeCpuCount(4);
  auto *desc = Init(CPU_AFFINITY_USER_OVERRIDE, 1, 6, 0);

  /* listener core0, delegator core1 */
  auto *d = FindByType(desc, UFSRV_THREAD_DELEGATOR);
  EXPECT_EQ(d->core, 1u);

  /* 6 workers distributed across 2 worker cores (2,3) */
  EXPECT_EQ(CountByType(desc, UFSRV_THREAD_WORKER), 6u);

  free(desc);
}

TEST_F(CpuAffinityTest, UserOverrideSingleCoreAllGrouped)
{
  UfsrvCpuAffinityFakeCpuCount(1);
  auto *desc = Init(CPU_AFFINITY_USER_OVERRIDE, 1, 4, 0);

  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 4u);
  /* All on core 0 */
  auto *a = FindByType(desc, UFSRV_THREAD_LISTENER);
  EXPECT_EQ(a->core, 0u);

  free(desc);
}

/* ══════════════════════════════════════════════════════════════════════════
   Group 5: Descriptor accessor functions
   ══════════════════════════════════════════════════════════════════════════ */

TEST_F(CpuAffinityTest, AccessorCoresSz)
{
  UfsrvCpuAffinityFakeCpuCount(6);
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 1);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorCoresSz(desc), 6u);
  free(desc);
}

TEST_F(CpuAffinityTest, AccessorWorkerSz)
{
  UfsrvCpuAffinityFakeCpuCount(8);
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 0);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorWorkerSz(desc), 2u);
  free(desc);
}

TEST_F(CpuAffinityTest, AccessorAuxSz)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 1, 3);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorAuxSz(desc), 3u);
  free(desc);
}

TEST_F(CpuAffinityTest, AccessorListenerSz)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 1, 0);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorListenerSz(desc), 1u);
  free(desc);
}

TEST_F(CpuAffinityTest, AccessorPolicy)
{
  auto *desc = Init(CPU_AFFINITY_USER_OVERRIDE, 1, 1, 0);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorPolicy(desc),
            CPU_AFFINITY_USER_OVERRIDE);
  free(desc);
}

TEST_F(CpuAffinityTest, AccessorThreadTypesCount)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 1);
  size_t count = UfsrvThreadCpuAffinityDescriptorThreadTypesCount(desc);
  EXPECT_GE(count, 3u);  /* at minimum: listener, delegator, worker */
  free(desc);
}

TEST_F(CpuAffinityTest, AccessorThreadTypeAtNullForOob)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 1, 0);
  size_t count = UfsrvThreadCpuAffinityDescriptorThreadTypesCount(desc);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorThreadTypeAt(desc, count),
            nullptr);
  EXPECT_EQ(UfsrvThreadCpuAffinityDescriptorThreadTypeAt(desc, 999),
            nullptr);
  free(desc);
}

TEST_F(CpuAffinityTest, AccessorThreadTypeAtValid)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 0);
  size_t count = UfsrvThreadCpuAffinityDescriptorThreadTypesCount(desc);
  for (size_t i = 0; i < count; i++) {
    auto *td = UfsrvThreadCpuAffinityDescriptorThreadTypeAt(desc, i);
    ASSERT_NE(td, nullptr);
    EXPECT_LE(td->type, UFSRV_THREAD_AUX);
  }
  free(desc);
}

/* ══════════════════════════════════════════════════════════════════════════
   Group 6: Thread type descriptor invariants
   ══════════════════════════════════════════════════════════════════════════ */

TEST_F(CpuAffinityTest, ListenerAlwaysPresent)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 1, 0);
  auto *a = FindByType(desc, UFSRV_THREAD_LISTENER);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->count, 1u);
  free(desc);
}

TEST_F(CpuAffinityTest, DelegatorAlwaysPresent)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 1, 0);
  auto *d = FindByType(desc, UFSRV_THREAD_DELEGATOR);
  ASSERT_NE(d, nullptr);
  EXPECT_EQ(d->count, 1u);
  free(desc);
}

TEST_F(CpuAffinityTest, AuxAlwaysUnpinned)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 3);
  auto *t = FindByType(desc, UFSRV_THREAD_AUX);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->core, (size_t)-1);
  EXPECT_EQ(t->count, 3u);
  free(desc);
}

TEST_F(CpuAffinityTest, WorkersHaveCorrectCount)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 3, 0);
  EXPECT_EQ(CountByType(desc, UFSRV_THREAD_WORKER),
            UfsrvThreadCpuAffinityDescriptorWorkerSz(desc));
  free(desc);
}

TEST_F(CpuAffinityTest, AuxWorkersUnpinnedInManaged)
{
  UfsrvCpuAffinityFakeCpuCount(8);
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 3);
  auto *uw = FindByType(desc, UFSRV_THREAD_AUX);
  ASSERT_NE(uw, nullptr);
  EXPECT_EQ(uw->core, (size_t)-1);
  free(desc);
}

/* ══════════════════════════════════════════════════════════════════════════
   Group 7: JSON Describe
   ══════════════════════════════════════════════════════════════════════════ */

TEST_F(CpuAffinityTest, DescribeReturnsNonNull)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 1);
  char *json = UfsrvCpuAffinityDescribe(desc);
  ASSERT_NE(json, nullptr);
  EXPECT_GT(strlen(json), 0u);
  free(json);
  free(desc);
}

TEST_F(CpuAffinityTest, DescribeContainsAllKeys)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 2, 1);
  char *json = UfsrvCpuAffinityDescribe(desc);
  ASSERT_NE(json, nullptr);

  EXPECT_NE(strstr(json, "\"cores_sz\""), nullptr);
  EXPECT_NE(strstr(json, "\"policy\""), nullptr);
  EXPECT_NE(strstr(json, "\"listener_worker_pool_sz\""), nullptr);
  EXPECT_NE(strstr(json, "\"worker_pool_sz\""), nullptr);
  EXPECT_NE(strstr(json, "\"aux_worker_pool_sz\""), nullptr);
  EXPECT_NE(strstr(json, "\"thread_types\""), nullptr);

  free(json);
  free(desc);
}

TEST_F(CpuAffinityTest, DescribeSystemOverrideJson)
{
  auto *desc = Init(CPU_AFFINITY_SYSTEM_OVERRIDE, 1, 1, 0);
  char *json = UfsrvCpuAffinityDescribe(desc);
  ASSERT_NE(json, nullptr);
  EXPECT_NE(strstr(json, "\"policy\":\"system_override\""), nullptr);
  free(json);
  free(desc);
}

TEST_F(CpuAffinityTest, DescribeManagedJson)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 1, 0);
  char *json = UfsrvCpuAffinityDescribe(desc);
  ASSERT_NE(json, nullptr);
  EXPECT_NE(strstr(json, "\"policy\":\"managed\""), nullptr);
  EXPECT_NE(strstr(json, "\"listener\""), nullptr);
  EXPECT_NE(strstr(json, "\"delegator\""), nullptr);
  free(json);
  free(desc);
}

TEST_F(CpuAffinityTest, DescribeNullDescReturnsNull)
{
  char *json = UfsrvCpuAffinityDescribe(nullptr);
  EXPECT_EQ(json, nullptr);
}

TEST_F(CpuAffinityTest, DescribeFreesCleanly)
{
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 3, 2);
  char *json = UfsrvCpuAffinityDescribe(desc);
  ASSERT_NE(json, nullptr);
  free(json);
  free(desc);
  /* No leak check here — run under valgrind for actual verification */
  SUCCEED();
}

TEST_F(CpuAffinityTest, DescribeRoundTripMatchesAccessors)
{
  UfsrvCpuAffinityFakeCpuCount(8);
  auto *desc = Init(CPU_AFFINITY_MANAGED, 1, 3, 2);
  ASSERT_NE(desc, nullptr);

  char *json = UfsrvCpuAffinityDescribe(desc);
  ASSERT_NE(json, nullptr);

  /* cores_sz */
  EXPECT_NE(strstr(json, "\"cores_sz\":8"), nullptr);

  /* policy */
  const char *policy_str = CpuAffinityPolicyToLiteral(
      UfsrvThreadCpuAffinityDescriptorPolicy(desc));
  char policy_kv[64];
  snprintf(policy_kv, sizeof(policy_kv), "\"policy\":\"%s\"", policy_str);
  EXPECT_NE(strstr(json, policy_kv), nullptr);

  /* worker_pool_sz */
  size_t wp = UfsrvThreadCpuAffinityDescriptorWorkerSz(desc);
  char wp_kv[64];
  snprintf(wp_kv, sizeof(wp_kv), "\"worker_pool_sz\":%zu", wp);
  EXPECT_NE(strstr(json, wp_kv), nullptr);

  /* aux_worker_pool_sz */
  size_t ax = UfsrvThreadCpuAffinityDescriptorAuxSz(desc);
  char ax_kv[64];
  snprintf(ax_kv, sizeof(ax_kv), "\"aux_worker_pool_sz\":%zu", ax);
  EXPECT_NE(strstr(json, ax_kv), nullptr);

  /* thread_types array present */
  EXPECT_NE(strstr(json, "\"thread_types\":["), nullptr);

  /* Each thread_type entry has valid type and core */
  size_t count = UfsrvThreadCpuAffinityDescriptorThreadTypesCount(desc);
  for (size_t i = 0; i < count; i++) {
    auto *td = UfsrvThreadCpuAffinityDescriptorThreadTypeAt(desc, i);
    ASSERT_NE(td, nullptr);

    /* Verify JSON contains this entry's type and core */
    switch (td->type) {
      case UFSRV_THREAD_LISTENER:
        EXPECT_NE(strstr(json, "\"type\":\"listener\""), nullptr);
        break;
      case UFSRV_THREAD_DELEGATOR:
        EXPECT_NE(strstr(json, "\"type\":\"delegator\""), nullptr);
        break;
      case UFSRV_THREAD_WORKER:
        EXPECT_NE(strstr(json, "\"type\":\"worker\""), nullptr);
        break;
      case UFSRV_THREAD_AUX:
        EXPECT_NE(strstr(json, "\"type\":\"aux\""), nullptr);
        break;
      default:
        FAIL() << "Unknown thread type";
    }

    /* Verify core value */
    char core_kv[64];
    snprintf(core_kv, sizeof(core_kv), "\"core\":%zu", td->core);
    EXPECT_NE(strstr(json, core_kv), nullptr);

    /* Verify count value */
    char count_kv[64];
    snprintf(count_kv, sizeof(count_kv), "\"count\":%zu", td->count);
    EXPECT_NE(strstr(json, count_kv), nullptr);
  }

  /* JSON is valid — starts with { and ends with } */
  size_t len = strlen(json);
  EXPECT_EQ(json[0], '{');
  EXPECT_EQ(json[len - 1], '}');

  free(json);
  free(desc);
}

/* ══════════════════════════════════════════════════════════════════════════
   Group 8: CPU-affinity recommendation (industry best practice)
   ══════════════════════════════════════════════════════════════════════════ */

/* CpuAffinityDescriptor field order:
   { listener_worker_pool_sz, worker_pool_sz, aux_worker_pool_sz } */

TEST_F(CpuAffinityTest, RecommendSingleCoreSystemOverride)
{
  CpuAffinityDescriptor d = { 1, 4, 3 };
  UfsrvCpuAffinityAdvice a;
  UfsrvCpuAffinityComputeAdvice(1, &d, &a);
  EXPECT_EQ(a.policy, CPU_AFFINITY_SYSTEM_OVERRIDE);
  EXPECT_EQ(a.worker_pool_sz, 4u);   /* unchanged — no pinning */
}

TEST_F(CpuAffinityTest, RecommendTwoCoreSystemOverride)
{
  CpuAffinityDescriptor d = { 1, 4, 3 };
  UfsrvCpuAffinityAdvice a;
  UfsrvCpuAffinityComputeAdvice(2, &d, &a);
  EXPECT_EQ(a.policy, CPU_AFFINITY_SYSTEM_OVERRIDE);
  EXPECT_EQ(a.worker_pool_sz, 4u);
}

TEST_F(CpuAffinityTest, RecommendFitsManaged)
{
  CpuAffinityDescriptor d = { 1, 2, 3 };
  UfsrvCpuAffinityAdvice a;
  UfsrvCpuAffinityComputeAdvice(8, &d, &a);
  EXPECT_EQ(a.policy, CPU_AFFINITY_MANAGED);
  EXPECT_FALSE(a.oversubscribed);
  EXPECT_EQ(a.worker_pool_sz, 2u);   /* fits — unchanged */
  EXPECT_EQ(a.pinnable_cores, 6);
  EXPECT_EQ(a.aux_worker_pool_sz, 3u);   /* unpinned — honoured */
  EXPECT_EQ(a.listener_worker_pool_sz, 1u);
}

TEST_F(CpuAffinityTest, RecommendOversubscribedCaps)
{
  CpuAffinityDescriptor d = { 1, 10, 5 };
  UfsrvCpuAffinityAdvice a;
  UfsrvCpuAffinityComputeAdvice(4, &d, &a);
  EXPECT_EQ(a.policy, CPU_AFFINITY_MANAGED);
  EXPECT_TRUE(a.oversubscribed);
  EXPECT_EQ(a.worker_pool_sz, 2u);   /* capped to cores - 2 */
  EXPECT_EQ(a.aux_worker_pool_sz, 5u);   /* unpinned — honoured */
  EXPECT_EQ(a.listener_worker_pool_sz, 1u);
}

TEST_F(CpuAffinityTest, RecommendThreeCoreExactFit)
{
  CpuAffinityDescriptor d = { 1, 1, 0 };
  UfsrvCpuAffinityAdvice a;
  UfsrvCpuAffinityComputeAdvice(3, &d, &a);
  EXPECT_EQ(a.policy, CPU_AFFINITY_MANAGED);
  EXPECT_EQ(a.worker_pool_sz, 1u);   /* pinnable = 1 */
}

TEST_F(CpuAffinityTest, RecommendZeroWorkers)
{
  CpuAffinityDescriptor d = { 0, 0, 0 };
  UfsrvCpuAffinityAdvice a;
  UfsrvCpuAffinityComputeAdvice(8, &d, &a);
  EXPECT_EQ(a.policy, CPU_AFFINITY_MANAGED);
  EXPECT_EQ(a.worker_pool_sz, 0u);
}

TEST_F(CpuAffinityTest, RecommendNullDescAllZero)
{
  UfsrvCpuAffinityAdvice a;
  UfsrvCpuAffinityComputeAdvice(8, nullptr, &a);
  EXPECT_EQ(a.policy, CPU_AFFINITY_MANAGED);
  EXPECT_EQ(a.worker_pool_sz, 0u);
  EXPECT_EQ(a.listener_worker_pool_sz, 0u);
  EXPECT_EQ(a.aux_worker_pool_sz, 0u);
}

TEST_F(CpuAffinityTest, RecommendNullAdviceNoCrash)
{
  CpuAffinityDescriptor d = { 1, 1, 1 };
  UfsrvCpuAffinityComputeAdvice(8, &d, nullptr);
  SUCCEED();
}

TEST_F(CpuAffinityTest, RecommendReturnsReport)
{
  CpuAffinityDescriptor d = { 1, 2, 3 };
  char *r = UfsrvCpuAffinityRecommend(&d);
  ASSERT_NE(r, nullptr);
  EXPECT_NE(strstr(r, "Recommended policy"), nullptr);
  EXPECT_NE(strstr(r, "Logical CPUs"), nullptr);
  free(r);
}
