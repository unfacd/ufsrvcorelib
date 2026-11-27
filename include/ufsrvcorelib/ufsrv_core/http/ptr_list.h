/*
	Onion HTTP server library
	Copyright (C) 2010-2016 David Moreno Montero and others

	This library is free software; you can redistribute it and/or
	modify it under the terms of, at your choice:

	a. the Apache License Version 2.0.

	b. the GNU General Public License as published by the
		Free Software Foundation; either version 2.0 of the License,
		or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of both libraries, if not see
	<http://www.gnu.org/licenses/> and
	<http://www.apache.org/licenses/LICENSE-2.0>.
	*/

#ifndef __INCLUDE_PTR_LIST_H
#define __INCLUDE_PTR_LIST_H

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <stdbool.h>
#include <stddef.h>
#include "ufsrvcorelib/ufsrv_core/http/ptr_list_type.h"

PUBLIC_API onion_ptr_list *onion_ptr_list_new();
PUBLIC_API onion_ptr_list *onion_ptr_list_add(onion_ptr_list *l, void *ptr);
PUBLIC_API onion_ptr_list *onion_ptr_list_remove(onion_ptr_list *l, void *ptr);
PUBLIC_API void onion_ptr_list_free(onion_ptr_list *l);
PUBLIC_API void onion_ptr_list_foreach(onion_ptr_list *l, void (*f)(void *ptr));
PUBLIC_API onion_ptr_list *onion_ptr_list_filter(onion_ptr_list *l, bool (*f)(void *data, void *ptr), void *data);
PUBLIC_API size_t onion_ptr_list_count(onion_ptr_list *l);


#endif
