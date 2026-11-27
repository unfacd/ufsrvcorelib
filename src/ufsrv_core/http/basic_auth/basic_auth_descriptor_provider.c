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

#include <ufsrvcorelib/ufsrv_core/http/basic_auth/basic_auth_descriptor_provider.h>
#include <uflib/recycler/recycler.h>
#include <ufsrv_core/system/log_message_literals.h>
#include <pthread.h>
#include <sys/syslog.h>

//IMPORTANT this is not yet general purpose: one instance per server, until static reference below are replaced
static RecyclerPoolHandle *BasicAuthDescriptorPoolHandle;

static int TypePoolInitCallback_BasicAuthDescriptor(ClientContextData *data_ptr, size_t oid);
static int TypePoolGetInitCallback_BasicAuthDescriptor(InstanceHolder *data_ptr, ContextData *context_data, size_t oid,
                                                       unsigned long   call_flags);
static int TypePoolPutInitCallback_BasicAuthDescriptor(InstanceHolder *data_ptr, ContextData *context_data,
                                                       unsigned long   call_flags);
static char *TypePoolPrintCallback_BasicAuthDescriptor(InstanceHolder *data_ptr, ContextData *context_data,
                                                       unsigned long   call_flags);
static int TypePoolDestructCallback_BasicAuthDescriptor(InstanceHolder *data_ptr, ContextData *context_data,
                                                        unsigned long   call_flags);

static RecyclerPoolOps ops_basicauth_descriptor = {
  TypePoolInitCallback_BasicAuthDescriptor, TypePoolGetInitCallback_BasicAuthDescriptor,
  TypePoolPutInitCallback_BasicAuthDescriptor, TypePoolPrintCallback_BasicAuthDescriptor,
  TypePoolDestructCallback_BasicAuthDescriptor
};

//----------- Recycer Type Pool BasicAuthDescriptor ---- //
//ufsrvwebsock etc use _CONF_SESNMEMSPECS_ALLOC_GROUPS(masterptr)
void
InitBasicAuthDescriptorRecyclerTypePool(size_t allocation_sz)
{
#define _BasicAuthDescriptor_EXPANSION_THRESHOLD (1024 * 10)

  BasicAuthDescriptorPoolHandle = RecyclerInitTypePool("BasicAuthDescriptor",
                                                       sizeof(BasicAuthDescriptor) + sizeof(uintptr_t), allocation_sz,
                                                       _BasicAuthDescriptor_EXPANSION_THRESHOLD,
                                                       &ops_basicauth_descriptor);

  syslog(LOG_INFO, "%s: Initialised TypePool: '%s'. TypeNumber:'%d', Block Size:'%lu'", __func__,
         BasicAuthDescriptorPoolHandle->type_name, BasicAuthDescriptorPoolHandle->type,
         BasicAuthDescriptorPoolHandle->blocksz);
}

void
BasicAuthDescriptorIncrementReference(BasicAuthDescriptor *descriptor_ptr, int multiples)
{
  RecyclerTypeReferenced(2, (RecyclerClientData*)descriptor_ptr, multiples);
}

void
BasicAuthDescriptorDecrementReference(BasicAuthDescriptor *descriptor_ptr, int multiples)
{
  RecyclerTypeUnReferenced(2, (RecyclerClientData*)descriptor_ptr, multiples);
}

__pure unsigned
BasicAuthDescriptorPoolTypeNumber()
{
  unsigned type = BasicAuthDescriptorPoolHandle->type;
  return type;
}

InstanceHolderForBasicAuthDescriptor *
BasicAuthDescriptorGetInstance(ContextData *ctx_data_ptr, unsigned long call_flags)
{
  InstanceHolderForBasicAuthDescriptor *instance_holder_ptr = RecyclerGet(
    BasicAuthDescriptorPoolTypeNumber(), ctx_data_ptr, call_flags);
  if (unlikely(IS_EMPTY(instance_holder_ptr))) goto return_error;

  return instance_holder_ptr;

return_error:
  syslog(LOG_DEBUG, LOGSTR_INCONSISTENT_STATE, __func__, pthread_self(), (void*)0, 0UL,
         LOGCODE_PROTO_INCONSISTENT_STATE, "Could not get BasicAuthDescriptor instance");
  return NULL;
}

void
BasicAuthDescriptorReturnToRecycler(InstanceHolder *instance_holder_ptr, ContextData *ctx_data_ptr,
                                    unsigned long   call_flags)
{
  RecyclerPut(BasicAuthDescriptorPoolTypeNumber(), instance_holder_ptr, (ContextData*)ctx_data_ptr, call_flags);
}

/**
 * 	@brief: "constructor" type intialiser for newly instantiated objects just before attaching them to the recycler.
 * 	One off for the object's lifetime. No InstanceHolder ref yet.
 *
 */
static int
TypePoolInitCallback_BasicAuthDescriptor(ClientContextData *data_ptr, size_t oid)
{
  BasicAuthDescriptor *descriptor_ptr = (BasicAuthDescriptor*)data_ptr;

  return 0; //success
}

/**
 * 	@param ContextData: whatever  context data we might have passed to the recycler when we issued Get().
 */
static int
TypePoolGetInitCallback_BasicAuthDescriptor(InstanceHolder *data_ptr, ContextData *context_data, size_t oid,
                                            unsigned long   call_flags)
{
  InstanceHolderForBasicAuthDescriptor *instance_descriptor_ptr = (InstanceHolderForBasicAuthDescriptor*)data_ptr;

  return 0; //success
}

/**
 * 	@param ContextData: whatever  context data we might havepassed to the recycler when we issued Put In this instance Fence *
 */
static int
TypePoolPutInitCallback_BasicAuthDescriptor(InstanceHolder *data_ptr, ContextData *context_data,
                                            unsigned long   call_flags)
{
  InstanceHolderForBasicAuthDescriptor *instance_descriptor_ptr = (InstanceHolderForBasicAuthDescriptor*)data_ptr;

  return 0; //success
}

static char *
TypePoolPrintCallback_BasicAuthDescriptor(InstanceHolder *data_ptr, ContextData *context_data, unsigned long call_flags)
{
  InstanceHolderForBasicAuthDescriptor *instance_descriptor_ptr = (InstanceHolderForBasicAuthDescriptor*)data_ptr;

  return NULL;
}

static int
TypePoolDestructCallback_BasicAuthDescriptor(InstanceHolder *data_ptr, ContextData *context_data,
                                             unsigned long   call_flags)
{
  InstanceHolderForBasicAuthDescriptor *instance_descriptor_ptr = (InstanceHolderForBasicAuthDescriptor*)data_ptr;

  return 0; //success
}

/**
 * @brief Default item printer for BasicAuthDescriptor LRU items
 * @param item_ptr ClientContextData-aliased and un-extracted BasicAuthDescriptor
 * @param index item iterator index position
 * @return
 */
char *
PrintBasicAuthDescriptor(ClientContextData *item_ptr, size_t index)
{
  BasicAuthDescriptor *basic_auth_ptr = (BasicAuthDescriptor*)item_ptr;
  syslog(LOG_ERR, "%s (pid:'%lu', uid:'%lu', idx:'%lu'): ListItem Client Data", __func__, pthread_self(),
         basic_auth_ptr->userid, index);

  return NULL;
}