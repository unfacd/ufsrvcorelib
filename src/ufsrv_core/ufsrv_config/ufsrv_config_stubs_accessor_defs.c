/**
  * @file ufsrv_config_stubs_accessor_defs.c
 * @brief User-maintained function definitions of getters / setters that are not covered by generic getters / setters
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>

#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h>

#include <uflib/adt/adt_distinct_array.h>
#include <uflib/collection_descriptor_type.h>

#include <limits.h>
#include <string.h>
#include <syslog.h>

/* Console allow/deny-list entries are client-certificate SPKI SHA-256
 * fingerprints, supplied by the config file as 64-char hex strings. */
#define UFSRV_CONFIG_CONSOLE_ALLOW_LIST_ENTRY_SZ 65   /* 64 hex + NUL */

static void
sDistinctArrayFromArrayValue(DistinctArray *da, const UfsrvConfigValue *value, size_t entry_sz, const char *name)
{
  if (!da) return;

  /* A slot may already be populated (config reload / re-set): free the prior
   * slab, index and hash table first, otherwise the memset below would orphan
   * them.  Safe on a fresh zero-initialised struct — every free is free(NULL). */
  DistinctArrayDestruct(da);
  memset(da, 0, sizeof(*da));

  da->distinct_array_descriptor.block_storage_unit_sz = entry_sz;
  da->distinct_array_descriptor.storage_slot_offset   = offsetof(struct VariableBlock, value);

  /* Entries are NUL-terminated strings of varying length (64-char fingerprints
   * or file paths), so hash/lookup by content via KEY_SIZE_ZERO (strlen/strcmp)
   * rather than a fixed key size — a fixed size (e.g. PATH_MAX for the
   * fileloader) would make _ComputeHash/memcmp read past short entries. */
  if (!HashTableInstantiate(&da->hashTable, HASHTABLE_ITEM_CONTAINER_OFFSET_ZERO,
                            KEY_SIZE_ZERO, HASH_ITEM_NOT_PTR_TYPE, name, HASHTABLE_DEFAULT_EXTRACTOR)) {
    syslog(LOG_ERR, "%s: HashTableInstantiate failed for '%s'", __func__, name);
    return;
  }

  if (!value || !value->stored_value.blob_val || value->collection_sz == 0) return;

  /* Walk the NUL-delimited blob.  Bound the walk by type_allocation_sz (the
   * producer's total byte count), NOT by collection_sz alone: the Lua
   * marshaller counts every array element in collection_sz but only writes the
   * string elements into the blob, so collection_sz can over-count and a blind
   * collection_sz walk would read past the buffer. */
  const char *p   = (const char *)value->stored_value.blob_val;
  const char *end = p + value->type_allocation_sz;
  for (size_t i = 0; i < value->collection_sz && p < end; i++) {
    size_t len = strnlen(p, (size_t)(end - p));
    if (len == (size_t)(end - p)) {
      /* Final entry is not NUL-terminated within the declared blob — stop. */
      break;
    }
    DistinctArrayPut(da, &da->stored_value_idx, (uint8_t **)&da->value_block_storage,
                     (uint8_t *)p, len + 1);
    p += len + 1;
  }
}

static CollectionDescriptor *
sCollectionDescriptorFromArrayValue(const UfsrvConfigValue *value, const char *name);

void
UfsrvConfigSetFileloaderBlackListedFilesRegistry(UfsrvConfigItem *item, UfsrvConfigSource config_source)
{
  UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(UFSRV_CONFIG_FIELD_FILELOADER_BLACK_LISTED_FILES_REGISTRY);

  free(slot->stored_value.blob_val);
  slot->stored_value.blob_val = sCollectionDescriptorFromArrayValue(item ? &item->value : NULL,
                                                                   "FileloaderBlackListedFilesRegistry");
  if (!slot->stored_value.blob_val) {
    slot->stored_value.blob_val = calloc(1, sizeof(CollectionDescriptor));
  }
  slot->source = config_source;
}

/* The registry arrives as a NUL-delimited blob of file paths.  Paths may contain
 * any single-character delimiter, so the collection is assembled directly from
 * the blob instead of tokenising a joined string; consumers traverse it through
 * uflib's CollectionDescriptor accessors (utils_collection_str.h). */
