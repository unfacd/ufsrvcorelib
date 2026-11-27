#ifndef UFSRVCORELIB_SESSION_IO_WORK_H
#define UFSRVCORELIB_SESSION_IO_WORK_H

#include <stddef.h>
#include <stdbool.h>
#include <ufsrv_core/delegator_sessions/session_io_state.h>
#include <ufsrv_core/session/session_io_work_type.h>

#include <ufsrv_core/ufsrvresult_type.h>

#if 0
typedef enum SessionIoAcquire {
  SESSION_IO_ACQUIRED,
  SESSION_IO_DEFER,
  SESSION_IO_RETIRED,
  SESSION_IO_BLEED
} SessionIoAcquire;

enum {
  SESSION_IO_TAKE_NONE = 0,
  SESSION_IO_TAKE_READY = 1,
  SESSION_IO_TAKE_BLEED = 2
};

typedef struct SessionIoReadResult {
  bool live;
  bool progress;
  bool deferred;
  uint64_t retry_at_ns;
} SessionIoReadResult;

typedef struct SessionIoCallbacks {
  SessionIoReadResult (*read)(void *);
  void (*write)(void *);
  void (*close)(void *);
  void (*defer)(void *, uint32_t, uint64_t);
  void (*retry_terminal)(void *, uint32_t);
} SessionIoCallbacks;
#endif

bool SessionIoSubmit(SessionIoQueue *, SessionIoSlot *, const SessionIoWork *);
uint64_t SessionIoRegister(SessionIoQueue *, SessionIoSlot *, const SessionIoWork *, void **);
void SessionIoUnregister(SessionIoSlot *);
bool SessionIoResolve(uint64_t, SessionIoWork *);
int SessionIoTake(SessionIoQueue *, uint64_t, SessionIoAcquire (*)(const SessionIoWork *, void *),
                  void (*)(const SessionIoWork *, void *), void *, SessionIoWork *);
bool SessionIoCollectHeld(SessionIoSlot *, SessionIoWork *);
bool SessionIoFinishHeld(SessionIoQueue *, SessionIoSlot *, const SessionIoWork *, bool);
void SessionIoCancel(SessionIoQueue *, void (*)(const SessionIoWork *, void *), void *);
void SessionIoContinuation(SessionIoWork *, uint32_t, bool);
SessionIoReadResult SessionIoReadOutcome(const UFSRVResult *, bool, bool, uint64_t, uint64_t);
void SessionIoDispatch(const SessionIoWork *, const SessionIoCallbacks *, void *);

#endif
