#ifndef SRC_INCLUDE_DICTIONARY_H_
#define SRC_INCLUDE_DICTIONARY_H_

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
#include "dictionary_type.h"
#include "vector_type.h"


/**
 * @short Flags to change some parameters of each key.
 * @ingroup dict
 */
enum onion_dict_flags_e{
//	OD_EMPTY=1,
	OD_FREE_KEY=2,     ///< Whether the key has to be removed at free time
	OD_FREE_VALUE=4,   ///< Whether the value has to be removed at free time
	OD_FREE_ALL=6,     ///< Whether both, the key and value have to be removed at free time. In any case its also marked for freeing later.
	OD_DUP_KEY=0x12,   ///< Whether the key has to be dupped
	OD_DUP_VALUE=0x24, ///< Whether the value has to be dupped
	OD_DUP_ALL=0x36,   ///< Whether both, the key and value have to be dupped. In any case its also marked for freeing later.
	OD_REPLACE=0x040,  ///< If already exists, replaces content.

	// Types
	OD_STRING=0,       ///< Stored data is a string, this is the most normal situation
	OD_DICT=0x0100,    ///< Stored data is another dictionary

	OD_TYPE_MASK=0x0FF00, ///< Mask for the types

  // Flags for onion_dict_set_flags
  OD_ICASE=0x01,     ///< Do case insensitive cmps.
};

/// Initializes a dict.
PUBLIC_API onion_dict *onion_dict_new();

PUBLIC_API void onion_dict_set_flags(onion_dict *dict, int flags);

/// Adds a value. Flags are or from onion_dict_flags_e, for example OD_DUP_ALL. @see onion_dict_flags_e
PUBLIC_API void onion_dict_add(onion_dict *dict, const char *key, const void *value, int flags);

/// Removes a value
PUBLIC_API int onion_dict_remove(onion_dict *dict, const char *key);

/// Removes the full dict struct form mem.
PUBLIC_API void onion_dict_free(onion_dict *dict);

/// Merges argument dictionary into current
PUBLIC_API void onion_dict_merge(onion_dict *me, const onion_dict *other);

/// Creates a soft duplicate of the dict.
PUBLIC_API onion_dict *onion_dict_dup(onion_dict *dict);

/// Creates a hard duplicate of the dict.
PUBLIC_API onion_dict *onion_dict_hard_dup(onion_dict *dict);

/// Gets a value
PUBLIC_API const char *onion_dict_get(const onion_dict *dict, const char *key);

/// Gets a value, recursively over the nested dicts, until NULL.
PUBLIC_API const char *onion_dict_rget(const onion_dict *dict, const char *key, ...);

/// Gets a dict. It ensures its a dict.
PUBLIC_API onion_dict *onion_dict_get_dict(const onion_dict *dict, const char *key);

/// Gets a dict. It ensures its a dict. Recursively until NULL.
PUBLIC_API onion_dict *onion_dict_rget_dict(const onion_dict *dict, const char *key, ...);

/// Prints a dot ready graph to stderr
PUBLIC_API void onion_dict_print_dot(const onion_dict *dict);

/// Signature of the visitor passed to onion_dict_preorder().
typedef void (*onion_dict_visitor)(void *data, const char *key, const void *value, int flags);

/// Visits the full graph in preorder, calling that function on each node.
PUBLIC_API void onion_dict_preorder(const onion_dict *dict, onion_dict_visitor func, void *data);

/// Counts elements
PUBLIC_API int onion_dict_count(const onion_dict *dict);

/// @{ @name lock management
/// Locks for reading. Several can read, one can write.
PUBLIC_API void onion_dict_lock_read(const onion_dict *dict);
/// Locks for writing
PUBLIC_API void onion_dict_lock_write(onion_dict *dict);
/// Unlocks last lock
PUBLIC_API void onion_dict_unlock(onion_dict *dict);
/// @}

/// Converts a dict into a JSON onion_block
PUBLIC_API onion_block *onion_dict_to_json(onion_dict *dict);
/// Converts a C string into a dictionary
PUBLIC_API onion_dict *onion_dict_from_json(const char *data);





#endif /* SRC_INCLUDE_DICTIONARY_H_ */
