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

#ifndef UFSRV_BASIC_AUTH_DESCRIPTOR_TYPE_H
#define UFSRV_BASIC_AUTH_DESCRIPTOR_TYPE_H

#include <uflib/standard_defs.h>
#include <uflib/recycler/instance_type.h>

typedef InstanceHolder InstanceHolderForBasicAuthDescriptor;

typedef struct BasicAuthDescriptor {
    unsigned long userid;
    char b64encoded[SMBUF];
    char decoded[SMBUF];
}	BasicAuthDescriptor;

#define AS_BASIC_AUTH_DESCRIPTOR(x) ((BasicAuthDescriptor *)(x))

#endif //UFSRV_BASIC_AUTH_DESCRIPTOR_TYPE_H
