#ifndef UFSRVCORELIB_RATELIMIT_HANDLE_TYPE_H
#define UFSRVCORELIB_RATELIMIT_HANDLE_TYPE_H

/* Storage backend, result record and application policy are owned by the
 * library that implements rate limiting; only their addresses are held here. */
typedef struct RateLimitBackend           RateLimitBackend;
typedef struct RateLimitResult            RateLimitResult;
typedef struct RateLimitApplicationPolicy RateLimitApplicationPolicy;

/*!
 * Per-call handle that binds a caller-supplied backend to an optional result
 * record.
 *
 * `result` may be NULL when the caller only cares about the return code and
 * the status out-parameter.
 */
typedef struct RateLimitHandle
{
  struct RateLimitBackend *   backend;    ///< storage backend (memory or Redis)
  RateLimitResult *           result;     ///< optional; filled on every call
  RateLimitApplicationPolicy *app_policy; ///< application defined policies that change ratelimit behaviour
} RateLimitHandle;

#endif
