/**
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file session_io.c
 * @brief Session socket transport and ordered read and write queues.
 */
#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include "config_ufsrvcorelib.h"
#endif

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <uflib/standard_defs.h>
#include <uflib/utils_str.h>

#include "ufsrv_core/delegator_sessions/delegator_session_worker_thread.h"
#include "ufsrv_core/protocol/protocol.h"
#include "ufsrv_core/session/session_io.h"
#include "ufsrv_core/session/sesstion_stat_flags.h"
#include "ufsrv_core/session/sessions_utils.h"
#include "ufsrv_core/system/log_message_literals.h"
#include "ufsrv_core/ufsrv_config/ufsrv_config_shim.h"

static int _SocketMessageDecode(InstanceHolderForSession *, SocketMessage *, unsigned);

static void
ReportTlsReadError(SSL *ssl, int read_result, int read_error)
{
  int         error  = SSL_get_error(ssl, read_result);
  const char *detail = ERR_error_string(read_error, NULL);

  switch (error) {
    case SSL_ERROR_NONE:
      printf("SSL_ERROR_NONE (%d)(%s)\n", error, detail);
      break;
    case SSL_ERROR_WANT_READ:
      printf("SSL_ERROR_WANT_READ (%d)(%s)\n", error, detail);
      break;
    case SSL_ERROR_WANT_WRITE:
      printf("SSL_ERROR_WANT_WRITE (%d)(%s)\n", error, detail);
      break;
    case SSL_ERROR_ZERO_RETURN:
      printf("SSL_ERROR_ZERO_RETURN (%d)(%s): The TLS/SSL connection has been closed (peer disconnected?).\n", error,
             detail);
      break;
    case SSL_ERROR_SYSCALL: {
      printf("SSL_ERROR_SYSCALL (%d)(%s): Some I/O error occurred \n", error, detail);
      int last_error = ERR_peek_last_error();
      printf("SSL_ERROR_SYSCALL (%d)(%s): Last error in the queue \n", last_error, ERR_error_string(last_error, NULL));
      break;
    }
    case SSL_ERROR_SSL:
      printf("SSL_ERROR_SSL (%d)(%s): A failure in the SSL library occurred, usually a protocol error.\n", error,
             detail);
      break;
    default:
      printf("UNKNOWN SSL ERROR (%d)(%s)\n", error, detail);
      break;
  }
}

/**
 * 	@brief: this reads/queues into Sessions incoming SocketMessage. It operates under the assumption that reader does not own Session lock.
 * 	Since we own MessageQueue lock, we are assured no concurrent read of the socket can take place, or read into transitional incoming buffer
 * 	@param blocksz: Since we don't own the session, we need to know current session's read-block size
 * 	@locked: MessageQueue must be locked by caller
 * 	@dynamic_memory:	allocates a 'SocketMesage *' which if successful freed somehwrere else
 * 	@return: >0 amount of bytes read, 0 would block, -1 io error
 */
inline static ssize_t
_ReadIntoMessageQueueWithNoSessionLock(UfsrvSession *sesn_ptr, int blocksz)
{
  char           erbuf[MBUF] = {0};
  char *         er;
  bool           blocking_io = false;
  SocketMessage *sm_ptr      = calloc(1, sizeof(SocketMessage));

  //allocate buffer
  sm_ptr->_raw_msg = calloc(1, blocksz);

  //this bit is immutable so it maybe safe to read concurrently
  if (SESNSTATUS_IS_SET(sesn_ptr->stat, SESNSTATUS_SECURE)) {
    ERR_clear_error();
    sm_ptr->raw_msg_size = SSL_read(sesn_ptr->session_crypto.ssl, sm_ptr->_raw_msg, blocksz);
  }
  else {
    sm_ptr->raw_msg_size = read(UFSRV_SESSION_SOCKETFD(sesn_ptr), sm_ptr->_raw_msg, blocksz);
    //todo potential data-race condition as we read off the socket value where we don't hold lock on it, it could be in the middle of being destroyed by other thread due to IA error
  }

  int read_errno = (sm_ptr->raw_msg_size < 0) ? errno : 0;

  sm_ptr->sm_errno = 0;

  if (sm_ptr->raw_msg_size > 0) {
    AddToQueue(&(sesn_ptr->message_queue_in.queue), sm_ptr);

    syslog(LOG_DEBUG, LOGSTR_IO_MSGQ_ADD_NODECODE, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
           UFSRV_SESSION_INSOCKMSG_QUEUE_SIZE(sesn_ptr), sm_ptr->raw_msg_size, sm_ptr->missing_msg_size,
           UFSRV_SESSION_CUMMULATIVE_RC(sesn_ptr), LOGCODE_IO_MSGQ_ADD_NODECODE);

    return sm_ptr->raw_msg_size;
  }
  else {
    //ascertain blocking socket status
    if (sm_ptr->raw_msg_size == -1) {
      if (SESNSTATUS_IS_SET(sesn_ptr->stat, SESNSTATUS_SECURE)) {
        //errors other than blocking related https://www.openssl.org/docs/manmaster/ssl/SSL_get_error.html
        int err = SSL_get_error(sesn_ptr->session_crypto.ssl, sm_ptr->raw_msg_size);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) { //open SSL can return either during read
          blocking_io = true;
        }
      }
      else if ((read_errno == EAGAIN) || (read_errno == EWOULDBLOCK)) { // || (read_errno==EINPROGRESS)
        blocking_io = true;
      }

      if (blocking_io) {
        //nothing we can do we just go back status quo unchanged
        syslog(LOG_NOTICE, LOGSTR_IO_BLOCKING_READ, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
               read_errno, LOGCODE_IO_BLOCKING_READ_NO_SESNLOCK);

        free(sm_ptr->_raw_msg);
        free(sm_ptr);

        return 0;
      }

      sm_ptr->sm_errno = read_errno ? read_errno : EIO;
    }
    else {
      sm_ptr->sm_errno = SOCKMSG_SMERRNO_PEERCLOSED;
    }

    er = (sm_ptr->sm_errno == SOCKMSG_SMERRNO_PEERCLOSED) ? "peer closed" : strerror_r(sm_ptr->sm_errno, erbuf, MBUF);

    //since we don't own the Session's lock, well queue it in as an error exception, to alert at queue consolidation time
    AddToQueue(&(sesn_ptr->message_queue_in.queue), sm_ptr);

    syslog(LOG_DEBUG, LOGSTR_QUEING_EXCEPTION, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
           UFSRV_SESSION_INSOCKMSG_QUEUE_SIZE(sesn_ptr), sm_ptr->sm_errno, er, LOGCODE_IO_QUEINGEXC);

    int sm_errno_queued = sm_ptr->sm_errno;

    //since we read nothing anyway,  we deallocate
    free(sm_ptr->_raw_msg);
    sm_ptr->raw_msg_size = 0;

    return (sm_errno_queued == SOCKMSG_SMERRNO_PEERCLOSED) ? SOCKMSG_SMERRNO_PEERCLOSED : -1;
  }
}

