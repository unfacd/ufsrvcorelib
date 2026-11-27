/*

 Copyright (c) 2015-2026 unfacd works

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
# include <config_ufsrvcorelib.h>
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <ufsrvcorelib/ufsrv_core/http/basic_auth/basic_auth.h>

#include <ufsrvcorelib/ufsrv_core/ufsrvresult_type.h>
#include <ufsrvcorelib/ufsrv_core/delegator_sessions/thread_context_type.h>
#include <ufsrvcorelib/ufsrv_core/delegator_sessions/delegator_session_worker_thread.h>
#include <ufsrvcorelib/ufsrv_core/cache_backend/redis.h>
#include <uflib/adt/adt_locking_lru.h>
#include <ufsrvcorelib/ufsrv_core/http/basic_auth/basic_auth_descriptor_provider.h>
#include <ufsrvcorelib/ufsrv_core/system/log_message_literals.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_shim.h>

extern __thread ThreadContext ufsrv_thread_context;

//IMPORTANT this is not yet general purpose: one instance per server, until static references below are replaced
static HashTable  BasicAuthHashTable;
static LockingLru BasicAuthLruCache;

static void _InitialiseBasicAuthHashTable(HashTable *hashtable_ptr, size_t hashtable_sz, unsigned long call_flags);

static BasicAuthDescriptor *_CacheLocalLruGetBasicAuthItem(const char *basicauth_b64encoded);
static UFSRVResult *        _CacheBackendGetBasicAuthItem(const char *basicauth_b64encoded);
static UFSRVResult *        _CachebackendDelBasicAuthItem(const char *basicauth_b64encoded);
static size_t               _CacheLocalLruGetConfiguration(void);

#define RESPONSE_UNAUTHORIZED "<h1>Unauthorized access</h1>"
#define RESPONSE_RATELIMIT_EXCEEDED "<h1>Rate limit exceeded</h1>"

static size_t
_CacheLocalLruGetConfiguration(void)
{
  return 1024;
  /*lua_getglobal(UfsrvConfigLuaHandle(), "ufsrvapi_cache_authorisation");
  if (!lua_istable(UfsrvConfigLuaHandle(), -1)) {
    syslog(LOG_NOTICE, "%s: `ufsrvapi_cache_authorisation' is not a valid config table", __func__);
    return 1024;
  } else {
    //TODO: validate values
    return LUA_GetFieldToInteger("size");
    //sessions_delegator_ptr->user_timeouts.suspended=LUA_GetFieldToString("eviction_policy");
  }

  return 0;*/
}

//in ufsrvxxx use _CONF_SESNMEMSPECS_ALLOC_GROUPS(masterptr)
void
InitialiseBasicAuthLruCache(size_t allocation_sz)
{
  size_t lrucache_sz = _CacheLocalLruGetConfiguration();
  if (lrucache_sz == 0) {
    lrucache_sz = _CONFIDEFAULT_HASHTABLE_BASICAUTH_SZ;
    syslog(LOG_NOTICE, "%s: NOTICE: Lru Cache Size for BasicAuth was incorrectly set: Using default value of: '%ld'",
           __func__, lrucache_sz);
  }

  InitBasicAuthDescriptorRecyclerTypePool(allocation_sz); //_CONF_SESNMEMSPECS_ALLOC_GROUPS(masterptr)
  InitLockingLruItemRecyclerTypePool(allocation_sz);
  InitLockingLru(&BasicAuthLruCache, "BasicAuth", lrucache_sz, UfsrvGetSessionWorkersSize(), &BasicAuthHashTable,
                 _InitialiseBasicAuthHashTable, NULL, PrintBasicAuthDescriptor);
}

inline static void
_InitialiseBasicAuthHashTable(HashTable *hashtable_ptr, size_t hashtable_sz, unsigned long call_flags)
{
  if (HashTableLockingInstantiate(hashtable_ptr, (offsetof(BasicAuthDescriptor, b64encoded)), KEY_SIZE_ZERO,
                                  HASH_ITEM_NOT_PTR_TYPE, "BasicAuth", NULL)) {
    HASHTABLE_CLEARFLAG(hashtable_ptr, flag_resizable);
    hashtable_ptr->max_size = hashtable_sz;

    syslog(LOG_INFO, "%s: SUCCESS: BasicAuth HashTable Instantiated: key_offset: '%ld'. key_size: '%ld'", __func__,
           hashtable_ptr->fKeyOffset, hashtable_ptr->fKeySize);
  }
  else {
    syslog(LOG_ERR, "%s: ERROR (errno: '%d'): COULD NOT INITIALISE BasicAuth HashTable: TERMINATING...", __func__,
           errno);

    exit(-1);
  }
}

