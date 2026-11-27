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
#include <ufsrvcorelib/ufsrv_core/configfile_processor/configfile_processor_symbol_table_type.h>
#include <ufsrvcorelib/ufsrv_core/delegator_sessions/ufsrv_listener_descriptor_type.h>

#include <uflib/adt/adt_distinct_array.h>
#include <uflib/collection_descriptor_type.h>

#include <limits.h>
#include <stdlib.h>
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

static const char *
sListenerString(const ConfFileSymbolTableEntry *entry, char **text)
{
  if (entry->type != UFSRV_CONFIG_VALUE_STR) return NULL;
  size_t size = entry->value.type_allocation_sz;
  const char *value = entry->value.stored_value.str_val;
  if (size < 2 || !value ||
      value[size - 1] != '\0' || memchr(value, '\0', size - 1)) return NULL;
  char *copy = *text;
  memcpy(copy, value, size);
  *text += size;
  return copy;
}

static bool
sListenerFromRow(const ConfFileCollectionRow *row, UfsrvListenerDescriptor *listener, char **text)
{
  for (size_t i = 0; i < row->field_count; ++i) {
    const ConfFileSymbolTableEntry *entry = &row->fields[i];
    if (strcmp(entry->key, "name") == 0) {
      listener->name = sListenerString(entry, text);
    } else if (strcmp(entry->key, "address") == 0) {
      listener->bind_address = sListenerString(entry, text);
    } else if (strcmp(entry->key, "protocol") == 0) {
      listener->protocol_key = sListenerString(entry, text);
    } else if (strcmp(entry->key, "port") == 0) {
      if (entry->type == UFSRV_CONFIG_VALUE_INT) {
        int64_t port = entry->value.stored_value.int_val;
        if (port > 0 && port <= UINT16_MAX) listener->port = (uint16_t)port;
      } else if (entry->type == UFSRV_CONFIG_VALUE_DOUBLE) {
        double port = entry->value.stored_value.double_value;
        if (port >= 1 && port <= UINT16_MAX && port == (uint16_t)port) listener->port = (uint16_t)port;
      }
    }
  }
  return listener->name && listener->bind_address && listener->port && listener->protocol_key;
}

void
UfsrvConfigSetListeners(UfsrvConfigItem *item, UfsrvConfigSource config_source)
{
  const ConfFileCollection *input = item && config_source != UFSRV_CONFIG_SOURCE_DEFAULTS
      ? item->value.stored_value.blob_val : NULL;
  size_t count = input ? input->row_count : 0;
  if (count == 0 && config_source != UFSRV_CONFIG_SOURCE_DEFAULTS) {
    syslog(LOG_ERR, "ufsrv.listeners requires a nonempty collection");
    exit(EXIT_FAILURE);
  }

  size_t text_size = 0;
  for (size_t i = 0; i < count; ++i) {
    const ConfFileCollectionRow *row = &input->rows[i];
    for (size_t j = 0; j < row->field_count; ++j) {
      const ConfFileSymbolTableEntry *entry = &row->fields[j];
      if (entry->type == UFSRV_CONFIG_VALUE_STR) {
        if (entry->value.type_allocation_sz > SIZE_MAX - text_size) goto allocation_error;
        text_size += entry->value.type_allocation_sz;
      }
    }
  }

  const size_t alignment = _Alignof(UfsrvListenerDescriptor);
  if (count > (SIZE_MAX - sizeof(CollectionDescriptor) - (alignment - 1)) / sizeof(collection_t *)) goto allocation_error;
  size_t offset = sizeof(CollectionDescriptor) + count * sizeof(collection_t *);
  offset += (alignment - offset % alignment) % alignment;
  if (text_size > SIZE_MAX - offset || count > (SIZE_MAX - offset - text_size) / sizeof(UfsrvListenerDescriptor)) goto allocation_error;
  size_t size = offset + count * sizeof(UfsrvListenerDescriptor) + text_size;

  CollectionDescriptor *collection = calloc(1, size);
  if (!collection) goto allocation_error;
  collection->collection_sz = count;
  collection->collection = count ? (void *)(collection + 1) : NULL;
  UfsrvListenerDescriptor *listeners = (void *)((char *)collection + offset);
  char *text = (char *)(listeners + count);
  for (size_t i = 0; i < count; ++i) {
    if (!sListenerFromRow(&input->rows[i], &listeners[i], &text)) {
      syslog(LOG_ERR, "ufsrv.listeners[%zu]: invalid listener descriptor", i + 1);
      free(collection);
      exit(EXIT_FAILURE);
    }
    collection->collection[i] = &listeners[i];
  }

  UfsrvConfigValue *slot = UfsrvConfigConfFileValuesInstance(UFSRV_CONFIG_FIELD_LISTENERS);
  free(slot->stored_value.blob_val);
  *slot = (UfsrvConfigValue){
    .stored_value.blob_val = collection,
    .type_allocation_sz = size,
    .collection_sz = count,
    .source = config_source,
  };
  return;

allocation_error:
  syslog(LOG_ERR, "ufsrv.listeners: could not allocate listener descriptors");
  exit(EXIT_FAILURE);
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
