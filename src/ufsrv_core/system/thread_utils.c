/**
 * Copyright (C) 2015-2021 unfacd works
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

#ifdef HAVE_CONFIG_UFNETCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

/* Locked-objects store geometry. Keyed on the object pointer by default. */
#define CONFIG_THREAD_LOCKED_OBJECTS_STORE_PFACTOR  6
#define CONFIG_THREAD_LOCKED_OBJECTS_STORE_KEYLEN   0

#include <assert.h>
#include <uflib/utils.h>
#include <ufsrv_core/system/thread_utils.h>

/**
 * @brief Prepare a locked-objects store for the calling thread.
 *
 * The store keys on the first CONFIG_THREAD_LOCKED_OBJECTS_STORE_KEYLEN bytes of
 * the recorded object (key_offset 0), hashed with uflib's byte hash.  The object
 * pointers themselves arrive through the normal insert/lookup/remove calls.
 *
 * @return 0 on success, -1 if the bucket array could not be allocated.
 */
int
InitLockedObjectsStore(HopscotchHashtableConfigurable *store)
{
  if (hopscotch_init_with_offset(&store->hashtable, CONFIG_THREAD_LOCKED_OBJECTS_STORE_PFACTOR) == NULL)	return -1;

  store->keylen = CONFIG_THREAD_LOCKED_OBJECTS_STORE_KEYLEN;
  store->key_offset = 0;
  store->hash_func = hash_bytes_u64;

  return 0;
}

int
PutIntoLockedObjectsStore(HopscotchHashtableConfigurable *ht_ptr, void *obj_ptr)
{
	if (IsObjectInLockedObjectsStore(ht_ptr, obj_ptr))	return 0;

	if ((hopscotch_insert_configurable(ht_ptr, (uint8_t *)obj_ptr)) == 0)	return 0;

	return_error:
	return -1;

}

int __attribute__ (( no_sanitize_thread ))
RemoveFromLockedObjectsStore(HopscotchHashtableConfigurable *ht_ptr, void *obj_ptr)
{

	if (!IsObjectInLockedObjectsStore(ht_ptr, obj_ptr))	goto return_error;

	void *obj_ptr_removed;
	if (IS_PRESENT((obj_ptr_removed = hopscotch_remove_configurable(ht_ptr, obj_ptr)))) {
		assert(obj_ptr_removed == obj_ptr);
		return 0;
	}

	return_error:
	return -1;

}

