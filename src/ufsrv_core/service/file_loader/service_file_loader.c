/**
 * @file service_file_loader.c
 * @brief service_file_loader
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 28/10/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#include <ufsrvcorelib/ufsrv_core/service/file_loader/service_file_loader.h>

#include <standard_defs.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <uflib/cdt/alias_map/alias_map.h>
#include <uflib/file_loader_service_concurrent/file_loader_concurrent_service.h>
#include <uflib/collection_descriptor_type.h>
#include <uflib/utils_collection_str.h>
#include <ufsrvcorelib/ufsrv_core/service/file_loader/file_loader_service_descriptor_type.h>
#include <uflib/utils_threads.h>

static bool
sPrepareServiceConfig(FileLoaderServiceDescriptor *descriptor, const char ***blacklist_entries_out,
                      uint32_t *                   blacklist_count_out)
{
  FileLoaderServiceConcurrent *config = &descriptor->file_loader;

  memset(config->proc_name, 0, sizeof(config->proc_name));
  if (descriptor->proc_name[0]) {
    strncpy(config->proc_name, descriptor->proc_name, PRCTL_MAX_NAME_SZ);
  }

  const CollectionDescriptor *blacklist = descriptor->blacklist_registry;
  if (!blacklist || blacklist->collection_sz == 0) return true;

  const char **entries = calloc(blacklist->collection_sz, sizeof(*entries));
  if (!entries) {
    syslog(LOG_ERR, "%s: could not marshal %zu blacklist entries", __func__, blacklist->collection_sz);
    return false;
  }

  CollectionStringIterator iterator = CollectionStringIteratorMake(blacklist);
  const char *             path;
  uint32_t                 count = 0;
  while ((path = CollectionStringIteratorNext(&iterator)) && count < blacklist->collection_sz) {
    entries[count++] = path;
  }

  config->service_config_params.blacklist_entries     = entries;
  config->service_config_params.blacklist_entry_count = count;

  *blacklist_entries_out = entries;
  *blacklist_count_out   = count;

  return true;
}

/*!
 * Join the configured registry documents into one JSON array and build the
 * alias map from it.  The map copies every string, so the joined buffer is
 * released before returning.  On failure the descriptor keeps a NULL map and
 * the caller disables the service.
 */
static bool
sBuildAliasMap(FileLoaderServiceDescriptor *descriptor)
{
  const CollectionDescriptor *registry  = descriptor->files_registry;
  size_t                      documents = registry ? registry->collection_sz : 0;
  size_t                      bytes     = 3; /* "[]" + NUL */

  if (documents) {
    CollectionStringIterator iterator = CollectionStringIteratorMake(registry);
    const char *             document;

    while ((document = CollectionStringIteratorNext(&iterator))) bytes += strlen(document) + 1;
  }

  char *json = malloc(bytes);

  if (!json) {
    syslog(LOG_ERR, "%s: could not marshal %zu registry documents", __func__, documents);

    return false;
  }

  char *write = json;

  *write++ = '[';
  *write   = '\0';

  if (documents) {
    CollectionStringIterator iterator = CollectionStringIteratorMake(registry);
    const char *             document;
    bool                     first = true;

    while ((document = CollectionStringIteratorNext(&iterator))) {
      if (!first) *write++ = ',';

      size_t length = strlen(document);

      memcpy(write, document, length);
      write += length;
      first = false;
    }
  }

  *write++ = ']';
  *write   = '\0';

  AliasMapJsonDescriptor json_descriptor = {
      .json_ptr   = json,
      .logger_ptr = descriptor->uf_logger,
  };

  AliasMap *aliases = AliasMapCreateWithJsonDescriptor(&json_descriptor);

  if (!aliases) {
    syslog(LOG_ERR, "%s: alias map rejected: status=%d json_offset=%zu", __func__,
           (int)json_descriptor.result.status, json_descriptor.result.json_offset);
  }

  free(json);

  descriptor->alias_map = aliases;

  return aliases != NULL;
}

/*!
 * Preload every distinct value of the map into the loader.  A snapshot is used
 * rather than AliasMapForEach() because FileLoaderServiceAddFile() performs I/O
 * and the map's read lock must not be held across it.
 */
static uint32_t
sPreloadRegistryValues(FileLoaderService *service_handle, AliasMap *aliases)
{
  if (!service_handle || !aliases) return 0;

  AliasMapSnapshot *snapshot_ptr = AliasMapSnapshotCreate(aliases);

  if (!snapshot_ptr) {
    syslog(LOG_ERR, "%s: could not snapshot the alias map", __func__);

    return 0;
  }

  uint32_t loaded = 0;
  size_t   groups = AliasMapSnapshotGroupCount(snapshot_ptr);

  for (size_t group = 0; group < groups; group++) {
    const char *path = AliasMapSnapshotValueAt(snapshot_ptr, group);

    if (path && FileLoaderServiceAddFile(service_handle, path)) {
      loaded++;
    }
    else {
      syslog(LOG_WARNING, "%s: could not add registry file '%s'", __func__, path ? path : "(null)");
    }
  }

  AliasMapSnapshotDestroy(snapshot_ptr);

  return loaded;
}

void *
ThreadFileLoaderService(FileLoaderServiceDescriptor *file_loader_service_descriptor)
{
  if (IS_EMPTY(file_loader_service_descriptor)) {
    syslog(LOG_ERR, "%s: TERMINATING: NULL data was passed", __func__);
    exit(-1);
  }

  const char **blacklist_entries = NULL;
  uint32_t     blacklist_count   = 0;

  if (!sPrepareServiceConfig(file_loader_service_descriptor, &blacklist_entries, &blacklist_count)) {
    file_loader_service_descriptor->is_file_loader_enabled = 0;

    return NULL;
  }

  FileLoaderServiceConcurrent *config = &file_loader_service_descriptor->file_loader;
  bool initialised = FileLoaderServiceInitWithLogger(config, file_loader_service_descriptor->uf_logger);

  free(blacklist_entries);
  config->service_config_params.blacklist_entries     = NULL;
  config->service_config_params.blacklist_entry_count = 0;

  if (!initialised) {
    syslog(LOG_ERR, "%s: FileLoaderServiceInitWithLogger failed: registry_size=%u loaded_size=%u", __func__,
           config->service_config_params.registry_size, config->service_config_params.loaded_size);
    file_loader_service_descriptor->is_file_loader_enabled = 0;

    return NULL;
  }

  file_loader_service_descriptor->thread_file_loader = FileLoaderServiceGetThread(config->service_handle);

  if (!sBuildAliasMap(file_loader_service_descriptor)) {
    syslog(LOG_ERR, "%s: alias map construction failed; file loader disabled", __func__);

    FileLoaderServiceDestroy(config->service_handle);
    config->service_handle                        = NULL;
    file_loader_service_descriptor->is_file_loader_enabled = 0;

    return NULL;
  }

  uint32_t registry_files =
      sPreloadRegistryValues(config->service_handle, file_loader_service_descriptor->alias_map);

  syslog(
    LOG_INFO,
    "%s: FileLoaderService started: thread=%lu registry_size=%u loaded_size=%u capacity=%u registry_files=%u blacklist_entries=%u",
    __func__, (unsigned long)file_loader_service_descriptor->thread_file_loader,
    (unsigned)config->service_config_params.registry_size, (unsigned)config->service_config_params.loaded_size,
    FileLoaderServiceCapacity(config->service_handle), (unsigned)registry_files, (unsigned)blacklist_count);

  return NULL;
}