static CollectionDescriptor *
sCollectionDescriptorFromArrayValue(const UfsrvConfigValue *value, const char *name)
{
  const char *blob = value ? (const char *)value->stored_value.blob_val : NULL;
  const char *end  = blob ? blob + value->type_allocation_sz : NULL;
  size_t count = 0, total = 0;

  for (const char *p = blob; p && p < end;) {
    size_t len = strnlen(p, (size_t)(end - p));
    if (len == (size_t)(end - p)) break;
    count++;
    total += len + 1;
    p += len + 1;
  }

  CollectionDescriptor *cd = calloc(1, sizeof(*cd) + count * sizeof(collection_t *) + total);
  if (!cd) {
    syslog(LOG_ERR, "%s: could not allocate collection '%s' (%zu entries)", __func__, name, count);
    return NULL;
  }

  cd->collection_sz          = count;
  cd->collection_base_offset = 0;
  cd->on_destroy_collection  = NULL;
  cd->collection             = count ? (collection_t **)((char *)cd + sizeof(*cd)) : NULL;

  char *str_area = (char *)cd + sizeof(*cd) + count * sizeof(collection_t *);
  size_t idx = 0;
  for (const char *p = blob; p && p < end && idx < count;) {
    size_t len = strnlen(p, (size_t)(end - p));
    if (len == (size_t)(end - p)) break;
    memcpy(str_area, p, len + 1);
    cd->collection[idx++] = str_area;
    str_area += len + 1;
    p += len + 1;
  }

  return cd;
}

void
UfsrvConfigSetFileloaderFilesRegistry(UfsrvConfigItem *item, UfsrvConfigSource config_source)
{
  UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(UFSRV_CONFIG_FIELD_FILELOADER_FILES_REGISTRY);

  free(slot->stored_value.blob_val);
  slot->stored_value.blob_val = sCollectionDescriptorFromArrayValue(item ? &item->value : NULL,
                                                                   "FileloaderFilesRegistry");
  if (!slot->stored_value.blob_val) {
    slot->stored_value.blob_val = calloc(1, sizeof(CollectionDescriptor));
  }
  slot->source = config_source;
}

void
UfsrvConfigSetOauthSettings(UfsrvConfigItem *item, UfsrvConfigSource config_source)
{
  /* TODO: hand-author custom setter for oauth_settings. */
  (void)item;
  (void)config_source;
}

void
UfsrvConfigSetSslCommandConsoleAllowListGod(UfsrvConfigItem *item, UfsrvConfigSource config_source)
{
  UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_GOD);
  sDistinctArrayFromArrayValue((DistinctArray *)slot->stored_value.blob_val, &item->value,
                               UFSRV_CONFIG_CONSOLE_ALLOW_LIST_ENTRY_SZ, "SslCommandConsoleAllowListGod");
  slot->source = config_source;
}

void
UfsrvConfigSetSslCommandConsoleAllowListBrowser(UfsrvConfigItem *item, UfsrvConfigSource config_source)
{
  UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_ALLOW_LIST_BROWSER);
  sDistinctArrayFromArrayValue((DistinctArray *)slot->stored_value.blob_val, &item->value,
                               UFSRV_CONFIG_CONSOLE_ALLOW_LIST_ENTRY_SZ, "SslCommandConsoleAllowListBrowser");
  slot->source = config_source;
}

void
UfsrvConfigSetSslCommandConsoleDenyList(UfsrvConfigItem *item, UfsrvConfigSource config_source)
{
  UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(UFSRV_CONFIG_FIELD_SSL_COMMAND_CONSOLE_DENY_LIST);
  sDistinctArrayFromArrayValue((DistinctArray *)slot->stored_value.blob_val, &item->value,
                               UFSRV_CONFIG_CONSOLE_ALLOW_LIST_ENTRY_SZ, "SslCommandConsoleDenyList");
  slot->source = config_source;
}

PUBLIC_API int *
UfsrvConfigPipefds(void)
{
  return (int *)UfsrvConfigRuntimeValuesInstance( UFSRV_RUNTIME_FIELD_PIPEFDS)->stored_value.blob_val;
}