ssize_t
ReadIncomingQueueLocked(UfsrvSession *sesn_ptr)
{
  return _ReadIntoMessageQueueWithNoSessionLock(sesn_ptr, UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr));
}

/**
 *

//
//	@brief	Assumes nonblocking. Should only be used with fully connected session.
//	@brief	The processed buffer can contain more than one frame. Incomplete frame in stored separately in a holding buffer.
//	Can read concurrently into a queued input buffer, where the session lock is own by other thread. To achieve that, pass the following:
//	ReadFromSocket(holder, NULL, SOCKMSG_READSOCKET|SOCKMSG_DONTDECODE|SOCKMSG_DONTOWNSESNLOCK);
//	@param	sock_msg_ptr if NULL 1)create a SocketMessage and attach to incoming SocketMessage 2)used passed buffer
// 	@param	flag=SOCKMSG_READBUFFER the messge is being read from SocketMessage. The queue should be locked in  the calling function
//	@param	flag=SOCKMSG_READSOCKET the message is read from network socket.If @aparam sock_msg_ptr is null, store the message in the incoming message queue
//	@param	flag=SOCKMSG_DONTDECODE message is stored raw. combined with SOCKMSG_READSOCKET (sock_msg_ptr must be NULL) has the effect of enqueueing the message without any processing.
//			this is used when the caller doesn't own the Session lock, so we just read into the Socket's incoming queue.
//	@param	flag=SOCKMSG_DONTOWNSESNLOCK indicates the current thread does not own the lock for the Session, so we are limited to only enqueueing locally created SocketMessageQueue
//			 which must be locked separately to acghieve that effect.
//	@param flag=SOCKMSG_KEEPMSGQUEUE_LOCKED: only applicable if we are reading into the queue
//
//	@locks	Session.MessageQueueIncoming
//	@side-effect	Session object suspended where it is caused by i/o events
// 	sm_ptr->holding_buffer maybe created
 //	@return -2 logical error
// 	@return	-1 socket errot. 0 on protocol close. amount of bytes read if >0
 //	@return 0	amended buffer with more data, but still not complete to process OR blocking read
//
*/
ssize_t
ReadFromSocket(InstanceHolderForSession *instance_sesn_ptr, SocketMessage *sock_msg_ptr, unsigned flag)
{
  int     no_error = 1;
  int     i;
  bool    blocking_io = false;
  ssize_t raw_msg_size;

  SocketMessage *sm_ptr = NULL;

  if (sock_msg_ptr) {
    sm_ptr = sock_msg_ptr;
  }
  else {
  }

  UfsrvSession *sesn_ptr = AS_UFSRV_SESSION(GetInstance(instance_sesn_ptr));

  if (flag & SOCKMSG_READSOCKET) {
    UFSRVResult    res;
    SocketMessage *sm_ptr_consolidated = NULL;

    //this lock ensures one thread at a time owns the socket, but multiple threads can queue for it (provided they lock the queue)
    if ((MessageQueueLock(sesn_ptr, UFSRV_SESSION_INSOCKMSG_PTR(sesn_ptr), 0)) != 0) {
      return -2; //nothing we can do we just go back
    }

    if (!sock_msg_ptr) {
      ssize_t rc = _ReadIntoMessageQueueWithNoSessionLock(sesn_ptr, UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr));

      if (!(flag & SOCKMSG_KEEPMSGQUEUE_LOCKED)) MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));

      return rc;
    }

    //>>> MessageQueue Locked, Session Locked

    //consolidate into transitional buffer
    ConsolidateSocketMessageQueue(sesn_ptr, SOCKMSG_CONSOLIDATE_INSESSION, &res);
    //function should not lock queue in absence of flag
    if (res.result_type == RESULT_TYPE_SUCCESS) {
      //this is the regular transitional buffer: we could have read it directly off sesn_ptr
      sm_ptr_consolidated = (SocketMessage*)res.result_user_data;

      //check for previously flagged I/O error
      if (!(sm_ptr_consolidated->sm_errno == 0)) {
        syslog(LOG_DEBUG, LOGSTR_QUEDIOERR, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
               sm_ptr_consolidated->sm_errno, LOGCODE_TSWORKER_QUEUEDIOERR, "PRE REQUEST SERVICE");

        if (sm_ptr_consolidated->sm_errno == SOCKMSG_SMERRNO_PEERCLOSED) {
          MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));

          return 0;
        }

