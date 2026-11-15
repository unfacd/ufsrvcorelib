
#ifndef UFSRVCORELIB_FILE_LOADER_SERVICE_DESCRIPTOR_TYPE_H
#define UFSRVCORELIB_FILE_LOADER_SERVICE_DESCRIPTOR_TYPE_H

#include <pthread.h>
#include <uflib/cdt/alias_map/alias_map.h>
#include <uflib/collection_descriptor_type.h>
#include <uflib/file_loader_service_concurrent/file_loader_service_concurrent_type.h>
#include <uflib/logger/logger_type.h>

typedef struct FileLoaderServiceDescriptor {
  int                         is_file_loader_enabled;
  char                        proc_name[PRCTL_MAX_NAME_SZ + 1]; ///< optional loader thread label; empty = "ufFLoader"
  FileLoaderServiceConcurrent file_loader;  ///< The FiloLoader module own configurator (todo there is interface overlap with this Descriptor: consolidate and make module use this)
  pthread_t                   thread_file_loader;
  UfLogger                    *uf_logger;
  const CollectionDescriptor  *files_registry; ///< config-supplied registry documents (may be NULL)
  const CollectionDescriptor  *blacklist_registry; ///< config-supplied paths never served (may be NULL)
  AliasMap                    *alias_map; ///< built from files_registry at start; NULL until then
} FileLoaderServiceDescriptor;

#endif //UFSRVCORELIB_FILE_LOADER_SERVICE_DESCRIPTOR_TYPE_H
