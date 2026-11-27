#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <ufsrv_core/delegator_sessions/session_io_work.h>
#include <sys/epoll.h>

SessionIoQueue session_io_queue;

#define SESSION_IO_REGISTRATION_BUCKETS 4096u
static SessionIoRegistration *registrations[SESSION_IO_REGISTRATION_BUCKETS];
static uint64_t last_generation;

/**
 * @brief Connections queued for a session worker and not yet taken.
 *
 * The count rises when the delegator submits work and falls when a worker takes
 * it, so it is the backlog waiting on a free worker. Slots held by a worker are
 * already out of the queue and are not counted.
 *
 * @return A relaxed, best-effort snapshot; the queue is mutated by the delegator
 *         and by session workers without a lock.
 */
PUBLIC_API size_t
SessionIoQueueDepth(void)
{
  return __atomic_load_n(&session_io_queue.count, __ATOMIC_RELAXED);
}

PUBLIC_API uint64_t
SessionIoGeneration(const SessionIoSlot *slot)
{
  return __atomic_load_n(&slot->registration.identity.generation, __ATOMIC_ACQUIRE);
}

static bool
sSameIdentity(const SessionIoWork *left, const SessionIoWork *right)
{
  return left->session == right->session && left->session_id == right->session_id &&
         left->fd == right->fd && left->generation == right->generation;
}

static void
sAppend(SessionIoQueue *queue, SessionIoSlot *slot)
{
  slot->next = NULL;
  if (queue->tail) queue->tail->next = slot;
  else queue->head = slot;
  queue->tail = slot;
  queue->count++;
}

static SessionIoSlot *
sPop(SessionIoQueue *queue)
{
  SessionIoSlot *slot = queue->head;
  queue->head = slot->next;
  if (!queue->head) queue->tail = NULL;
  queue->count--;
  slot->next = NULL;
  return slot;
}

static bool
sPending(const SessionIoWork *work)
{
  return work->events || work->retry_read || work->retry_write;
}

static void
sMerge(SessionIoWork *into, const SessionIoWork *work)
{
  into->events |= work->events;
  into->retry_read |= work->retry_read;
  into->retry_write |= work->retry_write;
  if (into->not_before_ns < work->not_before_ns) into->not_before_ns = work->not_before_ns;
  if (!into->session) into->session = work->session;
}

static void
sUnlink(SessionIoQueue *queue, SessionIoSlot *slot)
{
  SessionIoSlot **link = &queue->head;
  SessionIoSlot *previous = NULL;
  while (*link && *link != slot) {
    previous = *link;
    link = &(*link)->next;
  }
  if (!*link) {
    slot->queued = false;
    slot->next = NULL;
    return;
  }
  *link = slot->next;
  if (queue->tail == slot) queue->tail = previous;
  queue->count--;
  slot->next = NULL;
  slot->queued = false;
}

bool
SessionIoSubmit(SessionIoQueue *queue, SessionIoSlot *slot, const SessionIoWork *work)
{
  if (work->generation != SessionIoGeneration(slot)) return false;
  if (slot->held) {
    if (sSameIdentity(&slot->work, work)) sMerge(&slot->work, work);
    return false;
  }
  if (slot->queued) {
    if (sSameIdentity(&slot->work, work)) {
      sMerge(&slot->work, work);
    } else {
      slot->work = *work;
      slot->retry_after_ns = 0;
    }
    return false;
  }
  slot->work = *work;
  slot->queued = true;
  slot->retry_after_ns = 0;
  sAppend(queue, slot);
  return true;
}

uint64_t
SessionIoRegister(SessionIoQueue *queue, SessionIoSlot *slot, const SessionIoWork *identity, void **retired)
{
  *retired = NULL;
  if (SessionIoGeneration(slot) || last_generation >= UINT64_MAX - 1) return 0;
  if (slot->queued) {
    *retired = slot->work.session;
    sUnlink(queue, slot);
  }
  slot->held = false;
  slot->retry_after_ns = 0;
  slot->work = *identity;
  slot->work.events = 0;
  slot->work.retry_read = false;
  slot->work.retry_write = false;
  slot->work.not_before_ns = 0;
  slot->work.generation = ++last_generation;
  SessionIoRegistration *entry = &slot->registration;
  entry->identity.session = slot->work.session;
  entry->identity.session_id = slot->work.session_id;
  entry->identity.fd = slot->work.fd;
  entry->identity.events = 0;
  entry->identity.retry_read = false;
  entry->identity.retry_write = false;
  entry->identity.not_before_ns = 0;
  __atomic_store_n(&entry->identity.generation, slot->work.generation, __ATOMIC_RELEASE);
  size_t bucket = slot->work.generation % SESSION_IO_REGISTRATION_BUCKETS;
  entry->next = registrations[bucket];
  registrations[bucket] = entry;
  return slot->work.generation;
}