error_consolidation:
        MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));
        ErrorFromSocket(instance_sesn_ptr, CALLFLAGS_EMPTY);

        return -1;
      }

      //we are good to continue below
    }
    else if ((res.result_type == RESULT_TYPE_ERR) && (res.result_code != RESCODE_LOGIC_EMPTY_RESOURCE)) {
      //too bad we just bail out the session
      syslog(LOG_DEBUG, LOGSTR_IO_BUF_CONSOLIDATION_ERR, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
             LOGCODE_IO_BUF_CONSOLIDATION_ERR);

      goto error_consolidation; //same fate as above
    }
    //end consolidate_msgqueue_pre

    if (sm_ptr->raw_msg_size > 0) {
      //consolidation yielded some message: enlarge raw_msg by block_size so we can append new bytes into it (on success)
      unsigned char *tmp_hb = NULL;
      if (!(tmp_hb = realloc(sm_ptr->_raw_msg, sm_ptr->raw_msg_size + UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr)))) {
        MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));

        return -1;
      }

      //peg the read position of the raw buffer
      sm_ptr->raw_msg_cur_pos = sm_ptr->raw_msg_size;
      //re-point to the newly expanded buffer
      sm_ptr->_raw_msg        = tmp_hb;
    }
    else {
      //perhaps queue was empty, we just allocate new buffer and set sane opening position
      sm_ptr->_raw_msg        = calloc(1, UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr));
      sm_ptr->raw_msg_cur_pos = 0;
      sm_ptr->raw_msg_size    = 0;
    }

    //TODO: atomically check for SESNSTATUS_IOERROR efore reading as it could be set by another thread

    //this bit is immutable so it maybe safe to read concurrently
    if (SESNSTATUS_IS_SET(sesn_ptr->stat, SESNSTATUS_SECURE)) {
      ERR_clear_error();
      raw_msg_size = SSL_read(sesn_ptr->session_crypto.ssl, sm_ptr->_raw_msg + sm_ptr->raw_msg_cur_pos,
                              UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr));
    }
    else {
      //this is not correct as thread may not hold Session mutex for this
      if (false) { //UFSRV_SESSION_SOCKETFD(sesn_ptr)<0)
        syslog(LOG_NOTICE, LOGSTR_IO_INVALID_READ_FD, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
               UFSRV_SESSION_SOCKETFD(sesn_ptr), sm_ptr->missing_msg_size, sm_ptr->raw_msg_size,
               sm_ptr->holding_buffer_msg_size, SESNSTATUS_IS_SET(sesn_ptr->stat, SESNSTATUS_SUSPENDED) ? 1 : 0,
               LOGCODE_IO_INVALID_READ_FD);
      }
      //

      raw_msg_size = read(sesn_ptr->ssptr->sock, sm_ptr->_raw_msg + sm_ptr->raw_msg_cur_pos,
                          UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr));
    }

    sm_ptr->sm_errno = errno;

    if (raw_msg_size == -1) {
      if (unlikely(SESNSTATUS_IS_SET(sesn_ptr->stat, SESNSTATUS_SECURE))) {
        //errors other than blocking related https://www.openssl.org/docs/manmaster/ssl/SSL_get_error.html
        int err          = SSL_get_error(sesn_ptr->session_crypto.ssl, raw_msg_size);
        sm_ptr->sm_errno = err; //we'll reuse it even though technically tis is not system errno
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
          //open SSL can return either during read
          blocking_io = true;
        }
      }
      else if ((sm_ptr->sm_errno == EAGAIN) || (sm_ptr->sm_errno == EWOULDBLOCK) || (sm_ptr->sm_errno == EINPROGRESS)) {
        blocking_io = true;
      }
    }
    //>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    if (raw_msg_size > 0) {
      //we read extra bytes so update total buffer size
      sm_ptr->raw_msg_cur_pos += raw_msg_size; //not sure I need this
      sm_ptr->raw_msg_size    += raw_msg_size;

      UFSRV_SESSION_INCREMENT_RC(sesn_ptr, raw_msg_size);

      //This block is not relevant if we are reading into MessageSocket queue, because SocketMessage is instantiated here so missing size is 0
      //previous msg contained incomplete frame
      if (sm_ptr->missing_msg_size > 0) {
        //what we received doesn't represent the entire length of the missing frame fragment, because the amount we read is not enough
        //this could be caused by the limited 'buffer size' passed to 'read()', or network issues: so we need another read
        syslog(
          LOG_DEBUG,
          "%s (pid:'%lu' cid:'%lu'): NOTICE: SocketMessage contains missing buffer: missing_msg_size='%lu'. Read: raw_msg_size='%lu'. holding_buffer_msg_size='%lu'...",
          __func__, pthread_self(), UFSRV_SESSION_ID(sesn_ptr), sm_ptr->missing_msg_size, sm_ptr->raw_msg_size,
          sm_ptr->holding_buffer_msg_size);

        if (sm_ptr->raw_msg_size < UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr)) {
          //tweak back
          UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr) = UfsrvConfigBufferSizesIncomingBufferSize();
          //TODO: this is not entirely correct use the protocol's value
        }

        //this block terminates
        if (sm_ptr->raw_msg_size < sm_ptr->missing_msg_size) {
          syslog(
            LOG_DEBUG,
            "%s (pid:'%lu' cid:'%lu'): Received '%ld' bytes, but I need '%ld' bytes: growing holding_buffer to new size: '%ld'",
            __func__, pthread_self(), UFSRV_SESSION_ID(sesn_ptr), sm_ptr->raw_msg_size, sm_ptr->missing_msg_size,
            sm_ptr->holding_buffer_msg_size + sm_ptr->raw_msg_size);

          //enlarge the holding buffer, copy new bytes and adjust length, and return
          unsigned char *tmp_hb = realloc(sm_ptr->holding_buffer,
                                          sm_ptr->holding_buffer_msg_size + sm_ptr->raw_msg_size);
          if (!tmp_hb) {
            //memory allocation error: clean up
            free(sm_ptr->_raw_msg);
            sm_ptr->_raw_msg     = NULL;
            sm_ptr->raw_msg_size = 0;
            sm_ptr->sm_errno     = 0;

            if (!(sock_msg_ptr)) {
              free(sm_ptr);
            }

            MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));

            return -2;
          }