/**
 * 	@brief	Given basicauth user credentials: look up the user  in lru cache first, and where doesn't exist try
 * 	in backend cache, which causes the item to be fed into the local lru cache. Does not query db backend.
 *
 * 	@dynamic_memory IMPORTS and DEALLOCATES 'char *'
 * 	@returns 0 on success and the userid returned in param
 */
int
CacheValidateBasicAuth(const char *basicauth_b64encoded, const char *basicauth_decoded, unsigned long *return_userid)
{
  char *        basicauth_decoded_backend = NULL;
  char *        userid                    = NULL;
  unsigned long ret;
  unsigned long userid_converted;

  BasicAuthDescriptor *basicauth_ptr = _CacheLocalLruGetBasicAuthItem(basicauth_b64encoded);
  if (IS_PRESENT(basicauth_ptr)) {
    *return_userid = basicauth_ptr->userid;
#ifdef __UF_TESTING
    syslog(LOG_DEBUG,
           "%s {pid:'%lu', userid:'%lu', basicauth_decoded:'%s', basicauth_decoded_backend:'%s'}: LRU CACHE HIT",
           __func__, pthread_self(), basicauth_ptr->userid, basicauth_ptr->decoded, basicauth_ptr->b64encoded);
#endif
    return 0;
  }

  _CacheBackendGetBasicAuthItem(basicauth_b64encoded);

  if (THREAD_CONTEXT_UFSRV_RESULT_TYPE_SUCCESS) {
    basicauth_decoded_backend = (char*)THREAD_CONTEXT_UFSRV_RESULT_USERDATA;
    if (IS_PRESENT(basicauth_decoded_backend)) {
      userid  = strrchr(basicauth_decoded_backend, ':');
      *userid = '\0';
      userid++;

      if (IS_EMPTY(userid)) goto return_userid_error;

      if ((strcmp(basicauth_decoded, basicauth_decoded_backend) == 0)) {
#ifdef __UF_FULLDEBUG
        syslog(LOG_DEBUG,
               "%s {pid:'%lu', o:'%p', userid:'%s', basicauth_decoded:'%s', basicauth_decoded_backend:'%s'}: BasicAuth Successful",
               __func__, pthread_self(), sesn_ptr, userid, basicauth_decoded, basicauth_decoded_backend);
#endif
return_success:
        userid_converted = strtoul(userid, NULL, 10);
        CacheLocalLruSetBasicAuthItem(basicauth_b64encoded, basicauth_decoded, userid_converted, LRU_CLIENT_DATA_EMPTY);
        *return_userid = userid_converted;
        free(basicauth_decoded_backend);
        return 0;
      }
      else goto return_mismatch_error;
    }
  }

  goto return_backend_error;

return_userid_error:
  syslog(LOG_DEBUG,
         "%s {pid:'%lu', basicauth_decoded:'%s', basicauth_decoded_backend:'%s'}: ERROR: BACKEND CACHED: COULD NOT ASCERTAIN USERID",
         __func__, pthread_self(), basicauth_decoded, basicauth_decoded_backend);
  ret = -3;
  goto return_free;

return_mismatch_error:
  syslog(LOG_DEBUG,
         "%s {pid:'%lu', userid:'%s', basicauth_decoded:'%s', basicauth_decoded_backend:'%s'}: ERROR: BACKEND CACHED basicauth_decoded DOES NOT MATCH",
         __func__, pthread_self(), userid, basicauth_decoded, basicauth_decoded_backend);
  ret = -2;
  goto return_free;

return_backend_error:
  ret = -1;
  goto return_error;

return_free:
  free(basicauth_decoded_backend);

return_error:
  return ret;
}

/**
 * @brief An iterator callback
 * @param client_data_ptr_stored client data stored inside the cache
 * @param client_data_ptr_provided client data to compare with. Typically this is is a fixed value.
 * @return true f matched
 */
static bool
_LruCacheMatchUserId(LruClientData *client_data_ptr_stored, LruClientData *client_data_ptr_provided)
{
  BasicAuthDescriptor *descriptor_ptr_provided = AS_BASIC_AUTH_DESCRIPTOR(client_data_ptr_provided);
  BasicAuthDescriptor *descriptor_ptr_stored   = AS_BASIC_AUTH_DESCRIPTOR(client_data_ptr_stored);
  if (descriptor_ptr_provided->userid == descriptor_ptr_stored->userid) {
    return true;
  }

  return false;
}

