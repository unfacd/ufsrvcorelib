#ifndef UFSRVCORELIB_SESSION_LAYOUT_H
#define UFSRVCORELIB_SESSION_LAYOUT_H

#include <stddef.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>

typedef struct SessionStorageLayout {
  size_t size;
  size_t alignment;
} SessionStorageLayout;

PUBLIC_API SessionStorageLayout UfsrvSessionGetLayout(void);

#endif
