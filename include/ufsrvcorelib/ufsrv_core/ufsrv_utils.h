/**
 * @file ufsrv_utils.h
 * @brief Listener and server registry utility declarations.
 */
#ifndef UFSRVCORELIB_UFSRV_UTILS_H
#define UFSRVCORELIB_UFSRV_UTILS_H

#include <stdbool.h>
#include <time.h>

#include <ufsrvcorelib/ufsrvcorelib_defs.h>

#include "delegator_sessions/ufsrv_listener_descriptor_type.h"

typedef struct CollectionDescriptor CollectionDescriptor;
typedef struct PersistenceBackend PersistenceBackend;
typedef struct Protocol Protocol;
typedef struct RateLimitHandle RateLimitHandle;
typedef struct ScheduledJobs ScheduledJobs;
typedef struct Socket Socket;
typedef struct UfsrvSession UfsrvSession;

/**
 * @brief Validate configured listener protocol keys and bind them to registry entries.
 *
 * The registry ends with an entry whose name and key are both empty or NULL.
 * Each listener must have exactly one case-sensitive match on Protocol.protocol_key.
 * An empty collection is invalid. On failure, existing bindings are preserved.
 * Call before starting listeners. The registry must outlive the bound descriptors.
 * @code{.c}
 * bool valid = IsUfsrvListenersDescriptorValid(UfsrvConfigListenersAsPtr(), ProtocolsRegistry);
 * @endcode
 */
PUBLIC_API bool IsUfsrvListenersDescriptorValid(const CollectionDescriptor *listeners, const Protocol *protocols);

/**
 * @brief Visit listener descriptors in collection order with a synchronous block.
 *
 * Return false from the block to stop iteration. The caller owns any resources
 * created by earlier invocations and releases them on failure.
 * @code{.c}
 * bool complete = UfsrvListenerForEach(&listeners, ^bool(UfsrvListenerDescriptor *listener, size_t index) {
 *   return PrepareListener(listener, index);
 * });
 * @endcode
 */
PUBLIC_API bool UfsrvListenerForEach(const CollectionDescriptor *listeners,
                                   bool (^visit)(UfsrvListenerDescriptor *listener, size_t index));

/**
 * @brief Run one accept loop over prepared listener sockets and an optional console.
 *
 * The socket array must have one element per descriptor, in collection order.
 * The loop closes the supplied fds on exit. The caller owns the socket objects.
 * Returns zero on signal-driven shutdown and negative on error.
 * @code{.c}
 * int result = UfsrvMainListenerStart(&listeners, listener_sockets, console);
 * @endcode
 */
PUBLIC_API int UfsrvMainListenerStart(const CollectionDescriptor *listeners, Socket *const *sockets, Socket *console);

PUBLIC_API Socket * UfsrvListenerBootstrap(UfsrvListenerDescriptor *listener);
/**
 * @brief Register the local server instance in the registry.
 * @code{.c}
 * bool registered = UfsrvConfigRegisterUfsrverInstance(persistence);
 * @endcode
 */
PUBLIC_API bool UfsrvConfigRegisterUfsrverInstance(PersistenceBackend *persistence);

/**
 * @brief Fetch the IDs and activity times of servers in a group.
 * @code{.c}
 * CollectionDescriptor *servers = UfsrvConfigGetGeoGroup(session, server_class, group, &ids, &times);
 * @endcode
 */
PUBLIC_API CollectionDescriptor *UfsrvConfigGetGeoGroup(UfsrvSession *session, const char *server_class,
                                                        unsigned group, CollectionDescriptor *ids,
                                                        CollectionDescriptor *times);

/**
 * @brief Advance and return the request counter for a server group.
 * @code{.c}
 * long long request_id = UfsrvConfigGetReqid(session, server_class, group);
 * @endcode
 */
PUBLIC_API long long UfsrvConfigGetReqid(UfsrvSession *session, const char *server_class, int group);

/**
 * @brief Return the last recorded activity time for a server instance.
 * @code{.c}
 * time_t last_activity = UfsrvConfigGetUfsrverActivityTime(session, server_class, group, server_id);
 * @endcode
 */
PUBLIC_API time_t UfsrvConfigGetUfsrverActivityTime(UfsrvSession *session, const char *server_class,
                                                   int group, int server_id);

/**
 * @brief Return the scheduler store shared by server startup jobs.
 * @code{.c}
 * ScheduledJobs *jobs = GetScheduledJobsStore();
 * @endcode
 */
PUBLIC_API ScheduledJobs *GetScheduledJobsStore(void);

#endif