/**
 * @brief Pull a basicauth entry out of the cache and invalidate it, both locally and backend. Since backend cache is keyed on encoded basicauth
 * string, we have to fetch it locally first.
 * @param userid sequence id for user
 * @param is_invalidate_backend if set, the cachebackend will be cleared for \p userid
 * @return 1 on success 0 on failure
 * @dynamic_memory DEALLOCATES 'BasicAuthDescriptor  *'
 */
int
CacheInvalidateBasicAuth(unsigned long userid, bool is_invalidate_backend)
{
  BasicAuthDescriptor *basic_auth_pr = (LruClientData*)FindLruItemWithMatcherAndDelink(
    &BasicAuthLruCache, _LruCacheMatchUserId, AS_LRU_CLIENT_DATA(&(BasicAuthDescriptor)
  {
    .userid = userid
  }));
  if (IS_PRESENT(basic_auth_pr)) {
    if (is_invalidate_backend) {
      _CachebackendDelBasicAuthItem(basic_auth_pr->b64encoded);
    }

    free((void*)((uintptr_t)basic_auth_pr - sizeof(uintptr_t)));

    return 1;
  }

  syslog(LOG_ERR, "%s (pid:'%lu', userid:'%lu'): ERROR COULD NOT INVALIDATE BASICAUTH CACHE FOR USER: NO MATCH FOUND",
         __func__, pthread_self(), userid);

  return 0;
}

/**
 * 	@brief Basic interface function for caching freshly authenticated basicauth value.
 * 	We leave an extra room for a shadow reference to the Lrus list item corresponding with this BasicAuthDescriptor so we can derive them from
 * 	one another.
 * 	@param basicauth_b64encoded[in] the basicauth string encoded for transport by the authenticating remote end
 * 	@param basicauth_decoded[in] \p basicauth_b64encoded unpacked into original format, usually username:password of some sort
 * 	@param userid internal user id for authenticating remote end
 * 	@param basicauth_ptr_in[inout] The structure that encapsulates all information relating to the basicauth request. Can be pre-allocated by user.
 * 	@note if \p basicauth_ptr_in was to be provided by caller, it must be allocated as  \a sizeof(BasicAuthDescriptor) + sizeof(uintptr_t) <i>and pass it with pointer <u>retracted</u> by 8 bytes</i>
 *
 */
BasicAuthDescriptor *_Nullable

CacheLocalLruSetBasicAuthItem(const char *basicauth_b64encoded, const char *basicauth_decoded, unsigned long userid,
                              BasicAuthDescriptor *basicauth_ptr_in)
{
  uintptr_t            p;
  BasicAuthDescriptor *basicauth_ptr;

  if (IS_PRESENT(basicauth_ptr_in)) p = (uintptr_t)basicauth_ptr_in;
  else p                              = (uintptr_t)calloc(1, sizeof(BasicAuthDescriptor) + sizeof(uintptr_t));
  //extra to store a pointer to list item reference

  basicauth_ptr = (BasicAuthDescriptor*)(p + (sizeof(uintptr_t)));

  strncpy(basicauth_ptr->b64encoded, basicauth_b64encoded, SMBUF - 1);
  strncpy(basicauth_ptr->decoded, basicauth_decoded, SMBUF - 1);
  basicauth_ptr->userid = userid;

  LruClientData *data_ptr_returned = LockingLruSet(&BasicAuthLruCache, AS_LRU_CLIENT_DATA(basicauth_ptr));
  if (IS_PRESENT(data_ptr_returned)) {
    if (data_ptr_returned != basicauth_ptr) {
#ifdef __UF_TESTING
      syslog(LOG_ERR, "%s (pid:'%lu', basicauth_uid_evicted:'%lu'): Evicted list item returned...", __func__,
             pthread_self(), ((BasicAuthDescriptor*)data_ptr_returned)->userid);
#endif

      free((void*)((uintptr_t)data_ptr_returned - sizeof(uintptr_t)));
    }
  }
  else {
    if (IS_EMPTY(basicauth_ptr_in)) free((void*)(p - sizeof(uintptr_t))); //we own the allocation of this pointer

    return NULL;
  }

  return basicauth_ptr;
}

