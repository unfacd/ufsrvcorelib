/**
 * @file ufsrvcorelib_defs.h
 * @brief Preprocessor definitions shared across all public headers of ufsrvcorelib.
 *
 * Copyright (C) 2015-2026 unfacd works
 */

#ifndef UFSRVCORELIB_DEFS_H
#define UFSRVCORELIB_DEFS_H

#include <pthread.h>
#include <syslog.h>

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

/**
 * Trace macros. Each names the emitting function and its thread, matching the
 * rest of the library's syslog output, and compiles out unless the build
 * selects its switch. UFSRV_LOG() carries no level and follows the base debug
 * switch, which either level implies.
 */
#if UFSRV_DEBUG_BUILD
# define UFSRV_LOG(fmt, ...) \
  syslog(LOG_DEBUG, "%s {pid:'%lu'}: " fmt, __func__, pthread_self(), ##__VA_ARGS__)
#else
# define UFSRV_LOG(fmt, ...) ((void)0)
#endif

#if UFSRV_DEBUG_BUILD_L2
# define UFSRV_LOG_L2(fmt, ...) \
  syslog(LOG_DEBUG, "%s {pid:'%lu'}: " fmt, __func__, pthread_self(), ##__VA_ARGS__)
#else
# define UFSRV_LOG_L2(fmt, ...) ((void)0)
#endif

#if UFSRV_DEBUG_BUILD_L3
# define UFSRV_LOG_L3(fmt, ...) \
  syslog(LOG_DEBUG, "%s {pid:'%lu'}: " fmt, __func__, pthread_self(), ##__VA_ARGS__)
#else
# define UFSRV_LOG_L3(fmt, ...) ((void)0)
#endif

#endif /* UFSRVCORELIB_DEFS_H */
