/**
 * @file ufsrvcorelib_defs.h
 * @brief Preprocessor definitions shared across all public headers of ufsrvcorelib.
 *
 * Copyright (C) 2015-2026 unfacd works
 */

#ifndef UFSRVCORELIB_DEFS_H
#define UFSRVCORELIB_DEFS_H

/*!
 * Marker for public API functions — exported from the shared library.
 *
 * Place before the return type of every function that is part of a module's
 * public contract.  With -fvisibility=hidden, only PUBLIC_API symbols are
 * visible to consumers.  The attribute is harmless (no-op) in static builds.
 *
 * @code{.c}
 * PUBLIC_API void ConfFileDestroy(ConfFile cp);
 * PUBLIC_API int  ConfFileGetInt(ConfFile cp, const char *scope, const char *key);
 * @endcode
 */
#ifndef PUBLIC_API
  #if defined(__GNUC__) || defined(__clang__)
    #define PUBLIC_API __attribute__((visibility("default")))
  #else
    #define PUBLIC_API
  #endif
#endif

#endif /* UFSRVCORELIB_DEFS_H */
