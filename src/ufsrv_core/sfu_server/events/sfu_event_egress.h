#ifndef UFSRVCORELIB_SFU_EVENT_EGRESS_H
#define UFSRVCORELIB_SFU_EVENT_EGRESS_H

#include <stdbool.h>
#include <stddef.h>

#include <ufsrvcorelib/ufsrv_core/sfu_server/events/sfu_event_type.h>

typedef struct SfuEventEgress SfuEventEgress;

/** Worker-thread adapter operations. Both calls must return in bounded time.
 * submit may buffer records. finish flushes pending records and closes the
 * adapter using the monotonic shutdown deadline (zero on worker failure).
 */
typedef struct {
  bool (*submit)(void *context, const SfuEvent *event);
  void (*finish)(void *context, uint64_t deadline_ms);
} SfuEventEgressAdapter;

SfuEventEgress *SfuEventEgressStart(size_t capacity, const SfuEventEgressAdapter *adapter, void *context);

/** One nonblocking submission. A full queue drops the new event. */
bool SfuEventEgressTrySubmit(SfuEventEgress *egress, const SfuEvent *event);

/** Caller has joined all producers. Joins the worker and frees the queue. */
void SfuEventEgressStop(SfuEventEgress *egress);

#endif
