/**
 * @file sesstion_stat_flags.h
 * @brief sesstion_stat_flags
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 19/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_SESSTION_STAT_FLAGS_H
#define UFSRVCORELIB_SESSTION_STAT_FLAGS_H

//various session status flags
#define SESNSTATUS_SET(x,y)						((x) |= (y))
#define SESNSTATUS_UNSET(x,y)					((x) &= ~(y))
#define SESNSTATUS_IS_SET(x,y)				((x) & (y))

#define SESNSTATUS_HANDSHAKED					(0x1UL<<1UL)
#define SESNSTATUS_AUTHENTICATED			(0x1UL<<2UL)
#define SESNSTATUS_CONNECTED					(0x1UL<<3UL)//mutually exclusive with SUSEND both cannot be on at the same time
#define SESNSTATUS_SUSPENDED	 			  (0x1UL<<4UL)//previously connected|authenticated|handshaked... fully suspended
#define SESNSTATUS_RECYCLED						(0x1UL<<5UL)//currently in recycler cache
#define SESNSTATUS_DEFERRED_RECYCLE		(0x1UL<<6UL)//session marked as soft suspended and is ready to be recycled
#define SESNSTATUS_IDLED							(0x1UL<<7UL)//idle for longer than set idle period, did not respond to ping msg
#define SESNSTATUS_INSERVICE			    (0x1UL<<8UL)//curently being serviced by a thread
#define SESNSTATUS_SECURE		  			  (0x1UL<<9UL)//SSL
#define SESNSTATUS_IOERROR						(0x1UL<<10UL)//in msg queue
#define SESNSTATUS_LOCATED						(0x1UL<<11UL)//in location declated
#define SESNSTATUS_MIGRATED						(0x1UL<<12UL)//session was migrated from another backend
#define SESNSTATUS_REMOTE							(0x1UL<<13UL)//session referenced by association, with which we don't maintain a direct connection with session
#define SESNSTATUS_REMOTE_CONNECTED		(0x1UL<<14UL)//remote and connected: we especially mark this, but otherwise we reuse SUSPENDED, RECYCLED,
#define SESNSTATUS_FENCELIST_LAZY			(0x1UL<<15UL)//remote user with incomplete fence list, being built on demand
#define SESNSTATUS_QUIT								(0x1UL<<16UL)//user quit on request or by the system after certain inactivity cycle
#define SESNSTATUS_RECYCLEREQUEST			(0x1UL<<17UL)//request to rerun session through Delegor IO cycle
#define SESNSTATUS_EPHEMERAL					(0x1UL<<18UL)//session is being serviced  outside the context of direct IO mode, eg by a worker thread
#define SESNSTATUS_UNDERCONSTRUCTION 	(0x1UL<<19UL)//session not fully built yet not all values assigned
#define SESNSTATUS_FAULTY							(0x1UL<<20UL)//session has faulty elements and cannot be used for I/O
#define SESNSTATUS_IPC								(0x1UL<<21UL)//session is of type IPC ie not regular IO worker session
#define SESNSTATUS_SNAPSHOT						(0x1UL<<22UL)//session that is purely used for data representation and therefore not connected or visible other than to creator
#define SESNSTATUS_CARRIER						(0x1UL<<23UL)//session that is not associatd with real user and not visible in hashes.
#define SESNSTATUS_TRANSIENT					(0x1UL<<24UL)//session is allocated for a new SessionWorker request as a temporary holder, until real session is located. Similar to Ephemeral for UfsrvWorker
#define SESNSTATUS_CONNECTION_LISTENER (0x1UL<<25UL)//session is of type connection listener

// A dedicated worker owns socket closure and teardown.
#define SESNSTATUS_WORKER_OWNS_SOCKET (0x1UL<<26UL)

#endif //UFSRVCORELIB_SESSTION_STAT_FLAGS_H