success_need_more:
          sm_ptr->holding_buffer = tmp_hb;
          memcpy(sm_ptr->holding_buffer + sm_ptr->holding_buffer_msg_size, sm_ptr->_raw_msg, sm_ptr->raw_msg_size);
          sm_ptr->holding_buffer_msg_size += sm_ptr->raw_msg_size;
          sm_ptr->missing_msg_size        -= sm_ptr->raw_msg_size;

          syslog(
            LOG_DEBUG,
            "%s (pid:'%lu' cid:'%lu'): APPENDED frame fragment: holding_buffer_msg_size='%ld'. Missing: '%ld'",
            __func__, pthread_self(), UFSRV_SESSION_ID(sesn_ptr), sm_ptr->holding_buffer_msg_size,
            sm_ptr->missing_msg_size);

          free(sm_ptr->_raw_msg);
          sm_ptr->_raw_msg     = NULL;
          sm_ptr->raw_msg_size = 0;
          sm_ptr->sm_errno     = 0;

          MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));

          //we still need to read more
          return 0;
        }

        //OK we read as much into _raw_msg, or more than what was previously missing. Do the following steps:

        //1)enlarge holding buffer by additional raw_msg_size factor
        unsigned char *tmp_hb = NULL;
        if (!(tmp_hb = realloc(sm_ptr->holding_buffer, sm_ptr->holding_buffer_msg_size + sm_ptr->raw_msg_size + 10))) {
          //extra space for holding separating '\0' n where we have multiple frames in one buffer
          //memory allocation error: clean up
          free(sm_ptr->_raw_msg);
          sm_ptr->raw_msg_size = 0;
          sm_ptr->sm_errno     = 0;

          if (!sock_msg_ptr) free(sm_ptr);

          MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));

          return -1;
        }

        sm_ptr->holding_buffer = tmp_hb;

        //2)append new frame fragment
        memcpy(sm_ptr->holding_buffer + sm_ptr->holding_buffer_msg_size, sm_ptr->_raw_msg, sm_ptr->raw_msg_size);

        //3)update actual length to reflect joined frame
        sm_ptr->raw_msg_size += sm_ptr->holding_buffer_msg_size;
        //tin_end+=sm_ptr->raw_msg_size;

        syslog(LOG_DEBUG, "%s (pid:'%lu' cid:'%lu'): JOINED FRAME FRAGMENTS: new msg size: '%lu' ", __func__,
               pthread_self(), UFSRV_SESSION_ID(sesn_ptr), sm_ptr->raw_msg_size);

        //4)free fragment and repoint it to full frame
        free(sm_ptr->_raw_msg);
        sm_ptr->_raw_msg = sm_ptr->holding_buffer;

        //reset
        sm_ptr->holding_buffer          = NULL;
        sm_ptr->holding_buffer_msg_size = sm_ptr->missing_msg_size = 0;
        sm_ptr->sm_errno                = 0;
        //we are now ready to feed joined frame into decode and determine if we have a complete frame
      }
      else {
        //do nothing we read postive number of bytes
      }
    }
    else { //we read <=0
      if (raw_msg_size == -1) {
        if (blocking_io) {
          if (sm_ptr->raw_msg_size == 0) { //this is the consolidated SocketMessage
            //nothing we can do: unwind allocation (implies consolidation yielded none)
            syslog(LOG_NOTICE, LOGSTR_IO_BLOCKING_READ, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
                   sm_ptr->sm_errno, LOGCODE_IO_BLOCKING);

            free(sm_ptr->_raw_msg);
            sm_ptr->_raw_msg = NULL;

            sm_ptr->sm_errno = 0;

            MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));

            //we preserve state of existing buffer data
            return 0;
          }

__blocked_read_but_have_consolidated_buffer:

          //perhaps a unique case... we'll have a go at what we consolidated earlier so continue below to decoding
          syslog(LOG_NOTICE, LOGSTR_IO_BLOCKING_READ_WITH_RAW, __func__, pthread_self(), sesn_ptr,
                 UFSRV_SESSION_ID(sesn_ptr), sm_ptr->sm_errno, sm_ptr->raw_msg_size, LOGCODE_IO_BLOCKING_READ_WITH_RAW);
        }
        else {
          //ordinary i/o error
          goto io_error;
        }
      }
      else {
        //we read'0'
io_error:

        if (SESNSTATUS_IS_SET(sesn_ptr->stat, SESNSTATUS_SECURE))
          ReportTlsReadError(sesn_ptr->session_crypto.ssl, raw_msg_size, sm_ptr->sm_errno);

        //since we read nothing anyway,  we deallocate
        if (sm_ptr->raw_msg_size == 0) {
          //this is the consolidated SocketMessage
#ifdef __UF_FULLDEBUG
          syslog(LOG_NOTICE, LOGSTR_IO_ERROR_READ_ZERO_EMPTY, __func__, pthread_self(), sesn_ptr,
                 UFSRV_SESSION_ID(sesn_ptr), sm_ptr->raw_msg_size, LOGCODE_IO_ZERO_READ_WITH_RAW_EMPTY);
#endif
          free(sm_ptr->_raw_msg);
          sm_ptr->_raw_msg = NULL;
          //sm_ptr->raw_msg_size=0;

          //no consolidation: IMPOSSIBLE for other threads to have read anything, because we hold the queue lock, plus the socket erred
          MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));
          ErrorFromSocket(instance_sesn_ptr, 0);

          return -1;
        }

        //OK this is interesting, as we have some data to process "offline"
        //logically, same outcome as above, but I'll keep them on separate paths for now to explore "offline"
        syslog(LOG_NOTICE, LOGSTR_IO_ERROR_READ_ZERO, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
               sm_ptr->raw_msg_size, LOGCODE_IO_ERROR_READ_WITH_RAW);

        MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));
        ErrorFromSocket(instance_sesn_ptr, 0);

        return -1;
      }
    }

    //>>> WE READ POSITIVE AMOUNT OF BYTES
    //>>> LOCK IS STILL IN PLACE ON INCOMING QUEUE

    if ((sm_ptr->raw_msg_size >= UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr))) {
      // using '>=' since consolidation will inflate size so we cant just do '=='
      sesn_ptr->io_retry_events |= EPOLLIN;

      //ramp up capacity
      if (UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr) < UfsrvConfigBufferSizesIncomingBufferMaxsize()) {
        //_CONF_READ_MAXBLOCKSZ(masterptr)) { //old reference
        //1mb or whatever defined in the config file
        UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr) *= 2; //double capacity
        if (UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr) > UfsrvConfigBufferSizesIncomingBufferMaxsize())
          UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr) = UfsrvConfigBufferSizesIncomingBufferMaxsize();
      }
    }

    MessageQueueUnLock(sesn_ptr, &(sesn_ptr->message_queue_in));

    if (flag & SOCKMSG_DONTDECODE) return sm_ptr->raw_msg_size;

    //CONTINUE BELOW TO DECODING
  }
  else if (flag & SOCKMSG_READBUFFER) {
    //we are reading from an already given buffer
    //>>>>> IMPORTANT: SocketMessage queue MUST BE locked in calling function

    if (flag & SOCKMSG_READQUEUE) {
      UFSRVResult res;

      //TODO: error checking
      ConsolidateSocketMessageQueue(sesn_ptr, SOCKMSG_CONSOLIDATE_INSESSION, &res); //dequeues into transient buffer
      sm_ptr = (SocketMessage*)res.result_user_data;

      //continue to start_msg_decoding;
      goto start_msg_decoding;
    }

    syslog(LOG_DEBUG, LOGSTR_IO_READING_MSGQUE_BUF, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
           sm_ptr->missing_msg_size, sesn_ptr->message_queue_in.queue.nEntries, LOGCODE_IO_READING_MSGQUE_BUF);

    if ((sm_ptr->missing_msg_size > 0) && (sesn_ptr->message_queue_in.queue.nEntries == 0)) {
      //we are not ready to process frame
      syslog(LOG_NOTICE, LOGSTR_IO_READ_MSGQUE_BUF_QUEEMPTY, __func__, pthread_self(), sesn_ptr,
             UFSRV_SESSION_ID(sesn_ptr), sm_ptr->missing_msg_size, sesn_ptr->message_queue_in.queue.nEntries,
             LOGCODE_IO_READ_MSGQUE_BUF_QUEEMPTY);

      //NO RECOVERY..

      return -5;
    }
    else if ((sm_ptr->missing_msg_size > 0) && (sesn_ptr->message_queue_in.queue.nEntries > 0)) {
      syslog(LOG_NOTICE, LOGSTR_IO_READ_MSGQUE_NOTCONSOLIDATED, __func__, pthread_self(), sesn_ptr,
             UFSRV_SESSION_ID(sesn_ptr), sm_ptr->missing_msg_size, sesn_ptr->message_queue_in.queue.nEntries,
             LOGCODE_IO_READ_MSGQUE_NOTCONSOLIDATED);

      //TODO: do we force consolidation?
      //continue with decoding for now
      goto start_msg_decoding;
    }
    //		else
    //		if (flag&SOCKMSG_READQUEUE)
    //		{
    //			UFSRVResult res;
    //			ConsolidateSocketMessageQueue (sesn_ptr, 0, &res);
    //			sm_ptr=(SocketMessage *)res.result_user_data;
    //			//continue to start_msg_decoding;
    //		}

    //now branches to 'goto start_msg_decoding';
  }
  else {
    //>>> SocketMessage is not locked in this context
    syslog(LOG_DEBUG, "%s (pid:'%lu' cid:'%lu'): UNKNOWN MSG SOURCE...", __func__, pthread_self(),
           UFSRV_SESSION_ID(sesn_ptr));

    return -9; //some logical error
  }

  //past this point we have two possibilities:
  //1)sm_ptr represents a buffer read from the network
  //2)sm_ptr buffer supplied by the caller.

  //past this point queue should be unlocked if we are reading from socket, allowing other threads to read off socket
