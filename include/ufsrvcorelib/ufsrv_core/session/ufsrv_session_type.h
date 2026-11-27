/**
 * @file ufsrv_session_type.h
 * @brief Core (transport / I/O lifecycle) partition of the ufsrv session.
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
 */

#ifndef UFSRVCORELIB_UFSRV_SESSION_TYPE_H
#define UFSRVCORELIB_UFSRV_SESSION_TYPE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#include <uflib/net/socket_type.h>
#include <uflib/recycler/instance_type.h>
#include <uflib/standard_ssl_includes.h>

#define AS_UFSRV_SESSION(x)	((UfsrvSession *)(x))

#ifdef MPSC_MESSAGE_QUEUE
# include <uflib/adt/adt_mpsc_queue_type.h>
#else
# include <uflib/adt/adt_queue.h>
#endif

#include "session_io_work_type.h"
#include <ufsrv_core/ufsrvresult_type.h>
#include "session_call_flags.h"

/* Temporary: mirrors the network-domain configuration default until the
 * cookie size constant is owned by ufsrvcorelib. Guarded so that including
 * the network-domain configuration does not redefine it. */
#ifndef CONFIG_MAX_COOKIE_SZ
# define CONFIG_MAX_COOKIE_SZ 64
#endif


/*! Stage of an I/O poll callback, pre or post the polled event. */
enum IOHandlerEventStage {
  PRE_HANDLER = 1,  ///< Invoked before the event is handled
  POST_HANDLER      ///< Invoked after the event is handled
};

/*! Geo-affinity group used for session routing. */
typedef enum EnumServerGeoGroup {
  GEOGROUP_APAC = 1,
  GEOGROUP_APAC_Singapore,
  GEOGROUP_APAC_Sydney,
  GEOGROUP_APAC_Tokyo,
  GEOGROUP_APAC_Seoul,
  GEOGROUP_APAC_Mumbai,
  GEOGROUP_EU = 100,
  GEOGROUP_EU_Ireland,
  GEOGROUP_EU_Frankfurt,
  GEOGROUP_US_WEST = 200,
  GEOGROUP_US_WEST_Oregon,
  GEOGROUP_US_WEST_NorthernCalifornia,
  GEOGROUP_US_EAST = 250,
  GEOGROUP_US_EAST_Northern_Virginia,
  GEOGROUP_US_EAST_Ohio,
  GEOGROUP_SOUTH_AMERICA = 300,
  GEOGROUP_AFRICA = 400,
  GEOGROUP_CHINA = 500,
  GEOGROUP_ME = 600
} EnumSessionGeoGroup;

typedef EnumSessionGeoGroup EnumServerGeoGroup;

/*! Per-session message queue (inbound or outbound). */
struct MessageQueue {
#ifdef MPSC_MESSAGE_QUEUE
  LocklessMpscQueue   queue;
#else
  Queue               queue;      ///< SocketMessage queue
#endif
  pthread_mutex_t     mutex;      ///< Guards queue structure manipulation
  pthread_mutexattr_t mutex_attr;
};
typedef struct MessageQueue MessageQueue;

/* Forward declarations for protocol-bound state. Pointed to, never
 * dereferenced by this header; the protocol implementation defines them. */
struct ProtocolTypeData;
struct ProtocolSessionData;
typedef struct ProtocolTypeData ProtocolTypeData;
typedef struct ProtocolSessionData ProtocolSessionData;


/*! Core partition of the ufsrv session.
 *
 *  Carries the transport and I/O lifecycle: identity, status, sockets,
 *  epoll registration, protocol handles, TLS, the session lock, the
 *  per-direction message queues and the I/O dispatch state.
 *
 *  In the composed session this type occupies the first sizeof(UfsrvSession)
 *  bytes at offset zero; the network-domain extension follows at
 *  offsetof(Session, sservice). The members below are copied verbatim from
 *  the current Session definition and remain there until the composition
 *  lands, so the existing Session * references keep resolving.
 */
typedef struct UfsrvSession {
  EnumSessionGeoGroup  geogroup;                ///< Geo affinity group for routing

  time_t               when_suspended;          ///< Timestamp of last suspension
  time_t               when_serviced_start;     ///< Start of the last I/O service cycle
  time_t               when_serviced_end;       ///< End of the last I/O service cycle

  unsigned long        session_id;              ///< Hashed session identity; do not change the offset
  unsigned long        device_id;               ///< Unique device installation number
  unsigned long        eid;                     ///< Most recent event id
  unsigned long        recycler_id;             ///< Unique id within the recycler lineage

  char                 session_cookie[CONFIG_MAX_COOKIE_SZ + 1]; ///< Hashed auth cookie; do not change the offset
  char                *cm_token;                ///< Cloud-messaging token

  unsigned long        stat;                    ///< SESNSTATUS_* bitmask
  unsigned long        net_address_hash;        ///< Hash of the connecting peer address

  Socket              *dsptr;                   ///< Destination socket
  Socket              *ssptr;                   ///< Source socket

  void                *event_descriptor;        ///< epoll event descriptor

  UFSRVResult          result;                 ///< generic return carrier mechanism

  /*! Per-session I/O poll handlers, invoked from within the protocol. */
  struct {
    int (*on_read)(InstanceHolder *, enum IOHandlerEventStage);
    int (*on_write)(InstanceHolder *, enum IOHandlerEventStage);
  } io_event_handlers;

  ProtocolTypeData    *protocol_type_data;      ///< Fixed reference to the lifecycle protocol
  ProtocolSessionData *protocol_session_data;   ///< Protocol-specific augmentation (HttpSession, McpSession, ...)
  const struct RecyclerPoolHandle *recycler_pool; ///< Originating pool, fixed after first checkout.

  struct {
    SSL *ssl;
  } session_crypto;                             ///< TLS context

  struct {
    pthread_rwlock_t     rwlock;
    pthread_rwlockattr_t rwattr;
  } session_events;                             ///< Mutable session state lock

  MessageQueue         message_queue_in;        ///< Inbound socket message queue
  MessageQueue         message_queue_out;       ///< Outbound socket message queue

  SessionIoSlot        io_work;                 ///< Session I/O dispatch slot
  uint32_t             io_retry_events;         ///< Events deferred for retry
  uint64_t             io_retry_after_ns;       ///< Monotonic retry deadline
} UfsrvSession;

