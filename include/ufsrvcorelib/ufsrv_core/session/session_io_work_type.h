/**
 * @file session_io_work_type.h
 * @brief session_io_work_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 17/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_SESSION_IO_WORK_TYPE_H
#define UFSRVCORELIB_SESSION_IO_WORK_TYPE_H
#include <stdbool.h>
#include <stdint.h>

/*! Unit of session I/O work handed to a session worker. */
typedef struct SessionIoWork {
  void          *session;
  unsigned long  session_id;
  int            fd;
  uint32_t       events;
  uint64_t       not_before_ns;
  bool           retry_read;
  bool           retry_write;
  uint64_t       generation;
} SessionIoWork;

/*! Registration record carried by a queued slot. */
typedef struct SessionIoRegistration {
  struct SessionIoRegistration *next;
  SessionIoWork                 identity;
} SessionIoRegistration;

/*! Queue slot for a session's pending I/O. */
typedef struct SessionIoSlot {
  struct SessionIoSlot  *next;
  SessionIoWork          work;
  uint64_t               retry_after_ns;
  bool                   queued;
  bool                   held;
  SessionIoRegistration  registration;
} SessionIoSlot;

typedef struct SessionIoQueue {
  SessionIoSlot *head;
  SessionIoSlot *tail;
  size_t count;
  uint64_t retry_at_ns;
} SessionIoQueue;

typedef enum SessionIoAcquire {
  SESSION_IO_ACQUIRED,
  SESSION_IO_DEFER,
  SESSION_IO_RETIRED,
  SESSION_IO_BLEED
} SessionIoAcquire;

/*! Outcome of a hand-off attempt between two held I/O slots. */
typedef enum SessionIoHandoff {
  SESSION_IO_HANDOFF_REJECTED = 0,  ///< Not moved; the caller keeps the slot
  SESSION_IO_HANDOFF_MOVED    = 1,  ///< Ownership moved to the destination slot
  SESSION_IO_HANDOFF_DEST_PIN = 2   ///< Destination pins the cell; accept without moving
} SessionIoHandoff;

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

#endif //UFSRVCORELIB_SESSION_IO_WORK_TYPE_H
