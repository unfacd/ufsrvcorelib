/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Fail-fast allocator wrappers for the extracted codec.
 *
 * This file is the replacement for coturn's ns_turn_utils.c, which cannot be
 * vendored as-is because it also owns logging, syslog, timers and file
 * rotation. Semantics are identical to turn_malloc_impl() & friends there:
 * log the call site and abort() rather than return NULL, because a codec that
 * cannot allocate a 40-byte key string mid-request has no recovery path.
 */

#include <ufsrvcorelib/ufsrv_core/turn/codec/turn_alloc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void turn_out_of_memory(const char *file, int line, const char *op, size_t sz)
{
  /* The logger writes into a stack buffer in coturn; here stderr is enough,
   * and it keeps this translation unit free of every other dependency. */
  fprintf(stderr, "%s:%d: out of memory: %s(%zu)\n", file, line, op, sz);
  abort();
}

void *turn_malloc_impl(size_t sz, const char *file, int line)
{
  void *ptr = malloc(sz);
  if (!ptr && sz) {
    turn_out_of_memory(file, line, "malloc", sz);
  }
  return ptr;
}

void *turn_calloc_impl(size_t number, size_t size, const char *file, int line)
{
  void *ptr = calloc(number, size);
  if (!ptr && number && size) {
    turn_out_of_memory(file, line, "calloc", number * size);
  }
  return ptr;
}

void *turn_realloc_impl(void *ptr, size_t sz, const char *file, int line)
{
  void *newptr = realloc(ptr, sz);
  if (!newptr && sz) {
    turn_out_of_memory(file, line, "realloc", sz);
  }
  return newptr;
}

char *turn_strdup_impl(const char *s, const char *file, int line)
{
  if (!s) {
    return NULL;
  }
  const size_t n   = strlen(s) + 1;
  char *       ptr = malloc(n);
  if (!ptr) {
    turn_out_of_memory(file, line, "strdup", n);
  }
  memcpy(ptr, s, n);
  return ptr;
}