start_msg_decoding:

  return (_SocketMessageDecode(instance_sesn_ptr, sm_ptr, flag));
}

ssize_t
ReadFromSocketNonQueued(InstanceHolderForSession *holder, SocketMessage *message, unsigned flags)
{
  if (!holder || !message || !(flags & SOCKMSG_READSOCKET) || (flags & ~(SOCKMSG_READSOCKET | SOCKMSG_DONTDECODE)) ||
    message->_raw_msg || message->_processed_msg) return -2;

  UfsrvSession *session = AS_UFSRV_SESSION(GetInstance(holder));
  if (!session || !session->ssptr || session->ssptr->sock < 0) return -2;

  size_t block_size = UFSRV_SESSION_SOCKETBLOCKSZ(session);
  if (!block_size) return -2;
  size_t         read_size = block_size > INT_MAX ? INT_MAX : block_size;
  unsigned char *buffer    = malloc(read_size);
  if (!buffer) return -2;

  ssize_t bytes;
  int     read_error;
  bool    retry;
  bool    closed;
  bool    secure = SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SECURE);
  if (secure) {
    SSL *ssl = session->session_crypto.ssl;
    if (!ssl) {
      free(buffer);
      return -2;
    }
    ERR_clear_error();
    bytes      = SSL_read(ssl, buffer, (int)read_size);
    read_error = bytes > 0 ? 0 : SSL_get_error(ssl, (int)bytes);
    retry      = read_error == SSL_ERROR_WANT_READ || read_error == SSL_ERROR_WANT_WRITE;
    closed     = read_error == SSL_ERROR_ZERO_RETURN;
  }
  else {
    do {
      bytes = read(session->ssptr->sock, buffer, read_size);
    }
    while (bytes < 0 && errno == EINTR);
    read_error = bytes < 0 ? errno : 0;
    retry      = read_error == EAGAIN || read_error == EWOULDBLOCK || read_error == EINPROGRESS;
    closed     = bytes == 0;
  }

  message->sm_errno = 0;
  if (bytes <= 0) {
    free(buffer);
    if (retry) return 0;
    message->sm_errno = closed ? SOCKMSG_SMERRNO_PEERCLOSED : read_error;
    if (!closed) {
      char        detail[MBUF] = {0};
      const char *error_text;
      if (secure) {
        ERR_error_string_n(ERR_peek_last_error(), detail, sizeof detail);
        error_text = detail;
      }
      else {
        error_text = strerror_r(read_error, detail, sizeof detail);
      }
      syslog(LOG_ERR, "%s {pid:'%lu', o:'%p', cid:'%lu', fd:'%d', transport:'%s', error:'%d'}: Socket read failed: %s",
             __func__, pthread_self(), session, UFSRV_SESSION_ID(session), session->ssptr->sock, secure ? "TLS" : "TCP",
             read_error, error_text);
      SESNSTATUS_SET(session->stat, SESNSTATUS_IOERROR);
    }
    return -1;
  }

  UFSRV_SESSION_INCREMENT_RC(session, bytes);
  if (message->missing_msg_size > 0) {
    bool   complete = (size_t)bytes >= message->missing_msg_size;
    size_t padding  = complete ? 10 : 0; // Matches the decoder's frame separator space.
    if (message->holding_buffer_msg_size > SSIZE_MAX - (size_t)bytes - padding) {
      free(buffer);
      return -2;
    }
    size_t         size   = message->holding_buffer_msg_size + (size_t)bytes;
    unsigned char *joined = realloc(message->holding_buffer, size + padding);
    if (!joined) {
      free(buffer);
      return -2;
    }
    memcpy(joined + message->holding_buffer_msg_size, buffer, (size_t)bytes);
    free(buffer);
    if (!complete) {
      message->holding_buffer          = joined;
      message->holding_buffer_msg_size = size;
      message->missing_msg_size        -= (size_t)bytes;
      return 0;
    }
    message->holding_buffer          = NULL;
    message->holding_buffer_msg_size = 0;
    message->missing_msg_size        = 0;
    buffer                           = joined;
    bytes                            = (ssize_t)size;
  }

  message->_raw_msg        = buffer;
  message->raw_msg_size    = bytes;
  message->raw_msg_cur_pos = bytes;
  if (flags & SOCKMSG_DONTDECODE) return bytes;
  return _SocketMessageDecode(holder, message, flags);
}

static int
_SocketMessageDecode(InstanceHolderForSession *instance_sesn_ptr, SocketMessage *sm_ptr, unsigned flag)
{
  UfsrvSession *sesn_ptr = AS_UFSRV_SESSION(GetInstance(instance_sesn_ptr));

  const Protocol *protocol = UfsrvSessionProtocol(sesn_ptr);
  if (protocol && protocol->protocol_callbacks.msg_decode_callback) {
    if (flag & SOCKMSG_DONTDECODE) {
      //well, do nothing, shuffle pointers around. potentially useless case
      goto default_decode_block;
    }

    UFSRVResult *res_ptr = protocol->protocol_callbacks.msg_decode_callback(instance_sesn_ptr, sm_ptr, flag);
    if (res_ptr->result_type == RESULT_TYPE_SUCCESS) {
      return sm_ptr->processed_msg_size;
    }
    else {
      switch (res_ptr->result_code) {
        case RESCODE_IO_DECODED:
          return -4;

        case RESCODE_IO_SUSPENDED:
          return -1;

        case RESCODE_IO_PROTOCOL_SHUTDOWN:
          return -3;
      }
    }
  }
  else {
default_decode_block:
#ifdef __UF_FULLDEBUG
    syslog(LOG_DEBUG, "%s (pid:'%lu' cid:'%lu'): INVOKING _DEFAULT_ DECODE BLOCK: on raw_msg_size: '%ld' ", __func__,
           pthread_self(), UFSRV_SESSION_ID(sesn_ptr), sm_ptr->raw_msg_size);
#endif

    //just shift content across different buckets
    sm_ptr->_processed_msg = sm_ptr->_raw_msg;
    sm_ptr->processed_msg_size = sm_ptr->raw_msg_size; //=0;

    sm_ptr->_raw_msg     = NULL;
    sm_ptr->raw_msg_size = 0; // prevents raw_msg from inadvertently free'd

    sm_ptr->sm_errno = 0;

    return sm_ptr->processed_msg_size;
  }

  return 0;
}