typedef InstanceHolder InstanceHolderForSession;
#define AS_INSTANCE_HOLDER_FOR_SESSION(x, y) InstanceHolderForSession *x = (InstanceHolderForSession *)(y)

typedef struct InstanceContextForSession {
  InstanceHolderForSession *instance_sesn_ptr;
  UfsrvSession *sesn_ptr;
  bool lock_already_owned,
       is_locked;
} InstanceContextForSession;
#define AS_INSTANCE_CONTEXT_FOR_SESSION(x) ((InstanceContextForSession *)(x))

/* Access the fields of a UfsrvSession. */
#define UFSRV_SESSION_ID(x) ((x)->session_id)
#define UFSRV_SESSION_EID(x) ((x)->eid)
#define UFSRV_SESSION_STATUS(x) ((x)->stat)
#define UFSRV_SESSION_DEVICEID(x) ((x)->device_id)
#define UFSRV_SESSION_COOKIE(x) ((x)->session_cookie)
#define UFSRV_SESSION_WHEN_SUSPENDED(x) ((x)->when_suspended)
#define UFSRV_SESSION_WHEN_SERVICE_STARTED(x) ((x)->when_serviced_start)
#define UFSRV_SESSION_WHEN_SERVICED(x) ((x)->when_serviced_end)
#define UFSRV_SESSION_HADDRESS(x) ((x)->ssptr->haddress)
#define UFSRV_SESSION_HPORT(x) ((x)->ssptr->hport)
#define UFSRV_SESSION_PROTOCOL_SESSION_DATA(x) ((x)->protocol_session_data)
#define UFSRV_SESSION_PROTOCOLTYPE(x) ((x)->protocol_type_data)
#define UFSRV_SESSION_CUMMULATIVE_TR(x) ((x)->ssptr->trbytes)
#define UFSRV_SESSION_CUMMULATIVE_RC(x) ((x)->ssptr->rcbytes)
#define UFSRV_SESSION_INCREMENT_TR(x, y) ((x)->ssptr->trbytes += (y))
#define UFSRV_SESSION_INCREMENT_RC(x, y) ((x)->ssptr->rcbytes += (y))
#define UFSRV_SESSION_CMTOKEN(x) ((x)->cm_token)
#define UFSRV_SESSION_NETADDRESS_HASH(x) ((x)->net_address_hash)
#define UFSRV_SESSION_GEOGROUP(x) ((x)->geogroup)
#define UFSRV_SESSION_SOCKET(x) ((x)->ssptr)
#define UFSRV_SESSION_SOCKETFD(x) ((x)->ssptr->sock)
#define UFSRV_SESSION_SOCKETBLOCKSZ(x) ((x)->ssptr->blocksz)
#define UFSRV_SESSION_INSOCKMSG_TRANS(x) ((x)->ssptr->socket_msg)
#define UFSRV_SESSION_INSOCKMSG_TRANS_PTR(x) (&((x)->ssptr->socket_msg))
#define UFSRV_SESSION_INSOCKMSG_TRANS_EMPTY(x) ((x)->ssptr->socket_msg.holding_buffer_msg_size==0)
#define UFSRV_SESSION_OUTSOCKMSG_TRANS(x) ((x)->ssptr->socket_msg_out)
#define UFSRV_SESSION_OUTSOCKMSG_TRANS_PTR(x) (&((x)->ssptr->socket_msg_out))
#define UFSRV_SESSION_INSOCKMSG(x) ((x)->message_queue_in)
#define UFSRV_SESSION_INSOCKMSG_PTR(x) (&((x)->message_queue_in))
#define UFSRV_SESSION_INSOCKMSG_QUEUE(x) ((x)->message_queue_in.queue)
#define UFSRV_SESSION_INSOCKMSG_QUEUE_SIZE(x) ((x)->message_queue_in.queue.nEntries)
#define UFSRV_SESSION_INSOCKMSG_QUEUE_PTR(x) (&((x)->message_queue_in.queue))
#define UFSRV_SESSION_EVENT_HANDLER_ON_READ(x) ((x)->io_event_handlers.on_read)
#define UFSRV_SESSION_EVENT_HANDLER_ON_WRITE(x) ((x)->io_event_handlers.on_write)
#define UFSRV_SESSION_OUTSOCKMSG(x) ((x)->message_queue_out)
#define UFSRV_SESSION_OUTSOCKMSG_PTR(x) (&((x)->message_queue_out))
#define UFSRV_SESSION_OUTSOCKMSG_QUEUE(x) ((x)->message_queue_out.queue)
#define UFSRV_SESSION_OUTSOCKMSG_QUEUE_SIZE(x) ((x)->message_queue_out.queue.nEntries)
#define UFSRV_SESSION_OUTSOCKMSG_QUEUE_PTR(x) (&((x)->message_queue_out.queue))
#define UFSRV_SESSION_RESULT_PTR(x) (&((x)->result))

#endif //UFSRVCORELIB_UFSRV_SESSION_TYPE_H