/**
 * 	@brief	Basic interface for querying the local LruCache for the existence of a given basicauth value.
 * 	If item is present in the hash, the items is promoted to the front of the list, unless it is already at the head
 */
static BasicAuthDescriptor *_Nullable

_CacheLocalLruGetBasicAuthItem(const char * _Nonnull basicauth_b64encoded)
{
  BasicAuthDescriptor *basicauth_ptr, *basicauth_ptr_evicted = NULL;

  basicauth_ptr = (BasicAuthDescriptor*)LockingLruGet(&BasicAuthLruCache, basicauth_b64encoded,
                                                      (LruClientData**)&basicauth_ptr_evicted);
  if (IS_PRESENT(basicauth_ptr_evicted)) {
#ifdef __UF_TESTING
    syslog(LOG_ERR, "%s (pid:'%lu', basicauth_uid_evicted:'%lu'): Evicted list item returned...", __func__,
           pthread_self(), ((BasicAuthDescriptor*)basicauth_ptr_evicted)->userid);
#endif

    free((void*)((uintptr_t)basicauth_ptr_evicted - sizeof(uintptr_t)));
  }

  return basicauth_ptr;
}

#define _REDIS_CMD_SET_BASICAUTH	"SET %s:%s %s:%lu EX %lu"
#define _REDIS_CMD_GET_BASICAUTH "GET %s:%s"
#define _REDIS_CMD_DEL_BASICAUTH "DEL %s:%s"

/**
 *	@dynamic_memory: EXPORTS 'char *'
 */
static UFSRVResult *
_CacheBackendGetBasicAuthItem(const char *basicauth_b64encoded)
{
  int                 rescode = RESCODE_PROG_NULL_POINTER;
  PersistenceBackend *pers_ptr;
  redisReply *        redis_ptr;

  if (unlikely((IS_EMPTY(basicauth_b64encoded)))) goto return_error_param;

  pers_ptr = THREAD_CONTEXT_SESSION_CACHEBACKEND;

  char command_buf[LBUF];
  snprintf(command_buf, LBUF - 1, _REDIS_CMD_GET_BASICAUTH, _BASICAUTH_PREFIX, basicauth_b64encoded);
  redis_ptr = (*pers_ptr->send_command)(NULL, pers_ptr, command_buf);

  if (IS_EMPTY(redis_ptr)) {
    rescode = RESCODE_BACKEND_CONNECTION;
    goto return_error_backend_connection;
  }

  if (redis_ptr->type == REDIS_REPLY_STRING) {
#ifdef __UF_FULLDEBUG
    syslog(LOG_DEBUG, "%s (pid:'%lu' o:'%p', location:'%s'): Retrieved decoded basicauth...", __func__, pthread_self(),
           sesn_ptr, redis_ptr->str);
#endif
    char *basicauth_decoded_backend = strdup(redis_ptr->str);
    freeReplyObject(redis_ptr);

    THREAD_CONTEXT_RETURN_RESULT_SUCCESS(basicauth_decoded_backend, RESCODE_PROG_NULL_POINTER)
  }

  if (redis_ptr->type == REDIS_REPLY_ERROR) goto return_error_backend_error;
  if (redis_ptr->type == REDIS_REPLY_NIL) goto return_error_backend_nil;

  goto on_return_free;

return_error_param:
  syslog(LOG_DEBUG, LOGSTR_NULL_PARAM, __func__, pthread_self(), LOGCODE_PROTO_MISSING_PARAM, "id or location");
  goto return_final;

return_error_backend_connection:
  syslog(LOG_DEBUG,
         "%s (pid:'%lu', basicauth_b64encoded:'%s'): ERROR COULD ISSUE GET COMMAND: BACKEND CONNECTIVITY ERROR",
         __func__, pthread_self(), basicauth_b64encoded);
  goto return_final;

return_error_backend_error:
  syslog(LOG_DEBUG, "%s (pid:'%lu', basicauth_b64encoded:'%s'): ERROR COULD NOT GET: REPLY ERROR '%s'", __func__,
         pthread_self(), basicauth_b64encoded, redis_ptr->str);
  goto on_return_free;

return_error_backend_nil:
  syslog(LOG_DEBUG, "%s (pid:'%lu', basicauth_b64encoded:'%s'): ERROR COULD NOT GET: NIL REPLY ERROR", __func__,
         pthread_self(), basicauth_b64encoded);
  goto on_return_free;

on_return_free:
  freeReplyObject(redis_ptr);

return_final:
  THREAD_CONTEXT_RETURN_RESULT_ERROR(NULL, rescode)

return_generic_error:
  syslog(LOG_DEBUG, LOGSTR_NULL_PARAM, __func__, pthread_self(), LOGCODE_PROTO_MISSING_PARAM, "Target Session *");
  return _ufsrv_result_generic_error;
}

