/*

 Copyright (c) 2015-2026 unfacd works

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#ifndef SRC_INCLUDE_BACKENDCONFIG_TYPE_H_
#define SRC_INCLUDE_BACKENDCONFIG_TYPE_H_

#include <sys/time.h>

// typedef void * PersistenceBackendType; //opaque type

	struct BackendConfig {
	    unsigned type;
	    const char *backend_label;

	    struct {
	        const char *host;
	        unsigned port;
	        struct timeval timeout;
	    } con_tcp;
	    struct {
	        const char *path;
	    } con_unix;

	    // PersistenceBackendType cache_backend_ptr; ///> opaque type holding the persistence agents connection handle
	};

typedef struct BackendConfig BackendConfig;

#endif /* SRC_INCLUDE_BACKENDCONFIG_TYPE_H_ */
