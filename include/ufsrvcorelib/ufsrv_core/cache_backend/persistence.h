/**
 * Copyright (C) 2015-2026 unfacd works
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

#ifndef SRC_INCLUDE_PERSISTENCE_H_
#define SRC_INCLUDE_PERSISTENCE_H_

#include <ufsrv_core/cache_backend/persistence_type.h>
#include <ufsrv_core/session/ufsrv_session_type.h>

unsigned DisconnectPersistanceBackend(UfsrvSession *, int);//redisContext *c, int keep_fd);
PersistenceBackend *InitialisePersistenceBackend(PersistenceBackend *, const char* server_address, unsigned server_port, const char* descriptive_label, bool
                                                 is_re_initialised);
UserMessageCacheBackend *InitialiseCacheBackendUserMessage(PersistenceBackend *in_per_ptr, const char* server_address, unsigned server_port, const char* descriptive_label, bool
                                                           is_re_initialised);
UserMessageCacheBackend *InitialiseCacheBackendFence(PersistenceBackend *in_per_ptr, const char* server_address, unsigned server_port, const char* descriptive_name, bool
                                                     is_re_initialised);
void PrintPersistenceError(UfsrvSession *sesn_ptr, char *user_str)__attribute__((unused));

#endif /* SRC_INCLUDE_PERSISTENCE_H_ */