UFSRVResult *
CacheBackendSetBasicAuthItem(const char *basicauth_b64encoded, const char *basicauth_decoded, unsigned long userid)
{
  int rescode = RESCODE_PROG_NULL_POINTER;

  if ((IS_EMPTY(basicauth_b64encoded)) || (IS_EMPTY(basicauth_decoded))) goto return_error_param;

  PersistenceBackend *pers_ptr;
  redisReply *        redis_ptr;

  pers_ptr = THREAD_CONTEXT_SESSION_CACHEBACKEND;

  char command_buf[LBUF] = {0};
  snprintf(command_buf, LBUF - 1, _REDIS_CMD_SET_BASICAUTH, _BASICAUTH_PREFIX, basicauth_b64encoded, basicauth_decoded,
           userid, _BASICAUTH_CACHE_EXPIRY);
  redis_ptr = (*pers_ptr->send_command)(NULL, pers_ptr, command_buf);

  if (unlikely(IS_EMPTY(redis_ptr))) goto return_error_backend_connection;

  if (strcasecmp(redis_ptr->str, "ok") == 0) {
    freeReplyObject(redis_ptr);

    THREAD_CONTEXT_RETURN_RESULT_SUCCESS(NULL, RESCODE_PROG_NULL_POINTER)
  }

  goto return_error_backend;

return_error_param:
  syslog(LOG_DEBUG, LOGSTR_NULL_PARAM, __func__, pthread_self(), LOGCODE_PROTO_MISSING_PARAM, "id or location");
  goto return_final;

return_error_backend_connection:
  syslog(LOG_DEBUG,
         "%s (pid:'%lu', basicauth_b64encoded:'%s'): ERROR COULD ISSUE SET COMMAND: BACKEND CONNECTIVITY ERROR",
         __func__, pthread_self(), basicauth_b64encoded);
  goto return_final;

return_error_backend:
  syslog(LOG_DEBUG, "%s (pid:'%lu', basicauth_b64encoded:'%s'): ERROR SET _BASICAUTH FAILED: '%s' REPLY CODE:'%d'",
         __func__, pthread_self(), basicauth_b64encoded, redis_ptr->str, redis_ptr->type);

on_return_free:
  freeReplyObject(redis_ptr);

return_final:
  THREAD_CONTEXT_RETURN_RESULT_ERROR(NULL, rescode)
}

static UFSRVResult *
_CachebackendDelBasicAuthItem(const char *basicauth_b64encoded)
{
  PersistenceBackend *pers_ptr;
  redisReply *        redis_ptr;

  pers_ptr = THREAD_CONTEXT_SESSION_CACHEBACKEND;

  if (!(redis_ptr = (*pers_ptr->send_command)(NULL, pers_ptr, _REDIS_CMD_DEL_BASICAUTH, _BASICAUTH_PREFIX,
                                              basicauth_b64encoded))) {
    syslog(LOG_DEBUG, "%s {pid:'%lu', key:'%s'}: ERROR COULD NOT DEL BASICAUTH CACHE: BACKEND CONNECTIVITY ERROR",
           __func__, pthread_self(), basicauth_b64encoded);

    THREAD_CONTEXT_RETURN_RESULT_ERROR(NULL, RESCODE_BACKEND_CONNECTION)
  }

  if (redis_ptr->type == REDIS_REPLY_INTEGER && redis_ptr->integer == 1) {
    syslog(LOG_DEBUG, "%s {pid:'%lu'}: SUCCESS BASICAUTH:'%s' DELETED...", __func__, pthread_self(),
           basicauth_b64encoded);

    freeReplyObject(redis_ptr);

    THREAD_CONTEXT_RETURN_RESULT_SUCCESS(NULL, RESCODE_PROG_NULL_POINTER)
  }
  else {
    syslog(LOG_DEBUG, "%s {pid:'%lu'}: ERROR COULD NOT DEL BASICAUTH: REPLY ERROR '%s'", __func__, pthread_self(),
           redis_ptr->str);

    freeReplyObject(redis_ptr);

    THREAD_CONTEXT_RETURN_RESULT_ERROR(NULL, RESCODE_PROG_NULL_POINTER)
  }
}