/**
 * 	@brief: Given a Session's incoming SocketMessage queue (sesn_ptr->message_queue_in), join queue entries into a single SocketMessage.
 * 	Essentially results in one (huge) consolidated buffer.
 *
 * 	Depending on call_flags, the consolidated SocketMessage can be the Session's actual transient incoming SocketMessage (sesn_ptr->ssptr->socket_msg)
 * 	or, re-attached into the incoming SocketMessage queue, or returned as a free standing SocketMessage pointer. See flags below.
 *
 * 	@call_flag SOCKMSG_CONSOLIDATE_INSESSION: Consolidate into transient incoming socket message
 * 	@call_flag SOCKMSG_CONSOLIDATE_INSESSIONQUEUE: consolidate into incoming socket message queue
 * 	@call_flag SOCKMSG_LOCK_SOCKMSGQUEUE: lock Session's incoming SocketMessage queue
 * 	@returns:
 *  @locked sesn_ptr: must be locked din the calling environment
 *  @locked message_queue_in: should be locked din the calling environment unless SOCKMSG_LOCK_SOCKMSGQUEUE is passed
 *  @locks message_queue_in: if SOCKMSG_LOCK_SOCKMSGQUEUE is passed
 *  @unlocks message_queue_in:  if SOCKMSG_LOCK_SOCKMSGQUEUE is passed
 *  @dynamic_memory: creates new SocketMessage structure and ListEntry in Queue if SOCKMSG_CONSOLIDATE_INSESSIONQUEUE is  set, which the user must free
 *  @dynamic memory: if neither SOCKMSG_CONSOLIDATE_INSESSIONQUEUE or SOCKMSG_CONSOLIDATE_INSESSION is set returns a free standing structure
 *  				which the user must free
 */
UFSRVResult *
ConsolidateSocketMessageQueue(UfsrvSession *sesn_ptr, unsigned call_flags, UFSRVResult *res_ptr)
{
  unsigned char *consolidated_buffer;
  SocketMessage *sm_ptr_consolidated;
  int            sm_errno = 0;

  //quick check before we launch into expensive locking
  if (__sync_add_and_fetch(&sesn_ptr->message_queue_in.queue.nEntries, 0) == 0) {
#ifdef __UF_FULLDEBUG
    syslog(LOG_NOTICE, "%s (pid='%lu' cid='%lu'): SOCKETMESSAGE QUEUE EMPTY: RETURNING...", __func__, pthread_self(),
           UFSRV_SESSION_ID(sesn_ptr));
#endif
    //technically not error, but we leave that up to the processing environment to work out context
    _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, RESCODE_LOGIC_EMPTY_RESOURCE)
  }

  if (call_flags & SOCKMSG_LOCK_SOCKMSGQUEUE) {
    if (!(MessageQueueLock(sesn_ptr, UFSRV_SESSION_INSOCKMSG_PTR(sesn_ptr), 0) == 0)) {
      _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, RESCODE_LOGIC_CANTLOCK)
    }
  }

  if (call_flags & SOCKMSG_CONSOLIDATE_INSESSION) {
    sm_ptr_consolidated = &(sesn_ptr->ssptr->socket_msg);
  }
  else {
    sm_ptr_consolidated = calloc(1, sizeof(SocketMessage));
  }

  syslog(LOG_DEBUG, LOGSTR_IO_BUF_CONSOLIDATED_PRE, __func__, pthread_self(), UFSRV_SESSION_ID(sesn_ptr),
         sesn_ptr->message_queue_in.queue.nEntries, sm_ptr_consolidated->raw_msg_size,
         UFSRV_SESSION_CUMMULATIVE_RC(sesn_ptr), LOGCODE_IO_BUF_CONSOLIDATED_PRE);

  //prime the buffer by factor of one if it's empty so realloc can work.
  if (sm_ptr_consolidated->raw_msg_size == 0) sm_ptr_consolidated->_raw_msg = calloc(1, sizeof(unsigned char));

  QueueEntry *   qe_ptr = NULL;
  SocketMessage *sm_ptr_aux;
  int            blocksz_factor = 1;

  while (sesn_ptr->message_queue_in.queue.nEntries != 0) {
    qe_ptr = deQueue(&(sesn_ptr->message_queue_in.queue));

    if ((sm_ptr_aux = (SocketMessage*)qe_ptr->whatever)) {
      //at some prior i/o cycle we hit an error
      if (!(sm_ptr_aux->sm_errno == 0)) {
        if (sm_ptr_aux->sm_errno == SOCKMSG_SMERRNO_PEERCLOSED) sm_errno = SOCKMSG_SMERRNO_PEERCLOSED;
        DestructSocketMessage(sm_ptr_aux);
        free(sm_ptr_aux);
        free(qe_ptr);

        continue;
      }

#ifdef __UF_FULLDEBUG
      syslog(LOG_INFO, "%s (pid:%lu cid='%lu'): DEQUEUEING (msg count='%lu')... ", __func__, pthread_self(),
             UFSRV_SESSION_ID(sesn_ptr),
             sesn_ptr->message_queue_in.queue.nEntries + 1); //+1 because dequeue above decrements
#endif
      unsigned char *tmp_hb = NULL;
      if (!(tmp_hb = realloc(sm_ptr_consolidated->_raw_msg,
                             sm_ptr_consolidated->raw_msg_size + sm_ptr_aux->raw_msg_size))) {
        //memory allocation error: clean up brute force.. this is mostly fatal condition
        syslog(
          LOG_INFO,
          "%s (pid:%lu cid='%lu'): MEMORY ERROR: COULD NOT ALLOCATE MEMORY FOR BUFFER RESIZING... (msg count='%lu')... ",
          __func__, pthread_self(), UFSRV_SESSION_ID(sesn_ptr),
          sesn_ptr->message_queue_in.queue.nEntries + 1); //+1 because dequeue above decrements
        //get rid of current element
        DestructSocketMessage(sm_ptr_aux);

        free(sm_ptr_aux);
        free(qe_ptr);

        // we only allocate this if we are not reusing transient incoming buffer
        if (!(call_flags & SOCKMSG_CONSOLIDATE_INSESSION)) free(sm_ptr_consolidated);
        //TODO: destroy queue

        if (call_flags & SOCKMSG_LOCK_SOCKMSGQUEUE) {
          MessageQueueUnLock(sesn_ptr, UFSRV_SESSION_INSOCKMSG_PTR(sesn_ptr));
        }

        _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, RESCODE_PROG_MEMORY_EXHAUSTED)
      }

      //repoint to new expanded buffer
      sm_ptr_consolidated->_raw_msg = tmp_hb;

      //stash into the new allocated space, beginning at current offset
      memcpy(sm_ptr_consolidated->_raw_msg + sm_ptr_consolidated->raw_msg_size, sm_ptr_aux->_raw_msg,
             sm_ptr_aux->raw_msg_size);

      //update offset
      sm_ptr_consolidated->raw_msg_size += sm_ptr_aux->raw_msg_size;

      //update total bytes read, as threads reading into queue cannot do that
      UFSRV_SESSION_INCREMENT_TR(sesn_ptr, sm_ptr_aux->raw_msg_size);

      //update dynamic block sizing, again on behalf previously restricted threads
      if (sm_ptr_aux->raw_msg_size <= UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr)) {
        //only do it if we have not reached global max limit on blocksz
        if (UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr) < UfsrvConfigBufferSizesIncomingBufferMaxsize()) blocksz_factor++;
        else {
          blocksz_factor--;
          if (blocksz_factor < 0) blocksz_factor = 1;
        }
      }

