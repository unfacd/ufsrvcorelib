/*
	Onion HTTP server library
	Copyright (C) 2010-2015 David Moreno Montero and others

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

#ifndef __INCLUDE_CODECS_H
#define __INCLUDE_CODECS_H

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <stdlib.h>
#include <sys/types.h>
#include "ufsrvcorelib/ufsrv_core/http/vector_type.h"

/// Decodes a base64 into a new char* (must be freed later).
PUBLIC_API char *onion_base64_decode(const char *orig, int *length);

/// Encodes a byte array to a base64 into a new char* (must be freed later).
PUBLIC_API char *onion_base64_encode(const char *orig, int length);

/// Performs URL unquoting
PUBLIC_API void onion_unquote_inplace(char *str);

/// Performs URL quoting, memory is allocated and has to be freed.
PUBLIC_API char *onion_quote_new(const char *str);

/// Performs URL quoting, uses auxiliary res, with maxlength size. If more, do up to where I can, and cut it with \0.
PUBLIC_API int onion_quote(const char *str, char *res, int maxlength);

/// Performs C quotation: changes " for \". Usefull when sending data to be interpreted as JSON.
PUBLIC_API char *onion_c_quote_new(const char *str);

/// Performs the C quotation on the ret str. Max length is l.
PUBLIC_API char *onion_c_quote(const char *str, char *ret, int l);

/// Calculates the sha1 checksum
PUBLIC_API void onion_sha1(const char *data, int length, char *result);

/// Calculates the HTML encoding of a string. Returned value must be freed. If no encoding needed, returns NULL.
PUBLIC_API char *onion_html_quote(const char *str);

/// Always return a freshly allocated string, to be later freed.
PUBLIC_API const char* onion_html_quote_dup(const char*str);

/// Generates JSON string encoding and adds it to an existing block
PUBLIC_API void onion_json_quote_add(onion_block *block, const char *str);

/// Adds to the block the quoted string; converts "\\n" to "\n"
PUBLIC_API ssize_t onion_json_unquote_add(onion_block *block, const char *str);

PUBLIC_API char *DecodeUrl(const char *s, size_t len);

#endif
