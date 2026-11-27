#ifndef UFSRVCORELIB_SESSION_IO_STATE_H
#define UFSRVCORELIB_SESSION_IO_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <ufsrv_core/session/session_io_work_type.h>

extern SessionIoQueue session_io_queue;

/**
 * @brief Connections queued for a session worker and not yet taken.
 *
 * @return Best-effort snapshot of the dispatch backlog.
 */
PUBLIC_API size_t SessionIoQueueDepth(void);

PUBLIC_API uint64_t SessionIoGeneration(const SessionIoSlot *);
PUBLIC_API SessionIoHandoff SessionIoHandoffHeld(SessionIoQueue *, SessionIoSlot *, SessionIoSlot *, const SessionIoWork *, const SessionIoWork *);

#endif
