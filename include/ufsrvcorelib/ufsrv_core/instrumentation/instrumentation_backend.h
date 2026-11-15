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

#ifndef INCLUDE_INSTRUMENTATION_BACKEND_H_
#define INCLUDE_INSTRUMENTATION_BACKEND_H_

#include <netinet/in.h>
#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <uflib/buffer_descriptor/buffer_descriptor_type.h>
#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend_type.h>

PUBLIC_API int InstrumentationBackendServerInit(const char *host, int port, struct sockaddr_in *instrumentation_backend_server);
PUBLIC_API InstrumentationBackend *InstrumentationBackendInit(const char *ns, InstrumentationBackend *instrum_backend_provided, const char *server_class, int serverid_by_uservoid);
PUBLIC_API void InstrumentationBackendReset(InstrumentationBackend *);
PUBLIC_API int statsd_send(InstrumentationBackend *link, const char *message, struct sockaddr_in *instrumentation_backend_server);

PUBLIC_API int statsd_count(InstrumentationBackend *link, char *stat, ssize_t value, float sample_rate, struct sockaddr_in *instrumentation_backend_server);
PUBLIC_API int statsd_dec(InstrumentationBackend *link, char *stat, float sample_rate, struct sockaddr_in *instrumentation_backend_server);
PUBLIC_API int statsd_inc(InstrumentationBackend *link, char *stat, float sample_rate, struct sockaddr_in *instrumentation_backend_server);
PUBLIC_API int statsd_gauge(InstrumentationBackend *link, char *stat, ssize_t value, struct sockaddr_in *instrumentation_backend_server);
PUBLIC_API int statsd_gauge_inc(InstrumentationBackend *link, char *stat_name,  ssize_t value, struct sockaddr_in *instrumentation_backend_server);
PUBLIC_API int statsd_gauge_dec(InstrumentationBackend *link, char *stat_name,  ssize_t value, struct sockaddr_in *instrumentation_backend_server);
PUBLIC_API int statsd_timing(InstrumentationBackend *link, char *stat, ssize_t ms, struct sockaddr_in *instrumentation_backend_server);

BufferDescriptor * InstrumentationBackendAsString(const InstrumentationBackend *backend, BufferDescriptor *buffer_provided);

#endif /* SRC_INCLUDE_INSTRUMENTATION_BACKEND_H_ */