#ifdef __UF_FULLDEBUG
      syslog(LOG_DEBUG, LOGSTR_IO_BUF_CONSOLIDATED, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
             sm_ptr_consolidated->raw_msg_size, LOGCODE_IO_BUF_CONSOLIDATED);
#endif

      DestructSocketMessage(sm_ptr_aux);
      free(sm_ptr_aux);
    }

    free(qe_ptr);
  }

  //restore error value recovered from consolidation if any. We only restore the last recorded value
  sm_ptr_consolidated->sm_errno = sm_errno;

  if ((blocksz_factor > 0) && (sm_ptr_consolidated->sm_errno == 0)) {
    unsigned new_blocksz = (unsigned)pow(UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr), blocksz_factor);

    if (new_blocksz > UfsrvConfigBufferSizesIncomingBufferMaxsize())
      new_blocksz = UfsrvConfigBufferSizesIncomingBufferMaxsize();

    UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr) = new_blocksz;

    //good chance buffer is not drained, so lets cycle again
    sesn_ptr->io_retry_events |= EPOLLIN;
  }

  syslog(LOG_DEBUG, LOGSTR_IO_BUF_CONSOLIDATED_FIN, __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_ID(sesn_ptr),
         sm_ptr_consolidated->raw_msg_size, UFSRV_SESSION_CUMMULATIVE_RC(sesn_ptr),
         UFSRV_SESSION_SOCKETBLOCKSZ(sesn_ptr), sm_errno, LOGCODE_IO_BUF_CONSOLIDATED_FIN);

  //put this back into the now empty queue
  if (call_flags & SOCKMSG_CONSOLIDATE_INSESSIONQUEUE) {
    AddToQueue(UFSRV_SESSION_INSOCKMSG_QUEUE_PTR(sesn_ptr), sm_ptr_consolidated);
  }

  if (call_flags & SOCKMSG_LOCK_SOCKMSGQUEUE) {
    MessageQueueUnLock(sesn_ptr, UFSRV_SESSION_INSOCKMSG_PTR(sesn_ptr));
  }

  _RETURN_RESULT_RES(res_ptr, sm_ptr_consolidated, RESULT_TYPE_SUCCESS, RESCODE_LOGIC_WITH_RESOURCE)
}

/**
 *	@brief: invokes standard error propagation process. Doesn't touch the deallocation of buffers
 *
 *	param call_flags: SOCKMSG_LOCK_SOCKMSGQUEUE. This is passed off by other callers
 *	param call_flags: SOCKMSG_CONSOLIDATE_INSESSION: consolidate queue into transitional buffer
 * 	@locked sesn_ptr: must be locked in calling environment
 */
void
ErrorFromSocket(InstanceHolderForSession *instance_sesn_ptr, unsigned call_flags)
{
  UfsrvSession *sesn_ptr = AS_UFSRV_SESSION(GetInstance(instance_sesn_ptr));

  SESNSTATUS_SET(sesn_ptr->stat, SESNSTATUS_IOERROR);
  RemoveSessionToMonitoredWorkEvents(instance_sesn_ptr);

  //consolidate queue into transitional incoming buffer
  if (call_flags & SOCKMSG_CONSOLIDATE_INSESSION) {
    UFSRVResult res;
    ConsolidateSocketMessageQueue(sesn_ptr, call_flags, &res);
  }

  //invoke decode lifecycle, which shifts data to processed. Nocommand processing involved here
  _SocketMessageDecode(instance_sesn_ptr, UFSRV_SESSION_INSOCKMSG_TRANS_PTR(sesn_ptr), call_flags);

  const Protocol *protocol = UfsrvSessionProtocol(sesn_ptr);
  if (protocol && protocol->protocol_callbacks.error_callback) {
    UFSRVResult *res_ptr = protocol->protocol_callbacks.error_callback(instance_sesn_ptr, call_flags);
  }

  UfsrvSessionSuspendSoft(instance_sesn_ptr);
}

/* The caller holds the session lock. A blocked write leaves its offset in the
 * message, so a queued head can resume without being re-encoded or requeued. */
static ssize_t
WriteSocketBuffer(UfsrvSession *session, SocketMessage *message)
{
  ssize_t start = message->written_msg_size;
  while (message->written_msg_size < message->processed_msg_size) {
    unsigned char *data      = message->_processed_msg + message->written_msg_size;
    size_t         remaining = message->processed_msg_size - message->written_msg_size;
    ssize_t        bytes;

    if (SESNSTATUS_IS_SET(session->stat, SESNSTATUS_SECURE)) {
      SSL *ssl = session->session_crypto.ssl;
      ERR_clear_error();
      bytes = SSL_write(ssl, data, remaining > INT_MAX ? INT_MAX : (int)remaining);
      if (bytes <= 0) {
        int error = SSL_get_error(ssl, (int)bytes);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return 0;
        break;
      }
    }
    else {
      bytes = send(session->ssptr->sock, data, remaining, MSG_NOSIGNAL);
      if (bytes < 0 && errno == EINTR) continue;
      if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
      if (bytes <= 0) break;
    }

    message->written_msg_size += bytes;
    UFSRV_SESSION_INCREMENT_TR(session, bytes);
  }

  if (message->written_msg_size == message->processed_msg_size) return message->written_msg_size - start;

  SESNSTATUS_SET(session->stat, SESNSTATUS_IOERROR);
  return -1;
}

ssize_t
WriteToSocketNonQueued(InstanceHolderForSession *holder, const void *data, size_t length)
{
  if (!holder || (length && !data) || length > SSIZE_MAX) return -1;
  UfsrvSession *session = AS_UFSRV_SESSION(GetInstance(holder));
  if (!session || !session->ssptr || session->ssptr->sock < 0 || !
    SESNSTATUS_IS_SET(session->stat, SESNSTATUS_WORKER_OWNS_SOCKET) || SESNSTATUS_IS_SET(
      session->stat, SESNSTATUS_IOERROR)) return -1;

  SocketMessage message = {._processed_msg = (unsigned char*)data, .processed_msg_size = (ssize_t)length,};
  ssize_t       written = WriteSocketBuffer(session, &message);
  if (written == (ssize_t)length) return written;

  SESNSTATUS_SET(session->stat, SESNSTATUS_IOERROR);
  syslog(LOG_ERR, "%s {pid:'%lu', o:'%p', cid:'%lu', fd:'%d'}: Socket write failed after %zd of %zu bytes", __func__,
         pthread_self(), session, UFSRV_SESSION_ID(session), session->ssptr->sock, message.written_msg_size, length);
  return -1;
}

