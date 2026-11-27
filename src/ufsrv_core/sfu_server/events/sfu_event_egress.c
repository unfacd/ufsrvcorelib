/** Server-owned event queue and single-consumer worker. */

#include "sfu_event_egress.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <uflib/cdt/lockless_ringbuffer/cdt_lockless_ringbuffer.h>

enum { SFU_EVENT_EGRESS_DRAIN_MS = 1000 };

struct SfuEventEgress {
  LocklessRingBuffer *queue;
  int wake_fd;
  pthread_t thread;
  SfuEventEgressAdapter adapter;
  void *context;
  atomic_bool stopping;
  uint64_t drain_deadline_ms; /* published by stopping */
  atomic_uint_fast64_t queue_full;
  uint64_t consumed;
  uint64_t undelivered;
};

static uint64_t
sMonotonicMs(void)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return UINT64_MAX;
  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void
sWake(const SfuEventEgress *egress)
{
  /* EAGAIN means a wake is already pending on the nonblocking descriptor. */
  while (eventfd_write(egress->wake_fd, 1) < 0 && errno == EINTR) {
  }
}

static void *
sRun(void *context)
{
  SfuEventEgress *egress = context;
  struct pollfd wake = {.fd = egress->wake_fd, .events = POLLIN};
  uint32_t next_sequence = 1;

  for (;;) {
    const bool stopping = atomic_load_explicit(&egress->stopping, memory_order_acquire);
    if (stopping && sMonotonicMs() >= egress->drain_deadline_ms) break;

    SfuEvent event;
    if (LocklessRingBufferTryPop(egress->queue, &event)) {
      event.seq = next_sequence++;
      if (egress->adapter.submit && egress->adapter.submit(egress->context, &event)) {
        ++egress->consumed;
      } else {
        ++egress->undelivered;
      }
      continue;
    }
    if (stopping) break;

    const int ready = poll(&wake, 1, -1);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0 || !(wake.revents & POLLIN)) {
      syslog(LOG_ERR, "%s: SFU event worker wake poll failed", __func__);
      break;
    }
    eventfd_t count;
    if (eventfd_read(egress->wake_fd, &count) < 0 && errno != EINTR && errno != EAGAIN) {
      syslog(LOG_ERR, "%s: SFU event worker wake read failed", __func__);
      break;
    }
  }
  if (egress->adapter.finish) {
    const bool stopping = atomic_load_explicit(&egress->stopping, memory_order_acquire);
    egress->adapter.finish(egress->context, stopping ? egress->drain_deadline_ms : 0);
  }
  return NULL;
}

SfuEventEgress *
SfuEventEgressStart(size_t capacity, const SfuEventEgressAdapter *adapter, void *context)
{
  SfuEventEgress *egress = calloc(1, sizeof(*egress));
  if (!egress) return NULL;

  if (adapter) egress->adapter = *adapter;
  egress->context = context;
  atomic_init(&egress->stopping, false);
  atomic_init(&egress->queue_full, 0);

  egress->queue = LocklessRingBufferCreate(capacity, sizeof(SfuEvent), LOCKLESS_RINGBUF_MODE_MPSC);
  if (!egress->queue) goto fail_queue;

  egress->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (egress->wake_fd < 0) goto fail_wake;

  const int error = pthread_create(&egress->thread, NULL, sRun, egress);
  if (error == 0) return egress;
  syslog(LOG_ERR, "%s: could not start SFU event worker (error %d)", __func__, error);

  close(egress->wake_fd);
fail_wake:
  LocklessRingBufferDestroy(egress->queue);
fail_queue:
  free(egress);
  return NULL;
}

bool
SfuEventEgressTrySubmit(SfuEventEgress *egress, const SfuEvent *event)
{
  if (!egress || !event) return false;
  if (!LocklessRingBufferTryPush(egress->queue, event)) {
    atomic_fetch_add_explicit(&egress->queue_full, 1, memory_order_relaxed);
    return false;
  }
  sWake(egress);
  return true;
}

void
SfuEventEgressStop(SfuEventEgress *egress)
{
  if (!egress) return;

  const uint64_t now = sMonotonicMs();
  egress->drain_deadline_ms = now == UINT64_MAX ? now : now + SFU_EVENT_EGRESS_DRAIN_MS;
  atomic_store_explicit(&egress->stopping, true, memory_order_release);
  sWake(egress);
  pthread_join(egress->thread, NULL);

  const size_t remaining = LocklessRingBufferSize(egress->queue);
  const uint64_t full = atomic_load_explicit(&egress->queue_full, memory_order_relaxed);
  syslog(LOG_INFO, "%s: SFU event egress accepted_by_adapter=%llu queue_full=%llu undelivered=%llu abandoned=%zu",
         __func__, (unsigned long long)egress->consumed, (unsigned long long)full,
         (unsigned long long)egress->undelivered, remaining);

  close(egress->wake_fd);
  LocklessRingBufferDestroy(egress->queue);
  free(egress);
}