void
SessionIoUnregister(SessionIoSlot *slot)
{
  SessionIoRegistration *entry = &slot->registration;
  uint64_t generation = SessionIoGeneration(slot);
  if (!generation) return;
  SessionIoRegistration **link = &registrations[generation % SESSION_IO_REGISTRATION_BUCKETS];
  while (*link && *link != entry) link = &(*link)->next;
  if (*link) *link = entry->next;
  entry->next = NULL;
  __atomic_store_n(&entry->identity.generation, 0, __ATOMIC_RELEASE);
}

bool
SessionIoResolve(uint64_t generation, SessionIoWork *identity)
{
  if (!generation) return false;
  for (SessionIoRegistration *entry = registrations[generation % SESSION_IO_REGISTRATION_BUCKETS];
       entry; entry = entry->next) {
    if (entry->identity.generation == generation) {
      *identity = entry->identity;
      return true;
    }
  }
  return false;
}

static void
sClearReadableIoWork(SessionIoWork *work)
{
  work->events &= ~(uint32_t)(EPOLLIN | EPOLLERR);
  work->retry_read = true;
}

int
SessionIoTake(SessionIoQueue *queue, uint64_t now, SessionIoAcquire (*acquire)(const SessionIoWork *, void *),
              void (*release)(const SessionIoWork *, void *), void *context, SessionIoWork *work)
{
  size_t candidates = queue->count;
  queue->retry_at_ns = UINT64_MAX;
  while (candidates--) {
    SessionIoSlot *slot = sPop(queue);
    if (slot->retry_after_ns > now || slot->work.not_before_ns > now) {
      uint64_t deadline = slot->retry_after_ns > slot->work.not_before_ns ? slot->retry_after_ns : slot->work.not_before_ns;
      if (queue->retry_at_ns > deadline) queue->retry_at_ns = deadline;
      sAppend(queue, slot);
      continue;
    }
    SessionIoAcquire result = acquire(&slot->work, context);
    if (result == SESSION_IO_DEFER) {
      slot->retry_after_ns = now + 1000000;
      if (queue->retry_at_ns > slot->retry_after_ns) queue->retry_at_ns = slot->retry_after_ns;
      sAppend(queue, slot);
      continue;
    }
    if (result == SESSION_IO_BLEED) {
      *work = slot->work;
      sClearReadableIoWork(&slot->work);
      slot->retry_after_ns = now + 1000000;
      if (queue->retry_at_ns > slot->retry_after_ns) queue->retry_at_ns = slot->retry_after_ns;
      sAppend(queue, slot);
      return SESSION_IO_TAKE_BLEED;
    }
    slot->queued = false;
    if (result == SESSION_IO_RETIRED) {
      release(&slot->work, context);
      continue;
    }
    SessionIoWork claimed = slot->work;
    slot->held = true;
    slot->work.events = 0;
    slot->work.retry_read = false;
    slot->work.retry_write = false;
    slot->work.not_before_ns = 0;
    slot->retry_after_ns = 0;
    *work = claimed;
    return SESSION_IO_TAKE_READY;
  }
  return SESSION_IO_TAKE_NONE;
}

bool
SessionIoCollectHeld(SessionIoSlot *slot, SessionIoWork *out)
{
  if (!slot->held || slot->work.generation != SessionIoGeneration(slot) ||
      !sPending(&slot->work)) return false;
  *out = slot->work;
  slot->work.events = 0;
  slot->work.retry_read = false;
  slot->work.retry_write = false;
  slot->work.not_before_ns = 0;
  return true;
}

bool
SessionIoFinishHeld(SessionIoQueue *queue, SessionIoSlot *slot, const SessionIoWork *snapshot, bool publish)
{
  if (!slot->held || slot->queued || !sSameIdentity(&slot->work, snapshot)) return false;
  if (publish && SessionIoGeneration(slot) != snapshot->generation) return false;
  if (publish) sMerge(&slot->work, snapshot);
  slot->held = false;
  if (!publish) {
    slot->work.events = 0;
    slot->work.retry_read = false;
    slot->work.retry_write = false;
    slot->work.not_before_ns = 0;
    slot->retry_after_ns = 0;
    return false;
  }
  if (!sPending(&slot->work)) return false;
  slot->queued = true;
  slot->retry_after_ns = 0;
  sAppend(queue, slot);
  return true;
}