int
DispatchSocketMessageQueue(InstanceHolderForSession *holder, size_t entries)
{
  UfsrvSession *session = AS_UFSRV_SESSION(GetInstance(holder));
  Queue *       queue   = UFSRV_SESSION_OUTSOCKMSG_QUEUE_PTR(session);
  if (QueueEmpty(queue)) return 0;
  if (entries == 0 || entries > queue->nEntries) entries = queue->nEntries;

  for (size_t i = 0; i < entries; i++) {
    SocketMessage *message = queue->front->whatever;
    if (WriteSocketBuffer(session, message) < 0) {
      UfsrvSessionSuspendSoft(holder);
      return -1;
    }
    if (message->written_msg_size < message->processed_msg_size) return 0;

    QueueEntry *done = deQueue(queue);
    DestructSocketMessage(message);
    free(message);
    free(done);
  }

  return 1;
}

static int
EncodeOutgoingSocketMessage(InstanceHolderForSession *holder, UfsrvSession *session, SocketMessage *message,
                            unsigned                  flags)
{
  const Protocol *protocol      = UfsrvSessionProtocol(session);
  bool            framed        = (message->flag & (SOCKMSG_ENCODED | SOCKMSG_DONTWSFRAME)) != 0;
  bool            skip_encoding = framed || (flags & (SOCKMSG_DONTWSFRAME | SOCKMSG_DONTDECODE));
  if (skip_encoding || !protocol || !protocol->protocol_callbacks.msg_encode_callback) {
    message->_processed_msg     = message->_raw_msg;
    message->processed_msg_size = message->raw_msg_size;
  }
  else {
    if (message->raw_msg_size > SSIZE_MAX / 2) return -2;
    message->_processed_msg = calloc(2, message->raw_msg_size);
    if (!message->_processed_msg) return -2;
    message->processed_msg_size = 2 * message->raw_msg_size;

    UFSRVResult *result = protocol->protocol_callbacks.msg_encode_callback(holder, message, flags);
    if (result->result_type != RESULT_TYPE_SUCCESS) {
      switch (result->result_code) {
        case RESCODE_IO_DECODED: return -4;
        case RESCODE_IO_SUSPENDED: return -1;
        case RESCODE_IO_PROTOCOL_SHUTDOWN: return -3;
        default: return -2;
      }
    }
  }

  if (message->processed_msg_size < 0 || message->processed_msg_size < message->written_msg_size || (message->
    processed_msg_size && !message->_processed_msg)) return -2;

  if (message->_raw_msg != message->_processed_msg) free(message->_raw_msg);
  message->_raw_msg     = NULL;
  message->raw_msg_size = 0;
  if (message->processed_msg_size == 0) {
    free(message->_processed_msg);
    message->_processed_msg = NULL;
  }
  return 0;
}

static ssize_t
SendSocketMessage(InstanceHolderForSession *holder, SocketMessage *message, unsigned flags)
{
  UfsrvSession *session = AS_UFSRV_SESSION(GetInstance(holder));
  if (DispatchSocketMessageQueue(holder, 0) < 0) {
    DestructSocketMessage(message);
    return -1;
  }

  /* Protocol encoders may access the transitional message via the session. */
  SocketMessage *outgoing = UFSRV_SESSION_OUTSOCKMSG_TRANS_PTR(session);
  *outgoing               = *message;
  memset(message, 0, sizeof(*message));

  ssize_t result = EncodeOutgoingSocketMessage(holder, session, outgoing, flags);
  if (result < 0) {
    /* An encoder can report a negative length while still owning its buffer. */
    if (outgoing->_processed_msg == outgoing->_raw_msg) {
      outgoing->processed_msg_size = 0;
    }
    else if (outgoing->processed_msg_size <= 0) {
      free(outgoing->_processed_msg);
      outgoing->_processed_msg = NULL;
    }
    DestructSocketMessage(outgoing);
    return result;
  }

  Queue *queue = UFSRV_SESSION_OUTSOCKMSG_QUEUE_PTR(session);
  if (QueueEmpty(queue)) {
    result = WriteSocketBuffer(session, outgoing);
    if (result < 0) {
      DestructSocketMessage(outgoing);
      UfsrvSessionSuspendSoft(holder);
      return result;
    }
    if (outgoing->written_msg_size == outgoing->processed_msg_size) {
      DestructSocketMessage(outgoing);
      return result;
    }
  }

  SocketMessage *pending = malloc(sizeof(*pending));
  if (pending) *pending = *outgoing;
  if (!pending || AddToQueue(queue, pending) < 0) {
    bool started = outgoing->written_msg_size > 0;
    free(pending);
    DestructSocketMessage(outgoing);
    if (!started) return -2;

    SESNSTATUS_SET(session->stat, SESNSTATUS_IOERROR);
    UfsrvSessionSuspendSoft(holder);
    return -1;
  }
  memset(outgoing, 0, sizeof(*outgoing));
  return 0;
}

/**
 * @brief Send an owned copy of a text buffer without protocol framing.
 */
int
SendTextMessage(InstanceHolderForSession *holder, const char *text, size_t length)
{
  if (length > INT_MAX) return -2;
  void *copy = strbufdup(text, length);
  if (!copy) return -2;

  TransmissionMessage message = {.type = TRANSMSG_TEXT, .len = length, .msg = copy, .msg_packed = copy,};
  return SendToSocket(holder, &message, SOCKMSG_DONTWSFRAME);
}

ssize_t
SendToSocket(InstanceHolderForSession *holder, TransmissionMessage *message, unsigned flags)
{
  SocketMessage outgoing = {0};
  switch (message->type) {
    case TRANSMSG_PROTOBUF:
    case TRANSMSG_TEXT:
      if (message->len > SSIZE_MAX || (message->len && !message->msg_packed)) return -2;
      outgoing._raw_msg     = message->msg_packed;
      outgoing.raw_msg_size = message->len;
      break;

    case TRANSMSG_SOCKMSG: {
      SocketMessage *source = message->msg;
      if (!source || (!(source->flag & SOCKMSG_DONTWSFRAME) && (source->flag & (SOCKMSG_ENCODED | SOCKMSG_WSFRAMED)) !=
        (SOCKMSG_ENCODED | SOCKMSG_WSFRAMED))) return -2;
      if (source->processed_msg_size < 0 || source->written_msg_size < 0 || source->written_msg_size > source->
        processed_msg_size || (source->processed_msg_size && !source->_processed_msg)) return -2;

      outgoing._raw_msg         = source->_processed_msg;
      outgoing.raw_msg_size     = source->processed_msg_size;
      outgoing.written_msg_size = source->written_msg_size;
      outgoing.flag             = source->flag;
      if (source->_raw_msg == source->_processed_msg) {
        source->_raw_msg     = NULL;
        source->raw_msg_size = 0;
      }
      source->_processed_msg     = NULL;
      source->processed_msg_size = 0;
      break;
    }

    default:
      return -2;
  }

  if (outgoing.raw_msg_size == 0) {
    free(outgoing._raw_msg);
    return 0;
  }
  return SendSocketMessage(holder, &outgoing, flags);
}
