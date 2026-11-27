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

#ifndef UFSRV_BASIC_AUTH_DESCRIPTOR_PROVIDER_H
#define UFSRV_BASIC_AUTH_DESCRIPTOR_PROVIDER_H

#include <ufsrvcorelib/ufsrv_core/http/basic_auth/basic_auth_descriptor_type.h>
#include <uflib/recycler/instance_type.h>
#include <uflib/recycler/recycler_type.h>

void InitBasicAuthDescriptorRecyclerTypePool(size_t allocation_sz);
void BasicAuthDescriptorIncrementReference(BasicAuthDescriptor *descriptor_ptr, int multiples);
void BasicAuthDescriptorDecrementReference(BasicAuthDescriptor *descriptor_ptr, int multiples);
unsigned BasicAuthDescriptorPoolTypeNumber();
InstanceHolder *BasicAuthDescriptorGetInstance(ContextData *ctx_data_ptr, unsigned long call_flags);
void BasicAuthDescriptorReturnToRecycler(InstanceHolder *, ContextData *ctx_data_ptr, unsigned long call_flags);
char *PrintBasicAuthDescriptor(ClientContextData *item_ptr, size_t index);

#endif //UFSRV_BASIC_AUTH_DESCRIPTOR_PROVIDER_H
