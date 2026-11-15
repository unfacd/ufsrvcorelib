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


#ifndef UFSRV_UTILS_JEMALLOC_H
#define UFSRV_UTILS_JEMALLOC_H

#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include "ufsrv_console_script/ufsrv_console_script_type.h"

/**
 * @brief Return a JSON description of the linked jemalloc allocator.
 *
 * Builds a compact JSON document of the form:
 *
 * @code{.unparsed}
 * {
 *   "version":  "<jemalloc version string>",
 *   "features": {
 *     "on":    { <build-time feature>: true,  ... },
 *     "off":   { <build-time feature>: false, ... },
 *     "stats": { <live statistic>: <value>,    ... }
 *   }
 * }
 * @endcode
 *
 * "features" carries:
 *
 *   "on"  — build-time feature switches that are compiled in (reported true):
 *     debug            debug-mode build (assertions and extra checks)
 *     stats            statistics support compiled in (the stats.* keys exist)
 *     prof             heap profiling compiled in (the prof.* keys exist)
 *     fill             freed memory is junk-filled (poisoned)
 *     utrace           utrace instrumentation hooks compiled in
 *     xmalloc          abort-on-out-of-memory behaviour compiled in
 *     cache_oblivious  cache-oblivious large-allocation layout compiled in
 *     lazy_lock        lazy (on-demand) mutex initialisation compiled in
 *
 *   "off" — the same feature switches compiled out (reported false).  A
 *           feature absent from the build is omitted entirely rather than
 *           listed as "off".
 *
 *   "stats" — live allocator statistics (in bytes unless noted), freshly
 *             recomputed via an epoch bump:
 *     allocated  bytes currently allocated by the application (live)
 *     active     bytes in active pages backing those allocations
 *     metadata   bytes used by jemalloc's own internal bookkeeping
 *     resident   bytes resident in physical memory (RSS-like)
 *     mapped     total bytes mmap'd from the operating system
 *     retained   bytes pooled for reuse, not yet returned to the OS
 *     narenas    number of arenas currently in use
 *
 * "version" — the jemalloc version string this process was built against.
 *
 * Future extensions (jemalloc mallctl keys not yet reported — available to
 * add as needed):
 *
 *   Thread-scoped (per calling thread):
 *     thread.arena            arena index the calling thread is assigned to
 *     thread.allocated        cumulative bytes allocated by this thread
 *     thread.deallocated      cumulative bytes deallocated by this thread
 *     thread.tcache.enabled   whether this thread's thread-cache is enabled
 *
 *   Per-arena breakdown:
 *     stats.arenas.<i>.allocated      live bytes for arena <i>
 *     stats.arenas.<i>.resident       resident bytes for arena <i>
 *     stats.arenas.<i>.bins.<j>.*     per-size-class bin statistics
 *     arenas.create / arenas.lookup / arenas.destroy   arena lifecycle
 *
 *   Decay and background purge:
 *     arenas.dirty_decay_ms / arenas.muzzy_decay_ms    dirty/muzzy page decay
 *     background_thread              background purge thread enabled
 *     stats.background_thread.num_threads              purge thread count
 *
 *   Thread-cache tuning:
 *     tcache.create / tcache.flush / tcache.destroy    explicit tcache lifecycle
 *     opt.tcache_max / opt.lg_tcache_max               tcache sizing knobs
 *
 *   Heap profiling (prof builds only):
 *     prof.active / prof.dump / prof.reset / prof.interval / prof.gdump
 *
 *   Runtime options (opt.*):
 *     opt.abort / opt.junk / opt.zero / opt.narenas / opt.oversize_threshold
 *
 * @code{.c}
 * char *json = UfsrvJemallocDescribe();
 * if (json) {
 *   syslog(LOG_INFO, "jemalloc: %s", json);
 *   free(json);
 * }
 * @endcode
 *
 * @return A heap-allocated JSON string (the caller must free() it), or NULL
 *         on allocation failure.
 */
BufferDescriptor *UfsrvJemallocDescribe(BufferDescriptor * buffer_provided);
BufferDescriptor *GetMethodInternalsMemoryJemalloc(ScriptHandlerContext *ctx);

#endif /* UFSRV_UTILS_JEMALLOC_H */
