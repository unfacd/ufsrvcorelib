#include <ufsrv_core/delegator_sessions/base_thread_context_data_type.h>
#include <ufsrv_core/delegator_sessions/thread_context_type.h>

/* One global multiplexed as thread local storage for each session worker
 * thread. Note this is actual struct storage, not a pointer. */
__thread ThreadContext ufsrv_thread_context;

/* Carried over from the old Sfu layout; the type it points at now lives here. */
__thread BaseThreadContext *base_thread_context;
