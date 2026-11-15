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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "utils_jemalloc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <json-c/json.h>
#include <jemalloc/jemalloc.h>

/* ── mallctl helpers ─────────────────────────────────────────────────────── */

/* Read a build-time feature switch (config.*).  Returns true and stores the
 * value if the key exists in this build; false if the key is absent (the
 * feature was not compiled in and must not be reported as simply "off"). */
static bool
sConfigBool(const char *key, bool *out)
{
  bool value = false;
  size_t sz = sizeof(value);

  if (mallctl(key, &value, &sz, NULL, 0) != 0)  return false;
  if (out)  *out = value;
  return true;
}

/* Read a size_t statistic (stats.* / arenas.*). */
static bool
sStatSize(const char *key, size_t *out)
{
  size_t value = 0;
  size_t sz = sizeof(value);

  if (mallctl(key, &value, &sz, NULL, 0) != 0)  return false;
  if (out)  *out = value;
  return true;
}

/* ── Public: Describe (JSON) ─────────────────────────────────────────────── */

BufferDescriptor *UfsrvJemallocDescribe(BufferDescriptor *buffer_provided)
{
  json_object *root     = json_object_new_object();
  json_object *features = json_object_new_object();
  json_object *on       = json_object_new_object();
  json_object *off      = json_object_new_object();
  json_object *stats    = json_object_new_object();

  if (!root || !features || !on || !off || !stats) {
    json_object_put(root);
    return NULL;
  }

  /* Build-time feature switches: true → "on", false → "off", absent → skip */
  static const char *const kConfigKeys[] = {
    "config.debug",
    "config.stats",
    "config.prof",
    "config.fill",
    "config.utrace",
    "config.xmalloc",
    "config.cache_oblivious",
    "config.lazy_lock",
  };

  for (size_t i = 0; i < sizeof(kConfigKeys) / sizeof(kConfigKeys[0]); i++) {
    bool value = false;

    if (!sConfigBool(kConfigKeys[i], &value))  continue;   /* not in this build */

    json_object *target = value ? on : off;
    /* strip the "config." prefix for a clean key name */
    const char *name = kConfigKeys[i] + strlen("config.");
    json_object_object_add(target, name, json_object_new_boolean(value));
  }

  /* Live statistics — bump the epoch first so every read is recomputed. */
  uint64_t epoch = 1;
  size_t epoch_sz = sizeof(epoch);
  mallctl("epoch", NULL, NULL, &epoch, epoch_sz);

  static const char *const kStatKeys[] = {
    "stats.allocated",
    "stats.active",
    "stats.metadata",
    "stats.resident",
    "stats.mapped",
    "stats.retained",
  };

  for (size_t i = 0; i < sizeof(kStatKeys) / sizeof(kStatKeys[0]); i++) {
    size_t value = 0;

    if (!sStatSize(kStatKeys[i], &value))  continue;

    const char *name = kStatKeys[i] + strlen("stats.");
    json_object_object_add(stats, name, json_object_new_int64((int64_t)value));
  }

  /* Number of arenas (unsigned, not size_t) */
  unsigned narenas = 0;
  size_t narenas_sz = sizeof(narenas);
  if (mallctl("arenas.narenas", &narenas, &narenas_sz, NULL, 0) == 0)
    json_object_object_add(stats, "narenas", json_object_new_int64((int64_t)narenas));

  /* Assemble */
  json_object_object_add(features, "on", on);
  json_object_object_add(features, "off", off);
  json_object_object_add(features, "stats", stats);
  json_object_object_add(root, "features", features);
  json_object_object_add(root, "version", json_object_new_string(JEMALLOC_VERSION));

  const char *flat = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
  BufferDescriptorAppendFormatted(buffer_provided, "%s", flat);

  json_object_put(root);

  return buffer_provided;
}

#include <ufsrvcorelib/ufsrv_console_script/ufsrv_console_script.h>

BufferDescriptor *
GetMethodInternalsMemoryJemalloc(ScriptHandlerContext *ctx)
{
  BufferDescriptor *buffer_provided = ctx ? ctx->buffer : NULL;

  if (!buffer_provided) {
    buffer_provided = calloc(1, sizeof(BufferDescriptor));
    if (!buffer_provided) {
      return NULL;
    }
    BufferDescriptorInit(buffer_provided, 256);
  }

  return UfsrvJemallocDescribe(buffer_provided);
}
