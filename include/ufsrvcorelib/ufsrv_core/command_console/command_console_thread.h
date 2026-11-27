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

#ifndef SRC_INCLUDE_COMMAND_CONSOLE_THREAD_H_
#define SRC_INCLUDE_COMMAND_CONSOLE_THREAD_H_

#include <ufsrvcorelib/ufsrvcorelib_defs.h>
#include <uflib/net/socket_type.h>
#include <uflib/buffer_descriptor/buffer_descriptor_type.h>

//Command handler: turn a parsed "METHOD /path" request into a JSON response.
//The console thread invokes the registered handler (if any) and writes the
//returned BufferDescriptor back to the peer.
typedef BufferDescriptor *(*ConsoleCommandHandler)(const char *method, const char *path, BufferDescriptor *buffer);

PUBLIC_API void *ThreadCommandConsoleClient(void *ptr);
PUBLIC_API int AnswerCommandConsoleRequest (Socket *s_ptr);
PUBLIC_API Socket *SetupCommandConsole (void);

PUBLIC_API void ConsoleThreadRegisterCommandHandler(ConsoleCommandHandler handler);


typedef struct UfsrvCommandConsole UfsrvCommandConsole;

/**
 * Start a standalone TLS console using the shared console configuration.
 * The caller owns the returned service and stops it before config teardown.
 *
 * @code{.c}
 * UfsrvCommandConsole *console = UfsrvCommandConsoleStart(ResolveCommand);
 * if (console) UfsrvCommandConsoleStop(console);
 * @endcode
 */
PUBLIC_API UfsrvCommandConsole *UfsrvCommandConsoleStart(ConsoleCommandHandler handler);

/**
 * Interrupt socket I/O, join the console thread, and release its resources.
 *
 * @code{.c}
 * UfsrvCommandConsoleStop(console);
 * @endcode
 */
PUBLIC_API void UfsrvCommandConsoleStop(UfsrvCommandConsole *console);


#endif /* SRC_INCLUDE_COMMAND_CONSOLE_THREAD_H_ */
