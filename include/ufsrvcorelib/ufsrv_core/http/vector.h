#ifndef SRC_INCLUDE_VECTOR_H_
#define SRC_INCLUDE_VECTOR_H_

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

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <sys/types.h>
#include "ufsrvcorelib/ufsrv_core/http/vector_type.h"

PUBLIC_API onion_block *onion_block_new();
PUBLIC_API void onion_block_free(onion_block *b);
PUBLIC_API void onion_block_clear(onion_block *b);

PUBLIC_API void onion_block_min_maxsize(onion_block *b, int minsize);
PUBLIC_API off_t onion_block_size(const onion_block *b);
PUBLIC_API const char *onion_block_data(const onion_block *b);

PUBLIC_API void onion_block_rewind(onion_block *b, off_t n);

PUBLIC_API int onion_block_add_char(onion_block *b, char c);
PUBLIC_API int onion_block_add_str(onion_block *b, const char *str);
PUBLIC_API int onion_block_add_data(onion_block *b, const char *data, size_t length);
PUBLIC_API int onion_block_add_block(onion_block *b, onion_block *toadd);


#endif /* SRC_INCLUDE_VECTOR_H_ */