PUBLIC_API SessionIoHandoff
SessionIoHandoffHeld(SessionIoQueue *queue, SessionIoSlot *from, SessionIoSlot *to,
                     const SessionIoWork *source, const SessionIoWork *dest)
{
  bool dest_pin;
  uint64_t not_before;
  uint64_t retry_after;
  if (!from || !to || from == to || !from->held) return SESSION_IO_HANDOFF_REJECTED;
  if (!sSameIdentity(&from->work, source)) return SESSION_IO_HANDOFF_REJECTED;
  if (to->held) return SESSION_IO_HANDOFF_REJECTED;
  if (SessionIoGeneration(to) != dest->generation) return SESSION_IO_HANDOFF_REJECTED;
  if (!sSameIdentity(&to->work, dest) &&
      (to->queued || sPending(&to->work)))
    return SESSION_IO_HANDOFF_REJECTED;
  dest_pin = to->queued;
  not_before = to->work.not_before_ns;
  retry_after = to->retry_after_ns;
  to->work.events |= from->work.events;
  to->work.retry_read = to->work.retry_read || from->work.retry_read;
  to->work.retry_write = to->work.retry_write || from->work.retry_write;
  to->work.not_before_ns = not_before;
  to->retry_after_ns = retry_after;
  to->work.session = dest->session;
  to->work.session_id = dest->session_id;
  to->work.fd = dest->fd;
  to->work.generation = dest->generation;
  if (dest_pin) sUnlink(queue, to);
  from->held = false;
  from->work.events = 0;
  from->work.retry_read = false;
  from->work.retry_write = false;
  from->work.not_before_ns = 0;
  to->held = true;
  return dest_pin ? SESSION_IO_HANDOFF_DEST_PIN : SESSION_IO_HANDOFF_MOVED;
}

void
SessionIoCancel(SessionIoQueue *queue, void (*release)(const SessionIoWork *, void *), void *context)
{
  while (queue->head) {
    SessionIoSlot *slot = sPop(queue);
    slot->queued = false;
    release(&slot->work, context);
  }
}

void
SessionIoContinuation(SessionIoWork *work, uint32_t retry, bool requested_write)
{
  work->events = retry & (EPOLLHUP | EPOLLRDHUP | EPOLLERR);
  work->retry_read = (retry & EPOLLIN) != 0;
  work->retry_write = (requested_write || (retry & EPOLLOUT)) && !(retry & (EPOLLHUP | EPOLLRDHUP));
}

SessionIoReadResult
SessionIoReadOutcome(const UFSRVResult *result, bool connection_valid, bool progress,
                     uint64_t now, uint64_t retry_at)
{
  if (!connection_valid || !result || result->result_code == RESCODE_IO_CONNECTIONCLOSED ||
      result->result_code == RESCODE_SESN_SOFTSPENDED || result->result_code == RESCODE_SESN_HARDSPENDED)
    return (SessionIoReadResult){0};
  if (_RESULT_TYPE_SUCCESS(result))
    return (SessionIoReadResult){.live = true, .progress = progress};
  uint64_t fallback = UINT64_MAX - now < 100000000 ? UINT64_MAX : now + 100000000;
  return (SessionIoReadResult){.live = true, .progress = progress, .deferred = true,
                               .retry_at_ns = retry_at > now ? retry_at : fallback};
}

void
SessionIoDispatch(const SessionIoWork *work, const SessionIoCallbacks *callbacks, void *context)
{
  uint32_t terminal = work->events & (EPOLLHUP | EPOLLRDHUP);
  if ((work->events & EPOLLERR) && !terminal) {
    callbacks->close(context);
    return;
  }
  if ((work->events & EPOLLIN) || work->retry_read || terminal) {
    SessionIoReadResult result = callbacks->read(context);
    if (!result.live) return;
    if (result.deferred) {
      uint32_t pending = work->events | EPOLLIN;
      if (work->retry_write) pending |= EPOLLOUT;
      if (terminal) pending &= ~EPOLLOUT;
      callbacks->defer(context, pending, result.retry_at_ns);
      return;
    }
    if (terminal) {
      if (result.progress) callbacks->retry_terminal(context, terminal);
      else callbacks->close(context);
      return;
    }
  }
  if ((work->events & EPOLLOUT) || work->retry_write) callbacks->write(context);
}
