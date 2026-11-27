#include <ufsrv_core/delegator_sessions/sessions_delegator_type.h>

static SessionsDelegator sessions_delegator;

/* Transitional export: every existing `extern SessionsDelegator *const
 * sessions_delegator_ptr` keeps resolving until WP5 converts the call sites
 * to SessionsDelegatorPtr(). */
SessionsDelegator *const sessions_delegator_ptr = &sessions_delegator;

SessionsDelegator *
SessionsDelegatorPtr(void)
{
  return &sessions_delegator;
}
