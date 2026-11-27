/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Allocator wrappers for the extracted codec.
 *
 * coturn's protocol layer calls turn_malloc()/turn_calloc()/turn_realloc()/
 * turn_strdup(), which ns_turn_utils.h defines as calls into these four
 * functions. This header carries only that contract, so the codec does not
 * pull in the server's logging, atomic and libevent headers.
 *
 * Contract (unchanged from coturn): a real allocation failure aborts the
 * process; NULL is returned only for a zero-size request or a NULL strdup
 * argument. Callers must not add NULL checks for allocation failure.
 */

#ifndef TURNPROTO_TURN_ALLOC_H
#define TURNPROTO_TURN_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *turn_malloc_impl(size_t sz, const char *file, int line);
void *turn_calloc_impl(size_t number, size_t size, const char *file, int line);
void *turn_realloc_impl(void *ptr, size_t sz, const char *file, int line);
char *turn_strdup_impl(const char *s, const char *file, int line);

#define turn_malloc(sz) turn_malloc_impl((sz), __FILE__, __LINE__)
#define turn_calloc(number, size) turn_calloc_impl((number), (size), __FILE__, __LINE__)
#define turn_realloc(ptr, sz) turn_realloc_impl((ptr), (sz), __FILE__, __LINE__)
#define turn_strdup(s) turn_strdup_impl((s), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* TURNPROTO_TURN_ALLOC_H */
