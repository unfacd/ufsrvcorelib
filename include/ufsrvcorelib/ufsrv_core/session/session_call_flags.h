/**
 * @file session_state_flags.h
 * @brief session_state_flags
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 18/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_SESSION_CALL_FLAGS_H
#define UFSRVCORELIB_SESSION_CALL_FLAGS_H

//function call flags
#define CALLFLAG_SET(x,y)			(x|=y)
#define CALLFLAG_UNSET(x,y)			(x&=~y)
#define CALLGFLAG_IS_SET(x,y)		(x&y)

#define SESSION_CALLFLAGS_EMPTY											0
#define CALL_FLAG_LOCK_SESSION											(0x1UL<<1UL)
#define CALL_FLAG_HASH_SESSION_LOCALLY							(0x1UL<<2UL)
#define CALL_FLAG_REMOTE_SESSION										(0x1UL<<3UL)//set it as remote session
#define CALL_FLAG_SUSSPEND_SESSION									(0x1UL<<4UL)
#define CALL_FLAG_WRITEBACK_FENCE_DATA_TO_BACKEND		(0x1UL<<5UL)//includes msgqueue broadcast
#define CALL_FLAG_WRITEBACK_SESSION_DATA_TO_BACKEND	(0x1UL<<6UL)
#define CALL_FLAG_SNAPSHOT_INSTANCE									(0x1UL<<7UL)
#define CALL_FLAG_SEARCH_BACKEND										(0x1UL<<8UL)
#define CALL_FLAG_HASH_UID_LOCALLY									(0x1UL<<9UL)
#define CALL_FLAG_SEARCH_LOCAL											(0x1UL<<10UL) //local hash
#define CALL_FLAG_UNLOCK_SESSION										(0x1UL<<11UL)
#define CALL_FLAG_ATTACH_FENCE_LIST_TO_SESSION			(0x1UL<<12UL)//build user's  list of fences
#define CALL_FLAG_SESSION_FENCE_APPEND							(0x1UL<<13UL)//append to existing user fence list, ensuring no duplication
#define CALL_FLAG_SESSION_FENCE_REBUILD							(0x1UL<<14UL)//rebuild user fence list
#define CALL_FLAG_CARRIER_INSTANCE									(0x1UL<<15UL)//
#define CALL_FLAG_FENCE_LIST_CHECK_DUP_SESSION			(0x1UL<<16UL)//check if session is in Fence Session list
#define CALL_FLAG_SESSION_LIST_CHECK_DUP_FENCE			(0x1UL<<17UL)//check if fence is in Session fence list
#define CALL_FLAG_DONT_LOCK_SESSION									(0x1UL<<18UL)
#define CALL_FLAG_SESSION_LIST_INCLUDE_REMOTE				(0x1UL<<19UL)//whci compliling sessions list from fence, include remote sessions
#define CALL_FLAGS_KILL_SESSION											(0x1UL<<20UL)//dont' send session into recycler: release all memory associated with it
#define CALL_FLAG_SWAP_COOKIE												(0x1UL<<21UL)
#define CALL_FLAG_DONT_BROADCAST_FENCE_EVENT				(0x1UL<<22UL)//announce fence event trough msgqueue, but not write back
#define CALL_FLAG_BROADCAST_SESSION_EVENT						(0x1UL<<23UL)//
#define CALL_FLAG_DONT_BROADCAST_SESSION_EVENT			(0x1UL<<24UL)//
#define CALL_FLAG_USER_AUTHENTICATED								(0x1UL<<25UL)//user already (password)authenticated
#define CALL_FLAG_USER_SIGNON												(0x1UL<<26UL)//sign user on
#define CALL_FLAG_HASH_USERNAME_LOCALLY							(0x1UL<<27UL)
#define CALL_FLAG_SELF_DESTRUCT											(0x1UL<<28UL)
#define CALL_FLAG_LOCTION_BY_USER										(0x1UL<<29UL)
#define CALL_FLAG_LOCATION_BY_SERVER									(0x1UL<<30UL)
#define CALL_FLAG_TRANSFER_DB_USERDATA							(0x1UL<<31UL)//session hold freshly sourced DB data
//#define CALL_FLAG_REBOOT_SESSION										(0x1UL<<32UL)//for session provider: load session data from db backend and propogate up
#define CALL_FLAG_DUP_SESSION_DATA									(0x1UL<<33UL)//duplicate dynamic data as opposed to copying by reference
#define CALL_FLAG_LOCK_SESSION_BLOCKING							(0x1UL<<34UL)//lock session and block if necessary
#define CALL_FLAG_TRANSFER_SESSION_ACCESS_CONTEXT		(0x1UL<<35UL)
#define CALL_FLAG_TRANSFER_WORKER_ACCESS_CONTEXT		(0x1UL<<36UL)
#define CALL_FLAG_INSTANTIATE_FROM_SYSTEM_USER			(0x1UL<<37UL)//when instantiating a session, assume the singular identity of ufsrv system user
#define CALL_FLAG_LOAD_DB_BACKEND_FOR_SESSION				(0x1UL<<38UL)//load user's data from db backend

#endif //UFSRVCORELIB_SESSION_CALL_FLAGS_H
