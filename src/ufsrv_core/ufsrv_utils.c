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

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <error.h>
#include "config_defs_ufsrvcorelib.h"
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <curl/curl.h>
#include <ufsrvcorelib/ufsrv_core/system/ufsrv_signals.h>
#include <uflib/recycler/recycler.h>
#include <uflib/adt/adt_queue.h>
#include <uflib/net/utils_net.h>
#include <uflib/net/RFC4193.h>
#include <ufsrv_core/protocol/protocol.h>
#include <ufsrv_core/delegator_sessions/delegator_session_worker_thread.h>
#include "ufsrv_core/ufsrv_utils.h"
#include <ufsrvcorelib/ufsrv_core/command_console/command_console_thread.h>
#include <ufsrvcorelib/ufsrv_core/delegator_sessions/ufsrv_listener_descriptor_type.h>
#include "ufsrv_core/ufsrv_instance_descriptor_type.h"
#include <uflib/collection_descriptor_type.h>
#include <ufsrv_core/cache_backend/persistence.h>
#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend.h>
#include <ufsrv_core/cache_backend/redis.h>
#include "ufsrv_core/cache_backend/server_registry_redis_defs.h"
#include <ufsrv_core/service/timer/delegator_timer_thread.h>
#include <uflib/adt/adt_minheap.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include <ufsrv_core/system/log_message_literals.h>

#include <hiredis/hiredis.h>
#include <scheduled_jobs/scheduled_jobs.h>
#include <scheduled_jobs/scheduled_jobs_type.h>
#include <systemd/sd-daemon.h>

#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>
#include <ufsrv_core/ratelimit_v2/ratelimit.h>

#include "ufsrv_core/delegator_sessions/thread_context_type.h"
#include "ufsrv_core/service/file_loader/service_file_loader.h"
#include "ufsrv_core/session/sessions_utils.h"
#include "ufsrv_core/session/sesstion_stat_flags.h"

#define EXTRA_KEEPALIVE_OPTIONS_UNSPECIFIED 0
#define EXTRA_KEEPALIVE_OPTIONS_VALID(x) ((x) > 0 && (x) != EXTRA_KEEPALIVE_OPTIONS_UNSPECIFIED)

///> convenient access macros to the delegator <-> main listener ipx pipe
#define PIPE_READ_END UfsrvConfigPipefds()[0]//masterptr->pipefds[0]
#define PIPE_WRITE_END UfsrvConfigPipefds()[1]//masterptr->pipefds[1]
#define WORK_DELEGATOR_PIPE UfsrvConfigWorkDelegatorPipe()
#define WORK_DELEGATOR_PIPE_SESSION ((UfsrvSession *)GetInstance(WORK_DELEGATOR_PIPE))
#define WORK_DELEGATOR_PIPE_WRITE_END(x) (x)->ssptr->sock //fd[1]
#define WORK_DELEGATOR_PIPE_READ_END(x) (x)->dsptr->sock	//fd[0]

extern __thread ThreadContext ufsrv_thread_context;

/* Server registry defaults used by instance selection. */
#define CONFIF_MAX_UFSRV_INSTANCE_PER_GEOGROUP 512
#define _CONFIGDEFAULT_DEFAULT_UFSRVGEOGROUP 3
#define _CONFIGDEDAULT_IDLE_TIME_INTERVAL_INTRA_REQUEST 65 ///< Seconds since last server activity.

/* Listener-to-delegator queue. Its backing array is private to this file. */
#define _CONFIG_LISTENER_CONNECTION_QUEUE_SZ 1024
static QueueClientData *listener_connection_queue[_CONFIG_LISTENER_CONNECTION_QUEUE_SZ];

static void sInitNewConnectionsQueue(void);
static void _InitConnectionListenerToWorkDelegatorPipe(int *_Nonnull              pipe_fd_set,
                                                       InstanceHolderForSession **sesn_ptr_ipc);
static void UFSRVThreadsOnceInitialiser(void);

static void sDestroyConnectionRateLimitHandle(RateLimitHandle *handle);

//This is the classic self-pipe-trick
//backdoor into the WorkDelegatorThread to unblock it from epoll_wait call to process new incoming connection
//uni-directional pipe source is writer end in main listening thread fd[1]
//reader end is destination socket in WorkDelegatorThread fd[0]
//flow source:fd[1] -> destination:fd[0]
/**
 *
 * @param pipe_fd_set pre-allocated array[2] to hold file descriptors created by pipe2()
 */
static void
_InitConnectionListenerToWorkDelegatorPipe(int *_Nonnull pipe_fd_set, InstanceHolderForSession **sesn_ptr_ipc)
{
  Socket * ss_ptr, *ds_ptr = NULL;
  UfsrvSession *sesn_ptr        = NULL;
  if ((pipe2(pipe_fd_set, O_NONBLOCK)) == -1) {
    syslog(LOG_ERR, "%s: COULD NOT initialise Pipe(errno:%d): exiting...", __func__, errno);
    exit(-1);
  }

  ss_ptr = malloc((sizeof(Socket))); //writer end in main listening  thread
  memset(ss_ptr, 0, sizeof(Socket));

  ss_ptr->type = SOCK_PIPEWRITER;
  ss_ptr->sock = PIPE_WRITE_END;
  strcpy(ss_ptr->address, "pipe.writer.localhost");
  strcpy(ss_ptr->haddress, "pipe.reader.localhost");

  ds_ptr = malloc(sizeof(Socket)); //reader end in WorkDelegator thread
  memset(ds_ptr, 0, sizeof(Socket));
  ds_ptr->type = SOCK_PIPEREADER;
  ds_ptr->sock = PIPE_READ_END;
  strcpy(ds_ptr->address, "pipe.reader.localhost");
  strcpy(ds_ptr->haddress, "pipe.writer.localhost");

  if (!(sesn_ptr = InstantiateUfsrvSession(ss_ptr, ds_ptr, 0, -1))) {
    close(ss_ptr->sock);
    close(ds_ptr->sock);
    free(ss_ptr);
    free(ds_ptr);

    syslog(LOG_ERR, "%s: COULD NOT initialise Pipe Interconnection Session: exiting...", __func__);
    _exit(-1);
  }

  //handy access. Even though not managed out off recycler, for consistency we envelope it with InstanceHolder
  InstanceHolderForSession *instance_sesn_ptr = calloc(1, sizeof(InstanceHolderForSession));
  SetInstance(instance_sesn_ptr, sesn_ptr);
  // WORK_DELEGATOR_PIPE = instance_sesn_ptr; replaced below
  *sesn_ptr_ipc = instance_sesn_ptr;

  syslog(
    LOG_INFO,
    "%s: Successfully initialised Pipe Interconnection Session (cid:'%lu') conection: WRITER: '%s:%d' READER: '%s:%d'",
    __func__, sesn_ptr->session_id, sesn_ptr->ssptr->address, sesn_ptr->ssptr->sock, sesn_ptr->dsptr->address,
    sesn_ptr->dsptr->sock);
}

/**
 * @brief Convenient result object predefined with success value. Do not reassign values.
 */

UFSRVResult *const ProvideSuccessResult()
{
  static UFSRVResult success_result = {.result_type = RESULT_TYPE_SUCCESS, .result_code = RECODE_NONE};

  return &success_result;
}

SessionsDelegator *const GetUfsrvSessionsDelegator()
{
  // return master.sessions_delegator;
  return UfsrvConfigSessionsDelegator();
}

/**
 * @brief Connect the initialised delegator with main ufsrv
 * @param sessions_delegator pre-allocated and initialised delegator object
 */
void
RegisterUfsrvSessionsDelegator(SessionsDelegator *const sessions_delegator)
{
  if (IS_PRESENT(sessions_delegator)) {
    // master.sessions_delegator = sessions_delegator;
    UfsrvConfigSetSessionsDelegator(&(UfsrvConfigItem){.value.stored_value.ptr_val = sessions_delegator},
                                    UFSRV_CONFIG_SOURCE_RUNTIME);
  }
  else {
    syslog(LOG_ERR, "%s: ERROR: NO SESSIONS DELEGATOR PROVIDED: EXITING", __func__);
    exit(-1);
  }
}

/**
* 	@brief: This SPSC queue is shared between the accept loop (producer) and ThreadWorkerDelegator (consumer).
* 	Ensure instrumentation is already initialised in order to correctly report on queue capacity.
*/
static void
sInitNewConnectionsQueue(void)
{
  syslog(LOG_INFO, ">> %s: New Connections Queue: Initialising lockless queue...", __func__);

  InitNewConnectionsQueue(listener_connection_queue, _CONFIG_LISTENER_CONNECTION_QUEUE_SZ);

  statsd_gauge(UfsrvConfigInstrumentationBackend(), "listener.connection.queue_capacity",
               _CONFIG_LISTENER_CONNECTION_QUEUE_SZ, UfsrvConfigInstrumentationBackendServerAsPtr());
}

//>>>>>>>>>> SSL thread locking semantics callbacks
static unsigned long thread_id_function();

static unsigned long thread_id_function(void)
{
  return ((unsigned long)pthread_self());
}

static void locking_function(int mode, int id, const char *file, int line);

static void locking_function(int mode, int id, const char *file, int line)
{
  pthread_mutex_t *ssl_mutexes = UfsrvConfigSslMutexes();
  if (mode & CRYPTO_LOCK) pthread_mutex_lock(&ssl_mutexes[id]);
  else pthread_mutex_unlock(&ssl_mutexes[id]);
}

//dynamic
struct CRYPTO_dynlock_value
{
  pthread_mutex_t mutex;
};

static struct CRYPTO_dynlock_value *dyn_create_func(const char *file, int line);

static struct CRYPTO_dynlock_value *
dyn_create_func(const char *file, int line)
{
  struct CRYPTO_dynlock_value *value;
  value = (struct CRYPTO_dynlock_value*)malloc(sizeof(struct CRYPTO_dynlock_value));
  pthread_mutex_init(&value->mutex, NULL);
  return value;
}

static void
dyn_destroy_func(struct CRYPTO_dynlock_value *l, const char *file, int line)
{
  pthread_mutex_destroy(&l->mutex);
  free(l);
}

static void dyn_lock_func(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line);

static void dyn_lock_func(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line)
{
  if (mode & CRYPTO_LOCK) pthread_mutex_lock(&l->mutex);
  else pthread_mutex_unlock(&l->mutex);
}

//>>>>>>>>>>>>>>>>>>>>>>>> end of SSL thread locking semantics callbacks


//http://www.informit.com/authors/bio/1933efae-b8cc-4f72-aebd-a4b7c7f761c2
//http://www.cse.cuhk.edu.hk/~pclee/tsinghua/files/lec3.pdf

//Permissive verify callback for the console's mutual-TLS handshake: accept any
//client certificate (return 1) so the chain is not validated against a CA. Access
//is then decided by the SPKI-hash allow-list check in AnswerCommandConsoleRequest.
static int
sSslConsoleVerifyCallback(int preverify_ok, X509_STORE_CTX *ctx)
{
  (void)preverify_ok;
  (void)ctx;
  return 1;
}

void
InitSSL(void)
{
  static bool is_crypto_initialised = false;
  if (!is_crypto_initialised) { //isUfsrvConfigCryptoInitialised() == 0) {
    const SSL_METHOD *ssl_method_console, *ssl_method_client;

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    ssl_method_console = TLS_server_method();
    SSL_CTX *ssl_ctx   = SSL_CTX_new(ssl_method_console);
    if (ssl_ctx == NULL) {
      syslog(LOG_INFO, "%s: ERROR: COULD NOT INITIALISE SSL CTX for CONSOLE connections...", __func__);
      exit(-1);
    }

    UfsrvConfigSetSslCtx(&(UfsrvConfigItem){.value = {.stored_value.ptr_val = ssl_ctx}}, UFSRV_CONFIG_SOURCE_RUNTIME);

    SSL *ssl_console = SSL_new(UfsrvConfigSslCtx());
    if (ssl_console == NULL) {
      syslog(LOG_INFO, "%s: ERROR: COULD NOT OBTAIN SSL object for CONSOLE CTX...", __func__);
      exit(-1);
    }

    UfsrvConfigSetSslCommandConsole(&(UfsrvConfigItem){.value = {.stored_value.ptr_val = ssl_console}}, UFSRV_CONFIG_SOURCE_RUNTIME);

    ssl_method_client     = TLS_server_method();
    SSL_CTX *ssl_user_ctx = SSL_CTX_new(ssl_method_client);
    if (ssl_user_ctx == NULL) {
      syslog(LOG_INFO, "%s: ERROR: COULD NOT INITIALISE SSL CTX for CLIENT connections...", __func__);
      exit(-1);
    }

    UfsrvConfigSetSslUserCtx(&(UfsrvConfigItem){.value = {.stored_value.ptr_val = ssl_user_ctx}},
                             UFSRV_CONFIG_SOURCE_RUNTIME);

    bool is_command_console_enabled = UfsrvConfigSslCommandConsoleIsEnabled();
    if (is_command_console_enabled) {
      // char *lx= LUA_GetFieldToString("location");
      char *c = UfsrvConfigSslCommandConsoleCertificateFile();
      char *k = UfsrvConfigSslCommandConsoleKeyFile();
      syslog(LOG_DEBUG, "%s: ufsrv_ssl location='%s' key='%s' certificate='%s'...", __func__, "_not used_", k, c);

      char *file_path;
      asprintf(&file_path, "%s/etc/%s", UfsrvConfigConfigDir(), c);
      free(c);
      if (SSL_CTX_use_certificate_file(UfsrvConfigSslCtx(), file_path, SSL_FILETYPE_PEM) != 1) {
        syslog(LOG_ERR, "%s: ERROR: COULD NOT LOAD CONSOLE CERTIFICATE '%s': %s", __func__, file_path,
               ERR_error_string(ERR_get_error(), NULL));
        exit(-1);
      }
      free(file_path);

      asprintf(&file_path, "%s/etc/%s", UfsrvConfigConfigDir(), k);
      free(k);
      if (SSL_CTX_use_PrivateKey_file(UfsrvConfigSslCtx(), file_path, SSL_FILETYPE_PEM) != 1) {
        syslog(LOG_ERR, "%s: ERROR: COULD NOT LOAD CONSOLE PRIVATE KEY '%s': %s", __func__, file_path,
               ERR_error_string(ERR_get_error(), NULL));
        exit(-1);
      }
      free(file_path);

      if (!(SSL_CTX_check_private_key(UfsrvConfigSslCtx()))) {
        syslog(LOG_ERR, "%s: ERROR: CONSOLE CERTIFICATE/PRIVATE KEY MISMATCH: %s", __func__,
               ERR_error_string(ERR_get_error(), NULL));
        exit(-1);
      }

      if (UfsrvConfigSslConsoleIsClientCertificateRequired()) {
        //mutual TLS: require a client certificate; the chain is NOT verified (the SPKI hash is pinned in AnswerCommandConsoleRequest)
        //NOTE: self-signed client certs require a dedicated CA (schema) field — follow-up.
        SSL_CTX_set_verify(UfsrvConfigSslCtx(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                           sSslConsoleVerifyCallback);
        if (SSL_CTX_set_default_verify_paths(UfsrvConfigSslCtx()) != 1) {
          syslog(
            LOG_WARNING, "%s: WARNING: COULD NOT LOAD DEFAULT VERIFY PATHS — client certificate verification may fail",
            __func__);
        }
      }

      bool is_command_console_client_enabled = UfsrvConfigSslCommandConsoleClientIsEnabled();
      if (is_command_console_client_enabled) {
        // char *l = LUA_GetFieldToString("location");
        char *c = UfsrvConfigSslCommandConsoleClientCertificateFile();
        char *k = UfsrvConfigSslCommandConsoleClientKeyFile();
        syslog(LOG_DEBUG, "%s: ufsrv_user_ssl location='%s' key='%s' certificate='%s'...", __func__, "_not used_", k,
               c);

        char *file_path;
        asprintf(&file_path, "%s/etc/%s", UfsrvConfigConfigDir(), c);
        free(c);
        SSL_CTX_use_certificate_file(UfsrvConfigSslUserCtx(), file_path, SSL_FILETYPE_PEM);
        free(file_path);

        asprintf(&file_path, "%s/etc/%s", UfsrvConfigConfigDir(), k);
        free(k);
        SSL_CTX_use_PrivateKey_file(UfsrvConfigSslUserCtx(), file_path, SSL_FILETYPE_PEM);
        free(file_path);
        // free(l);

        if (!(SSL_CTX_check_private_key(UfsrvConfigSslUserCtx()))) {
          syslog(LOG_INFO, "%s: ERROR: COULD NOT CHECK PRIVATE KEY FOR USER CTX...", __func__);
          //				exit(-1);
        }
      }
    }
    else {
      syslog(
        LOG_INFO,
        "%s: Command Console SSL semantics not supported: UfsrvConfigSslCommandConsoleIsEnabled() returned false",
        __func__);
    }

    { //static allocation of SSL threading semantics
      int              i;
      pthread_mutex_t *ssl_mutexes = (pthread_mutex_t*)malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
      UfsrvConfigSetSslMutexes(&(UfsrvConfigItem){.value = {.stored_value.ptr_val = ssl_mutexes}},
                               UFSRV_CONFIG_SOURCE_RUNTIME);
      for (i = 0; i < CRYPTO_num_locks(); i++) {
        pthread_mutex_init(&ssl_mutexes[i], NULL);
      }

      //static calbacks
      CRYPTO_set_id_callback(thread_id_function);
      CRYPTO_set_locking_callback(locking_function);

      //dynamic callbacks
      CRYPTO_set_dynlock_create_callback(dyn_create_func);
      CRYPTO_set_dynlock_lock_callback(dyn_lock_func);
      CRYPTO_set_dynlock_destroy_callback(dyn_destroy_func);
    }

    is_crypto_initialised = true;
    // UfsrvConfigSetCryptoInitialised(&(UfsrvConfigItem){.value = {.stored_value.int_val = 1}}, UFSRV_CONFIG_SOURCE_RUNTIME); //todo move out of netcorelib atalogue

    syslog(LOG_INFO, "%s: SUCCESS: SSL subsystem initialised.", __func__);
  }
  else {
    syslog(LOG_INFO, "%s: SSL subsystem is already initialised", __func__);
  }
}

__pure ScheduledJobs *
GetScheduledJobsStore(void)
{
  static ScheduledJobs scheduled_jobs_store;
  return &scheduled_jobs_store;
}

void
InitUfsrvScheduledJobsStore(ScheduledJobs *scheduled_jobs, size_t count, void (*scheduled_jobs_startup_callback)(void))
{
  InitScheduledJobsStore(scheduled_jobs, count);
  if (IS_PRESENT(scheduled_jobs_startup_callback)) {
    scheduled_jobs_startup_callback();
  }
  syslog(
    LOG_INFO, "%s (pid:'%lu', o:'%p', page_sz:'%d', entries_per_page:'%d'): SUCCESS: Initialised ScheduledJobsStore",
    __func__, pthread_self(), scheduled_jobs, MinHeapPageSize(), MinHeapEntriesPerPage());
}

/**
 * @brief Launch the TimerManager thread that fires off timed jobs.This also initialises the scheduled jobs store.
 */
void
LaunchTimerManagerThread(void (*scheduled_jobs_startup_callback)(void))
{
  InitUfsrvScheduledJobsStore(GetScheduledJobsStore(), 0, scheduled_jobs_startup_callback);

  int result = pthread_create(UfsrvConfigThreadHandleTimerDelegatorAsPtr(), NULL, ThreadTimerManager, (void*)GetScheduledJobsStore());
  if (result != 0) {
    char error_str[MEDIUMBUF] = {0};
    strerror_r(errno, error_str, MEDIUMBUF);
    syslog(
      LOG_ERR,
      "InitialiseWorkerDelegator: TERMINATING (errno: '%d'): COULD NOT INITIALISE Timer Manager(TM) thread: '%s'...",
      errno, error_str);

    exit(-1);
  }

  syslog(LOG_INFO, ">> %s: SUCCESSFULLY Initialised TimerManager...'", __func__);
}

void
LaunchFileloaderServiceThread(FileLoaderServiceDescriptor *descriptor)
{
  if (IS_EMPTY(descriptor)) return;
  if (!descriptor->is_file_loader_enabled) return;

  ThreadFileLoaderService(descriptor);
}

bool
UfsrvListenerForEach(const CollectionDescriptor *listeners,
                     bool (^visit)(UfsrvListenerDescriptor *listener, size_t index))
{
  if (!listeners || !listeners->collection || !listeners->collection_sz || !visit) return false;

  for (size_t i = 0; i < listeners->collection_sz; ++i) {
    UfsrvListenerDescriptor *listener = listeners->collection[i];
    if (!listener || !visit(listener, i)) return false;
  }
  return true;
}

#include <ufsrvcorelib/ufsrv_core/system/utils_cpu.h>

static int sAcceptListenerConnection(const UfsrvListenerDescriptor *listener, Socket *socket,
                                     int interval, RateLimitHandle *rate_limit);

int
UfsrvMainListenerStart(const CollectionDescriptor *listeners, Socket *const *sockets, Socket *console)
{
  int result = -1;
  int signal_fd = -1;
  size_t count = listeners ? listeners->collection_sz : 0;
  struct pollfd *fds = NULL;
  RateLimitHandle *rate_limit = &SessionsDelegatorPtr()->listener_rate_limit;

  if (!count || !listeners->collection || !sockets || count > SIZE_MAX - 2) goto done;
  fds = calloc(count + 2, sizeof(*fds));
  if (!fds) goto done;

  for (size_t i = 0; i < count; ++i) {
    const UfsrvListenerDescriptor *listener = listeners->collection[i];
    if (!listener || !sockets[i] || sockets[i]->sock < 0) goto done;
    if (!listener->protocol || !listener->protocol->session_pool) {
      syslog(LOG_ERR, "%s: no protocol session pool for listener %zu", __func__, i + 1);
      goto done;
    }
    fds[i + 2] = (struct pollfd){.fd = sockets[i]->sock, .events = POLLIN};
  }

  UfsrvCpuAffinityPin(UFSRV_THREAD_LISTENER, 0);
  signal_fd = signalfd(-1, UfsrvSignalControlSet(), SFD_NONBLOCK | SFD_CLOEXEC);
  if (signal_fd < 0) {
    syslog(LOG_ERR, "%s: could not create signal fd: %s", __func__, strerror(errno));
    goto done;
  }
  fds[0] = (struct pollfd){.fd = signal_fd, .events = POLLIN};
  fds[1] = (struct pollfd){.fd = console ? console->sock : -1, .events = POLLIN};

  sd_notifyf(0, "READY=1\nSTATUS=Main Listener Loop ready...");
  for (;;) {
    if (poll(fds, count + 2, -1) < 0) {
      if (errno == EINTR) continue;
      syslog(LOG_ERR, "%s: poll failed: %s", __func__, strerror(errno));
      break;
    }

    if (fds[0].revents & POLLIN) {
      struct signalfd_siginfo signal;
      ssize_t bytes = read(signal_fd, &signal, sizeof(signal));
      if (bytes == sizeof(signal)) {
        if (signal.ssi_signo == SIGINT || signal.ssi_signo == SIGTERM) {
          syslog(LOG_NOTICE, "%s: received signal %u, shutting down", __func__, signal.ssi_signo);
          result = 0;
          break;
        }
        if (signal.ssi_signo == SIGHUP) {
          syslog(LOG_NOTICE, "%s: received SIGHUP, configuration reload not implemented", __func__);
        }
      } else if (bytes < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
        syslog(LOG_ERR, "%s: could not read signal fd: %s", __func__, strerror(errno));
        break;
      }
    }

    for (size_t i = 0; i < count + 2; ++i) {
      if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        syslog(LOG_ERR, "%s: poll reported an error for fd %d (events %#x)", __func__, fds[i].fd, (unsigned)fds[i].revents);
        goto done;
      }
    }

    for (size_t i = 0; i < count; ++i) {
      if (fds[i + 2].revents & POLLIN) {
        sAcceptListenerConnection(listeners->collection[i], sockets[i], EXTRA_KEEPALIVE_OPTIONS_UNSPECIFIED, rate_limit);
      }
    }
    if (fds[1].revents & POLLIN) AnswerCommandConsoleRequest(console);
  }

done:
  if (sockets) {
    for (size_t i = 0; i < count; ++i) {
      if (sockets[i] && sockets[i]->sock >= 0) {
        close(sockets[i]->sock);
        sockets[i]->sock = -1;
      }
    }
  }
  if (console && console->sock >= 0) {
    close(console->sock);
    console->sock = -1;
  }
  if (signal_fd >= 0) close(signal_fd);
  free(fds);
  sDestroyConnectionRateLimitHandle(rate_limit);
  UfsrvInitiateShutdown();
  return result;
}

void
InitHTTPClient(void)
{
  syslog(LOG_INFO, "Initialisaing HTTPClient subsystem...");

  curl_global_init(CURL_GLOBAL_ALL);
}

/**
 * @brief Initialiser for thread keys used to hold common and essential connection handles for various backends and data stores.
 * Each thread will load its unique instance of each handle, using same keyname.
 */
static void
UFSRVThreadsOnceInitialiser(void)
{
  syslog(LOG_INFO, "%s: Performing pthread ONCE initialisation...", __func__);
  //NOOP
}

/**
 * @brief This is a common bootstrapping sequence for ufsrvwebsock and ufsrvrest type servers. typically, called from within
 * protocol initiation callback.
 */
void
InitUFSRV(SessionsDelegator *sd_ptr, SessionsDelegatorInitSpecs *spec)
{
  RegisterUfsrvSessionsDelegator(sd_ptr);

  UfsrvConfigSetThreadsSubsystemUfsrvOnce(&(UfsrvConfigItem){.value = {.stored_value.blob_val = PTHREAD_ONCE_INIT}},
                                          UFSRV_CONFIG_SOURCE_RUNTIME);
  pthread_once(UfsrvConfigThreadsSubsystemUfsrvOnceAsPtr(), UFSRVThreadsOnceInitialiser);

  if (spec && spec->on_init_crypto) spec->on_init_crypto();
  InitSSL();

  InstrumentationBackendServerInit(UfsrvConfigStatsBackendAddress(), UfsrvConfigStatsBackendPort(),
                                   UfsrvConfigInstrumentationBackendServerAsPtr());

  InstrumentationBackend *instrumentation_backend = InstrumentationBackendInit(NULL, NULL, NULL, 0);
  if (IS_PRESENT(instrumentation_backend)) {
    UfsrvConfigSetInstrumentationBackend(&(UfsrvConfigItem){.value = {.stored_value.ptr_val = instrumentation_backend}},
                                         UFSRV_CONFIG_SOURCE_RUNTIME);
    syslog(LOG_DEBUG, "%s: SUCCESS (instr_ptr:'%p'): Initialised Instrumentation Backend for Main Thread: '%lu'...",
           __func__, instrumentation_backend, pthread_self());
  }
  else syslog(LOG_DEBUG, "%s: ERROR: COULD NOT Initialise Instrumentation Backend for Main Thread: '%lu'...", __func__,
              pthread_self());

  PersistenceBackend *main_persistence = NULL;
  if (spec && spec->on_init_main_thread_backends)
    main_persistence = spec->on_init_main_thread_backends(&sd_ptr->listener_rate_limit);
  if (!main_persistence) {
    syslog(LOG_ERR, "%s: Could not initialise main-thread backends", __func__);
    _exit(-1);
  }

  _InitConnectionListenerToWorkDelegatorPipe(UfsrvConfigPipefds(),
                                             UfsrvConfigWorkDelegatorPipeAsPtr() /*&master.work_delegator_pipe*/);
  sd_ptr->listener_delegator_ipc = UfsrvConfigWorkDelegatorPipe(); // IMPORTANT connection for the time being

  sInitNewConnectionsQueue();

  if (spec != NULL && spec->on_init_hashtable != NULL) spec->on_init_hashtable();

  if (UfsrvConfigRegisterUfsrverInstance(main_persistence)) {
    syslog(LOG_INFO, "%s: SUCCESS: Registered with Configuration Server", __func__);
#if 0
    Session sesn = {0}; int ufsrv_group[10] = {0}; sesn.persistence_backend = masterptr->persistence_backend; sesn.
      geogroup   = 3; UfsrvConfigGetGeoGroup(&sesn, &((CollectionDescriptor){(collection_t**)ufsrv_group, 0}));
#endif
  }
  else {
    syslog(LOG_ERR, "%s: CRITICAL ERROR: COULD NOT REGISTER WITH CONFIGURATION SERVER: EXITING...", __func__);
    _exit(-1);
  }
} /**/

static bool
_IsRateLimitExceededForNewConnection(RateLimitHandle *handle, struct sockaddr_in *addr);

static void
sWriteRateLimitedResponseAndClose(int nsocket, UFSRVResult *result_ptr);

static int
sAcceptListenerConnection(const UfsrvListenerDescriptor *listener, Socket *s_ptr_listening, int interval, RateLimitHandle *rl_handle)
{
  const Protocol *protocol = listener->protocol;
  struct sockaddr_in hisaddr;
  socklen_t sin_size = sizeof(hisaddr);
  int nsocket = accept(s_ptr_listening->sock, (struct sockaddr *)&hisaddr, &sin_size);
  if (nsocket < 0) {
    int error = errno;
    if (error != EWOULDBLOCK && error != EAGAIN && error != EINTR) {
      syslog(LOG_ERR, LOGSTR_MAINLISTENER_ACCEPT_ERROR, __func__, (int)listener->port,
             error, strerror(error), LOGCODE_MAINLISTENER_ACCEPT_ERROR);
    }
    return 0;
  }

  if (_IsRateLimitExceededForNewConnection(rl_handle, &hisaddr)) {
    UFSRVResult *result = NULL;
    if (protocol->protocol_callbacks.on_ratelimited_callback) {
      result = protocol->protocol_callbacks.on_ratelimited_callback(NULL, 0, &hisaddr);
    }
    sWriteRateLimitedResponseAndClose(nsocket, result);
    return 0;
  }

  int opt = 1;
  int val = 1;
  setsockopt(nsocket, SOL_SOCKET, SO_KEEPALIVE, (void*)&opt, sizeof(int));
  if (EXTRA_KEEPALIVE_OPTIONS_VALID(interval)) {
    ConfigureSocketKeepAliveOptions(nsocket, interval);
  }
  SetSocketFlags(nsocket, 1, O_NONBLOCK);

  //up-to this point we have a fully connected socket

  InstanceHolderForSession *instance_sesn_ptr = UfsrvSessionCheckout(protocol, CALL_FLAG_HASH_SESSION_LOCALLY);
  if (IS_EMPTY(instance_sesn_ptr)) {
    close(nsocket);
    return 0;
  }

  UfsrvSession *sesn_ptr = GetInstance(instance_sesn_ptr);
  sesn_ptr->ssptr->sock = nsocket;
  inet_ntop(AF_INET, &hisaddr.sin_addr, sesn_ptr->ssptr->haddress, sizeof(sesn_ptr->ssptr->haddress));
  snprintf(sesn_ptr->ssptr->address, sizeof(sesn_ptr->ssptr->address), "%s", listener->bind_address);
  sesn_ptr->ssptr->hport = ntohs(hisaddr.sin_port);
  sesn_ptr->ssptr->port = listener->port;

  sesn_ptr->stat |= SESNSTATUS_CONNECTED;
  sesn_ptr->when_serviced_start = time(NULL); //corresponding endtime recorded  in ThreadWorkerDelegator()
  if (!SessionDelegatorPushIncomingConnection(instance_sesn_ptr)) {
    syslog( LOG_DEBUG, "%s (cid:'%lu', o:'%p', fd'%d', queue_sz:'%lu'): ERROR: COULD NOT ENQUEUE NEW CONNECTION: DROPPING...",
      __func__, (sesn_ptr)->session_id, sesn_ptr, sesn_ptr->ssptr->sock, SessionDelegatorIncomingConnectionsQueueSize());
    goto return_error;
  }

  //DON NOT MUTATE SESSION STATE AFTER THIS POINT IT: YYOU MIGHT BE MUTATING A LOCKED SESSION (ThreadWebSockets)

  statsd_gauge_dec(UfsrvConfigInstrumentationBackend(), "listener.connection.queue_capacity", 1, UfsrvConfigInstrumentationBackendServerAsPtr());

  //todo this is uses old locking queue code: port to lockless
#ifdef __UF_FULLDEBUG
  // syslog (LOG_DEBUG, LOGSTR_MAINLISTENER_ACCEPT_QUEUED,  __func__, (sesnptr)->session_id, sesnptr, (sesnptr)->ssptr->sock, QUEUE_ENTRIES_COUNT(new_connections_queue_ptr), LOGCODE_MAINLISTENER_ACCEPT_QUEUED);
#endif

#define PIPE_GO_MSG "G"
  const char *marshal_msg         = PIPE_GO_MSG;
  ssize_t     actual_written_size = 0;

  //Signal to delegator via event loop that queue contains an entry for fetching
  UfsrvSession *sesn_ptr_pipe = WORK_DELEGATOR_PIPE_SESSION;
  while (actual_written_size < (sizeof(PIPE_GO_MSG) - 1)) {
    ssize_t written = write(WORK_DELEGATOR_PIPE_WRITE_END(sesn_ptr_pipe), marshal_msg + actual_written_size, (sizeof(PIPE_GO_MSG) - 1) - actual_written_size);
    if (written < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        syslog(LOG_ERR, LOGSTR_MAINLISTENER_PIPE_WRITE_BLOCKING, __func__, errno, LOGCODE_MAINLISTENER_PIPE_WRITE_BLOCKING);
      } else {
        syslog(LOG_ERR, LOGSTR_MAINLISTENER_PIPE_WRITE_ERROR, __func__, errno, LOGCODE_MAINLISTENER_PIPE_WRITE_ERROR);
      }

      return nsocket;
    }

    if (written == 0) {
      syslog(LOG_ERR, "%s {pid:'%lu'}: !ERROR: NEW CONNECTIONS PIPE: WRITE MADE NO PROGRESS", __func__, pthread_self());
      return nsocket;
    }

    //nonerror
    actual_written_size += written;
  } //write loop

#ifdef __UF_FULLDEBUG
  if (actual_written_size == (sizeof(PIPE_GO_MSG) - 1)) syslog(
    LOG_DEBUG, "%s: SUCCESSFULLY written to 'New Connections Pipe': cid='%lu'", __func__, (sesnptr)->session_id);
#endif

  return nsocket;

return_error:
  close(nsocket);
  sesn_ptr->ssptr->sock = -1;
  UfsrvSessionReturnToRecycler(instance_sesn_ptr, NULL, CALL_FLAG_HASH_SESSION_LOCALLY);

  return 0;
}

#include <uflib/adt/adt_distinct_array.h>

static void
sDestroyConnectionRateLimitHandle(RateLimitHandle *handle)
{
  if (handle == NULL) return;
  RateLimitBackendDestroy(handle->backend);
  handle->backend = NULL;
}

static bool
_IsRateLimitExceededForNewConnection(RateLimitHandle *handle, struct sockaddr_in *addr)
{
  char ipstr[INET6_ADDRSTRLEN] = {0};

  if (!UfsrvConfigRatelimitIsEnabled()) return false;

  if (((struct sockaddr_storage*)addr)->ss_family == AF_INET) {
    struct sockaddr_in *s = (struct sockaddr_in*)addr;
    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
  }
  else {
    struct sockaddr_in6 *s = (struct sockaddr_in6*)addr;
    inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
  }

  if (handle != NULL && handle->app_policy != NULL && handle->app_policy->is_bypass_private_ip) {
    bool is_private = false;
    bool classified;

    if (((struct sockaddr_storage*)addr)->ss_family == AF_INET) {
      classified = IsIpAddressPrivate(IP_FAMILY_IPV4, IP_INPUT_BINARY,
                                      &((struct sockaddr_in*)addr)->sin_addr, &is_private);
    } else {
      classified = IsIpAddressPrivate(IP_FAMILY_IPV6, IP_INPUT_BINARY,
                                      &((struct sockaddr_in6*)addr)->sin6_addr, &is_private);
    }

    if (classified && is_private) {
      syslog(LOG_DEBUG, "%s {pid:'%lu', ip:'%s'}: PRIVATE ADDRESS: RATELIMIT BYPASSED..", __func__, pthread_self(),
             ipstr);
      return false;
    }
  }

  if (handle == NULL || handle->backend == NULL) {
    syslog(LOG_WARNING, "%s {pid:'%lu', ip:'%s'}: ERROR: RATELIMIT COULD NOT BE DETERMINED (no rate-limit handle)..",
           __func__, pthread_self(), ipstr);
    return true;
  }

  const RateLimitPolicy *spec = RateLimitSpecsFor(RL_CAT_CONNECTONS);
  RateLimitStatus        st;
  if (RateLimitCheck(handle, ipstr, spec, &st) != RL_OK) {
    syslog(LOG_WARNING, "%s {pid:'%lu', ip:'%s'}: ERROR: RATELIMIT COULD NOT BE DETERMINED..", __func__, pthread_self(),
           ipstr);
    return true; /* fail-closed */
  }

  if (st.remaining_actions < 0) {
    if (spec->min_difference > 0) syslog(LOG_NOTICE, LOGSTR_IO_RL_EXCEEDED_RATE_ACTIONS, __func__, pthread_self(),
                                         ipstr, (long)st.remaining_time_ms, (long)st.remaining_actions,
                                         LOGCODE_IO_RL_EXCEEDED_RATE_ACTIONS);
    else syslog(LOG_NOTICE, LOGSTR_IO_RL_EXCEEDED_ACTIONS, __func__, pthread_self(), ipstr, (long)st.remaining_actions,
                LOGCODE_IO_RL_EXCEEDED_ACTIONS);
    return true;
  }

  return false;
}

static void
sWriteRateLimitedResponseAndClose(int nsocket, UFSRVResult *result_ptr)
{
  BufferDescriptor *bd       = NULL;
  struct timeval    snd_time = {.tv_sec = 1, .tv_usec = 0};
  struct timeval    rcv_time = {.tv_sec = 0, .tv_usec = 250000};
  char              drain[512];

  if (IS_PRESENT(result_ptr) && IS_PRESENT(result_ptr->result_user_data))
    bd = (BufferDescriptor*)result_ptr->result_user_data;

  if (IS_PRESENT(bd) && IS_PRESENT(bd->data) && bd->size > 0) {
    size_t written = 0;

    setsockopt(nsocket, SOL_SOCKET, SO_SNDTIMEO, &snd_time, sizeof(snd_time));
    while (written < bd->size) {
      ssize_t n = send(nsocket, bd->data + written, bd->size - written, MSG_NOSIGNAL);
      if (n > 0) {
        written += (size_t)n;
        continue;
      }
      if (n < 0 && errno == EINTR) continue;
      break;
    }

    shutdown(nsocket, SHUT_WR);

    setsockopt(nsocket, SOL_SOCKET, SO_RCVTIMEO, &rcv_time, sizeof(rcv_time));
    for (;;) {
      ssize_t n = recv(nsocket, drain, sizeof(drain), 0);
      if (n > 0) continue;
      if (n < 0 && errno == EINTR) continue;
      break;
    }
  }

  if (IS_PRESENT(result_ptr)) {
    if (IS_PRESENT(bd)) {
      BufferDescriptorRelease(bd);
      free(bd);
    }
    free(result_ptr);
  }

  close(nsocket);
}

static const Protocol *
sFindListenerProtocol(const Protocol *protocols, const char *key)
{
  const Protocol *match = NULL;
  for (const Protocol *entry = protocols;
       (entry->protocol_name && *entry->protocol_name) || (entry->protocol_key && *entry->protocol_key);
       ++entry) {
    if (entry->protocol_key && strcmp(entry->protocol_key, key) == 0) {
      if (match) return NULL;
      match = entry;
    }
  }
  return match;
}

bool
IsUfsrvListenersDescriptorValid(const CollectionDescriptor *listeners, const Protocol *protocols)
{
  if (!listeners || !listeners->collection || listeners->collection_sz == 0 || !protocols) return false;

  for (size_t i = 0; i < listeners->collection_sz; ++i) {
    const UfsrvListenerDescriptor *listener = listeners->collection[i];
    if (!listener || !listener->protocol_key || !*listener->protocol_key) {
      syslog(LOG_ERR, "ufsrv.listeners[%zu]: missing protocol key", i + 1);
      return false;
    }
    if (!sFindListenerProtocol(protocols, listener->protocol_key)) {
      syslog(LOG_ERR, "ufsrv.listeners[%zu]: protocol key '%s' must match exactly one registered protocol",
             i + 1, listener->protocol_key);
      return false;
    }
  }

  for (size_t i = 0; i < listeners->collection_sz; ++i) {
    UfsrvListenerDescriptor *listener = listeners->collection[i];
    listener->protocol = sFindListenerProtocol(protocols, listener->protocol_key);
  }
  return true;
}

Socket *
UfsrvListenerBootstrap(UfsrvListenerDescriptor *listener)
{
  int fd = SetupListeningSocket(listener->bind_address, listener->port, SOCK_TCP, SOCKOPT_IP4 | SOCKOPT_REUSEADDRE);
  if (fd < 0) {
    syslog(LOG_ERR, "%s: could not listen on %s:%u: %s", __func__, listener->bind_address,
           (unsigned)listener->port, strerror(errno));
    return NULL;
  }

  Socket *socket = calloc(1, sizeof(*socket));
  if (!socket) {
    close(fd);
    return NULL;
  }
  socket->type = SOCK_MAIN_LISTENER;
  socket->sock = fd;
  socket->port = listener->port;
  snprintf(socket->address, sizeof(socket->address), "%s", listener->bind_address);
  snprintf(socket->haddress, sizeof(socket->haddress), "%s", listener->bind_address);
  return socket;
}

void
InitWorkersDelegator(int protocol_id)
{
  ProtocolInvokeWorkersDelegatorInitCallback(protocol_id);
} //eo

static UFSRVResult *_CacheBackendUfsrvConfigRegisterUfserverInstance(PersistenceBackend *pers_ptr, UFSRVResult *res_ptr);
static UFSRVResult *_CacheBackendSetUfsrvConfigReqid(UfsrvSession *sesn_ptr, const char *server_class, int ufsrv_geogroup);
static UFSRVResult *_CacheBackendUfsrvConfigRegisterUfserverActivityWithSession(UfsrvSession *sesn_ptr, time_t activity_time);
static UFSRVResult *_CacheBackendUfsrvConfigGetGeoGroup(UfsrvSession *sesn_ptr, const char *server_class, unsigned, CollectionDescriptor *, CollectionDescriptor *);
static UFSRVResult *_CacheBackendUfsrvConfigGetGeogroupSize(UfsrvSession *sesn_ptr);
static UFSRVResult *_CacheBackendUfsrvConfigGetUfserverActivityTime(UfsrvSession *sesn_ptr, const char *server_class,
                                                                    int      ufsrv_geogroup, int   serverid_by_user);

/**
 * 	@brief: given a geogroup get a server instance to be used for request serving. server_class is the calls of
 * 	servers responsible for serving. ufsrvap --> ufsrv
 */
UfsrvInstanceDescriptor *
GetUfsrvInstance(UfsrvSession *                sesn_ptr, const char *server_class, unsigned ufsrv_geogroup,
                 UfsrvInstanceDescriptor *instance_ptr_out)
{
  int                  collection_idxs[CONFIF_MAX_UFSRV_INSTANCE_PER_GEOGROUP]       = {0};
  time_t               collection_idxs_times[CONFIF_MAX_UFSRV_INSTANCE_PER_GEOGROUP] = {0};
  CollectionDescriptor collection_ufsrv_ids                                          = {
    .collection = (collection_t**)collection_idxs, CONFIF_MAX_UFSRV_INSTANCE_PER_GEOGROUP
  };
  CollectionDescriptor collection_ufsrv_times = {
    .collection = (collection_t**)collection_idxs_times, CONFIF_MAX_UFSRV_INSTANCE_PER_GEOGROUP
  };

  if (unlikely((ufsrv_geogroup == 0))) ufsrv_geogroup = _CONFIGDEFAULT_DEFAULT_UFSRVGEOGROUP;

  UfsrvConfigGetGeoGroup(sesn_ptr, server_class, ufsrv_geogroup, &collection_ufsrv_ids, &collection_ufsrv_times);

  if (collection_ufsrv_ids.collection_sz > 0) {
    size_t    counter                      = 0;
    time_t    last_activity_time, time_now = time(NULL);
    long long reqid                        = UfsrvConfigGetReqid(sesn_ptr, server_class, ufsrv_geogroup);

    if (reqid > 0) {
      int ufsrv_instance     = reqid % collection_ufsrv_ids.collection_sz;
      int ufsrv_instance_idx = ufsrv_instance < CONFIF_MAX_UFSRV_INSTANCE_PER_GEOGROUP ?
                                 ufsrv_instance :
                                 CONFIF_MAX_UFSRV_INSTANCE_PER_GEOGROUP - 1;

      do {
        last_activity_time = UfsrvConfigGetUfsrverActivityTime(sesn_ptr, server_class, ufsrv_geogroup,
                                                               collection_idxs[ufsrv_instance_idx]);
        if (time_now - last_activity_time < _CONFIGDEDAULT_IDLE_TIME_INTERVAL_INTRA_REQUEST) {
          instance_ptr_out->serverid_by_user = collection_idxs[ufsrv_instance_idx];
          instance_ptr_out->reqid            = reqid;
          instance_ptr_out->ufsrv_geogroup   = ufsrv_geogroup;
          instance_ptr_out->server_class     = server_class;
          //TODO: this constant per instance and references invariant global value

#ifdef __UF_TESTING
          syslog(
            LOG_DEBUG,
            "%s {pid:'%lu', o:'%p', cid:'%lu', reqid:'%llu', counter:'%lu', idx:'%d', server_id:'%d'}: Retrieved Ufsrv Instance...",
            __func__, pthread_self(), sesn_ptr, (sesn_ptr)->session_id, reqid, counter, ufsrv_instance_idx,
            collection_idxs[ufsrv_instance_idx]);
#endif

          return (instance_ptr_out);
        }
        else {
          ufsrv_instance_idx = (ufsrv_instance_idx + 1) % collection_ufsrv_ids.collection_sz; //wrap around as necessary
          syslog(
            LOG_ERR,
            "%s {pid:'%lu', o:'%p', cid:'%lu', reqid:'%llu, counter:'%lu',  server_id:'%d', ufsrv_instance_idx_incremented:'%d', time_now:'%lu', last_activity_time:'%lu', time_diff:'%lu'}: WARNING: FOUND UNRESPONSIVE SERVER IN GEOGROUP: TRYING NEXT",
            __func__, pthread_self(), sesn_ptr, (sesn_ptr)->session_id, reqid, counter,
            collection_idxs[ufsrv_instance_idx], ufsrv_instance_idx, time_now, last_activity_time,
            time_now - last_activity_time);
        }
      }
      while (++counter <= collection_ufsrv_ids.collection_sz);
    }
    else {
      syslog(LOG_ERR, "%s {pid:'%lu', o:'%p', cid:'%lu', reqid:'%llu'}: ERROR: COULD NOT UNLOCK RETRIEVE REQUEST ID",
             __func__, pthread_self(), sesn_ptr, (sesn_ptr)->session_id, reqid);
    }
  }

  return NULL;
}

bool
UfsrvConfigRegisterUfsrverInstance(PersistenceBackend *pers_ptr)
{
  UFSRVResult res = {0};
  _CacheBackendUfsrvConfigRegisterUfserverInstance(pers_ptr, &res);
  if (res.result_type == RESULT_TYPE_SUCCESS) return true;

  return false;
}

/**
 * 	@brief Register presence with the Configuration Server. The registration gequires the server to specify its geo_group, which
 * 	is a configuration file setting.
 * 	@WARNING: This doesn't have Session linked to it so must use send_command_sessionless
 */
static UFSRVResult *
_CacheBackendUfsrvConfigRegisterUfserverInstance(PersistenceBackend *pers_ptr, UFSRVResult *res_ptr)
{
  int         rescode   = RESCODE_BACKEND_DATA;
  redisReply *redis_ptr = NULL;

  if (!(redis_ptr = (*pers_ptr->send_command_sessionless)(pers_ptr, REDIS_CMD_CONFIG_UFSRV_MEMBER_ATTRS_IDENTIFIERS_SET,
                                                          UfsrvConfigServerClass(), UfsrvConfigUfsrvGeogroup(),
                                                          UfsrvConfigServerInstanceId(), getpid(), "0.0.0.0",
                                                          UfsrvConfigWhen(), UfsrvConfigServerInstanceId(),
                                                          UfsrvConfigWhen()))) goto return_redis_error;
  if (redis_ptr->type == REDIS_REPLY_STATUS && !(strcasecmp(redis_ptr->str, "OK") == 0)) {
    syslog(LOG_ERR, "%s {pid:'%lu, reply:'%s'}: ERROR: COULD NOT SET SERVER ATTRIBUTES...", __func__, pthread_self(),
           redis_ptr->str);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }

  freeReplyObject(redis_ptr);

  if (!(redis_ptr = (*pers_ptr->send_command_sessionless)(pers_ptr, REDIS_CMD_CONFIG_UFSRV_MEMBER_ADD,
                                                          UfsrvConfigServerClass(), UfsrvConfigUfsrvGeogroup(),
                                                          UfsrvConfigServerInstanceId()))) goto return_redis_error;

  if (redis_ptr->type == REDIS_REPLY_INTEGER) {
    // && redis_ptr->integer==1) //it can still suceed with 0 if the element is already in the set
    long long result = redis_ptr->integer;
    freeReplyObject(redis_ptr);
    _RETURN_RESULT_RES(res_ptr, (void *) (uintptr_t)result, RESULT_TYPE_SUCCESS, RESCODE_BACKEND_DATA)
  }

  if (redis_ptr->type == REDIS_REPLY_ERROR) goto return_redis_error;
  if (redis_ptr->type == REDIS_REPLY_NIL) goto return_redis_error;

return_redis_error:
  if (IS_EMPTY(redis_ptr)) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: CACHE BACKEND: NO REPLY RECEIVED...", __func__, pthread_self());
  }
  if (redis_ptr->type == REDIS_REPLY_ERROR) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: REDIS RESULTSET. Error: '%s'", __func__, pthread_self(), redis_ptr->str);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }
  if (redis_ptr->type == REDIS_REPLY_NIL) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: NIL SET", __func__, pthread_self());
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }

return_error:
  freeReplyObject(redis_ptr);
  _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, rescode)
}

/**
 * 	@brief: wrapper routine
 */
long long
UfsrvConfigGetReqid(UfsrvSession *sesn_ptr, const char *server_class, int ufsrv_geogroup)
{
  _CacheBackendSetUfsrvConfigReqid(sesn_ptr, server_class, ufsrv_geogroup);
  if (_RESULT_TYPE_SUCCESS(UFSRV_SESSION_RESULT_PTR(sesn_ptr))) return (long long)(intptr_t)_RESULT_USERDATA(UFSRV_SESSION_RESULT_PTR(sesn_ptr));

  return 0;
}

/**
 * 	@brief: Increment and return the request counter
 */
static UFSRVResult *
_CacheBackendSetUfsrvConfigReqid(UfsrvSession *sesn_ptr, const char *server_class, int ufsrv_geogroup)
{
  int rescode = RESCODE_BACKEND_DATA;

  PersistenceBackend *pers_ptr  = THREAD_CONTEXT_SESSION_CACHEBACKEND;
  redisReply *        redis_ptr = NULL;

  if (!(redis_ptr = (*pers_ptr->send_command)(sesn_ptr, pers_ptr, REDIS_CMD_CONFIG_UFSRV_REQID_INC, server_class,
                                              ufsrv_geogroup))) goto return_redis_error;

  if (redis_ptr->type == REDIS_REPLY_INTEGER) {
    _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), (void *) (uintptr_t)redis_ptr->integer, RESULT_TYPE_SUCCESS, RESCODE_BACKEND_DATA)
  }

  if (redis_ptr->type == REDIS_REPLY_ERROR) goto return_redis_error;
  if (redis_ptr->type == REDIS_REPLY_NIL) goto return_redis_error;

return_redis_error:
  if (IS_EMPTY(redis_ptr)) {
    syslog(LOG_DEBUG, "%s {pid:'%lu, o:'%p', cid:'%lu'}: ERROR: CACHE BACKEND: NO REPLY RECEIVED...", __func__,
           pthread_self(), sesn_ptr, (sesn_ptr)->session_id);
  }
  if (redis_ptr->type == REDIS_REPLY_ERROR) {
    syslog(LOG_DEBUG, "%s {pid:'%lu, o:'%p', cid:'%lu'}: ERROR: REDIS RESULTSET. Error: '%s'", __func__, pthread_self(),
           sesn_ptr, (sesn_ptr)->session_id, redis_ptr->str);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }
  if (redis_ptr->type == REDIS_REPLY_NIL) {
    syslog(LOG_DEBUG, "%s {pid:'%lu, o:'%p', cid:'%lu'}: ERROR: NIL SET", __func__, pthread_self(), sesn_ptr,
           (sesn_ptr)->session_id);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }

return_error:
  freeReplyObject(redis_ptr);
  _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), NULL, RESULT_TYPE_ERR, rescode)
}

time_t
UfsrvConfigGetUfsrverActivityTime(UfsrvSession *sesn_ptr, const char *server_class, int ufsrv_geogroup, int serverid_by_user)
{
  _CacheBackendUfsrvConfigGetUfserverActivityTime(sesn_ptr, server_class, ufsrv_geogroup, serverid_by_user);
  if (_RESULT_TYPE_SUCCESS(UFSRV_SESSION_RESULT_PTR(sesn_ptr))) {
    return ((time_t)(intptr_t)_RESULT_USERDATA(UFSRV_SESSION_RESULT_PTR(sesn_ptr)));
  }

  return 0;
}

static UFSRVResult *
_CacheBackendUfsrvConfigGetUfserverActivityTime(UfsrvSession *sesn_ptr, const char *server_class, int ufsrv_geogroup,
                                                int      serverid_by_user)
{
  int rescode = RESCODE_BACKEND_DATA;

  PersistenceBackend *pers_ptr  = THREAD_CONTEXT_SESSION_CACHEBACKEND;
  redisReply *        redis_ptr = NULL;

  if (!(redis_ptr = (*pers_ptr->send_command)(sesn_ptr, pers_ptr, REDIS_CMD_CONFIG_UFSRV_MEMBER_ATTR_LAST_GET,
                                              server_class, ufsrv_geogroup, serverid_by_user))) goto return_redis_error;

  if (redis_ptr->type == REDIS_REPLY_ARRAY) {
    // && redis_ptr->integer==1) //it can still succeed with 0 if the element is already in the set
    _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), (void *) (uintptr_t)strtoul(redis_ptr->element[0]->str, NULL, 10),
                        RESULT_TYPE_SUCCESS, RESCODE_BACKEND_DATA)
  }

  if (redis_ptr->type == REDIS_REPLY_ERROR) goto return_redis_error;
  if (redis_ptr->type == REDIS_REPLY_NIL) goto return_redis_error;

return_redis_error:
  if (IS_EMPTY(redis_ptr)) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: CACHE BACKEND: NO REPLY RECEIVED...", __func__, pthread_self());
  }
  if (redis_ptr->type == REDIS_REPLY_ERROR) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: REDIS RESULTSET. Error: '%s'", __func__, pthread_self(), redis_ptr->str);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }
  if (redis_ptr->type == REDIS_REPLY_NIL) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: NIL SET", __func__, pthread_self());
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }

return_error:
  freeReplyObject(redis_ptr);
  _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), NULL, RESULT_TYPE_ERR, rescode)
}

/**
 * 	@brief wrapper function
 */
bool
UfsrvConfigRegisterUfserverActivityWithSession(UfsrvSession *sesn_ptr, time_t activity_time)
{
  _CacheBackendUfsrvConfigRegisterUfserverActivityWithSession(sesn_ptr, activity_time);
  if (_RESULT_TYPE_SUCCESS(UFSRV_SESSION_RESULT_PTR(sesn_ptr))) return true;

  return false;
}

static UFSRVResult *
_CacheBackendUfsrvConfigRegisterUfserverActivityWithSession(UfsrvSession *sesn_ptr, time_t activity_time)
{
  int rescode = RESCODE_BACKEND_DATA;

  PersistenceBackend *pers_ptr  = THREAD_CONTEXT_SESSION_CACHEBACKEND;
  redisReply *        redis_ptr = NULL;

  if (!(redis_ptr = (*pers_ptr->send_command)(sesn_ptr, pers_ptr, REDIS_CMD_CONFIG_UFSRV_MEMBER_ATTR_LAST_SET,
                                              UfsrvConfigServerClass(), UfsrvConfigUfsrvGeogroup(),
                                              UfsrvConfigServerInstanceId(), activity_time))) goto return_redis_error;

  if (redis_ptr->type == REDIS_REPLY_INTEGER) {
    // && redis_ptr->integer==1) //it can still suceed with 0 if the element is already in the set
    _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), (void *) (uintptr_t)redis_ptr->integer, RESULT_TYPE_SUCCESS, RESCODE_BACKEND_DATA)
  }

  if (redis_ptr->type == REDIS_REPLY_ERROR) goto return_redis_error;
  if (redis_ptr->type == REDIS_REPLY_NIL) goto return_redis_error;

return_redis_error:
  if (IS_EMPTY(redis_ptr)) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: CACHE BACKEND: NO REPLY RECEIVED...", __func__, pthread_self());
  }
  if (redis_ptr->type == REDIS_REPLY_ERROR) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: REDIS RESULTSET. Error: '%s'", __func__, pthread_self(), redis_ptr->str);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }
  if (redis_ptr->type == REDIS_REPLY_NIL) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: NIL SET", __func__, pthread_self());
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }

return_error:
  freeReplyObject(redis_ptr);
  _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), NULL, RESULT_TYPE_ERR, rescode)
}

/**
 * 	@brief: Wrapper routine to get server collection serving a geogroup with which session is affiliated and served by.
 * 	@param collection_ptr: must be be fully allocated by user
 * 	@returns: collection of server id (integers)
 */
CollectionDescriptor *
UfsrvConfigGetGeoGroup(UfsrvSession *             sesn_ptr, const char *server_class, unsigned ufsrv_geogroup,
                       CollectionDescriptor *collection_ptr_ids, CollectionDescriptor *collection_ptr_times)
{
  _CacheBackendUfsrvConfigGetGeoGroup(sesn_ptr, server_class, ufsrv_geogroup, collection_ptr_ids, collection_ptr_times);

  if (_RESULT_TYPE_SUCCESS(UFSRV_SESSION_RESULT_PTR(sesn_ptr))) return (CollectionDescriptor*)_RESULT_USERDATA(UFSRV_SESSION_RESULT_PTR(sesn_ptr));

  return NULL;
}

/**
 * 	@brief: returns  id/last service times pairs for member servers of a given geogroup.
 * 	We allow for reassignment of user's geogroup to a default one if: 1)user's was set to zero, 2)user's group returned zero server collection
 *
 */
static UFSRVResult *
_CacheBackendUfsrvConfigGetGeoGroup(UfsrvSession *             sesn_ptr, const char *server_class, unsigned ufsrv_geogroup,
                                    CollectionDescriptor *collection_ptr_ids,
                                    CollectionDescriptor *collection_ptr_times_DEL)
{
  bool default_group_flagged = false;
  int  rescode;

  PersistenceBackend *pers_ptr  = THREAD_CONTEXT_SESSION_CACHEBACKEND;
  redisReply *        redis_ptr = NULL;

run_command:

  if (!(redis_ptr = (*pers_ptr->send_command)(sesn_ptr, pers_ptr, REDIS_CMD_CONFIG_UFSRV_MEMBER_GETALL, server_class,
                                              ufsrv_geogroup))) goto return_redis_error;

  if (redis_ptr->type == REDIS_REPLY_ARRAY && redis_ptr->elements > 0) {
    int *ufsrv_group_ids = (int*)collection_ptr_ids->collection;
    //time_t	*ufsrv_group_times	=	(time_t *)collection_ptr_times->collection;

    for (size_t i = 0; i < redis_ptr->elements; i++) {
      ufsrv_group_ids[i] = atoi(redis_ptr->element[i]->str);
    }

    collection_ptr_ids->collection_sz = redis_ptr->elements;
    freeReplyObject(redis_ptr);

    _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), collection_ptr_ids, RESULT_TYPE_SUCCESS, RESCODE_BACKEND_DATA);
  }
  else {
    //group is empty (eg servers all died, or that group dosnt have assigned servers), reassign to known default group
    if (unlikely(default_group_flagged)) goto return_default_group_flagged;

    ufsrv_geogroup        = _CONFIGDEFAULT_DEFAULT_UFSRVGEOGROUP;
    default_group_flagged = true; //to avoid endless loop in case the default grous has also died
    goto run_command;
  }

return_redis_error:
  if (IS_EMPTY(redis_ptr)) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: CACHE BACKEND: NO REPLY RECEIVED...", __func__, pthread_self());
    _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), NULL, RESULT_TYPE_ERR, RESCODE_LOGIC_EMPTY_RESOURCE);
  }
  if (redis_ptr->type == REDIS_REPLY_ERROR) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: REDIS RESULTSET. Error: '%s'", __func__, pthread_self(), redis_ptr->str);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }
  if (redis_ptr->type == REDIS_REPLY_NIL) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: NIL SET", __func__, pthread_self());
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }

return_default_group_flagged:
  syslog(
    LOG_ERR,
    "%s {pid:'%lu, o:'%p', ufsrv_geogroup_user:'%d', ufsrv_geogroup_default:'%d'}: DEFAULT GEO GROUP WAS FLAGGED UNAVAILABLE",
    __func__, pthread_self(), sesn_ptr, UFSRV_SESSION_GEOGROUP(sesn_ptr), ufsrv_geogroup);
  rescode                           = RESCODE_UFSRVGEOGROUP_DEFAULT;
  collection_ptr_ids->collection_sz = 0;

return_error:
  freeReplyObject(redis_ptr);
  _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), NULL, RESULT_TYPE_ERR, rescode);
}

/**
 * 	@brief: returns the identifier portion only for servers withing a given geogroup
 */
/**
 * 	@brief: Get server collection serving a geogroup with which session is affiliated and served by.
 * 	@param collection_ptr: must be be fully allocated by user
 * 	@returns: collection of server id (integers)
 */
__unused static UFSRVResult *
_CacheBackendUfsrvConfigGetGeoGroupInstances(UfsrvSession *sesn_ptr, CollectionDescriptor *collection_ptr)
{
  int rescode = RESCODE_BACKEND_DATA;

  PersistenceBackend *pers_ptr  = THREAD_CONTEXT_SESSION_CACHEBACKEND;
  redisReply *        redis_ptr = NULL;

  if (!(redis_ptr = (*pers_ptr->send_command)(sesn_ptr, pers_ptr, REDIS_CMD_CONFIG_UFSRV_MEMBER_GETALL,
                                              UfsrvConfigServerClass(),
                                              UFSRV_SESSION_GEOGROUP(sesn_ptr)))) goto return_redis_error;

  if (redis_ptr->type == REDIS_REPLY_ARRAY && redis_ptr->elements > 0) {
    int *ufsrv_group = (int*)collection_ptr->collection;
    for (size_t i = 0; i < redis_ptr->elements; i++) {
      char *last_service_time;
      if ((last_service_time = strchr(redis_ptr->element[0]->str, ':'))) { *last_service_time++ = '\0'; }
      ufsrv_group[i] = atoi(redis_ptr->element[i]->str);
    }
    collection_ptr->collection_sz = redis_ptr->elements;
    freeReplyObject(redis_ptr);
    _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), collection_ptr, RESULT_TYPE_SUCCESS, RESCODE_BACKEND_DATA);
  }

  if (redis_ptr->type == REDIS_REPLY_ERROR) goto return_redis_error;
  if (redis_ptr->type == REDIS_REPLY_NIL) goto return_redis_error;

return_redis_error:
  if (IS_EMPTY(redis_ptr)) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: CACHE BACKEND: NO REPLY RECEIVED...", __func__, pthread_self());
  }
  if (redis_ptr->type == REDIS_REPLY_ERROR) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: REDIS RESULTSET. Error: '%s'", __func__, pthread_self(), redis_ptr->str);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }
  if (redis_ptr->type == REDIS_REPLY_NIL) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: NIL SET", __func__, pthread_self());
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }

return_error:
  freeReplyObject(redis_ptr);
  _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), NULL, RESULT_TYPE_ERR, rescode);
}

//

/**
 * 	@brief: Wrapper routine to get the number of present servers within a give geogroup
 */
size_t
UfsrvConfigGetGeogroupSize(UfsrvSession *sesn_ptr)
{
  _CacheBackendUfsrvConfigGetGeogroupSize(sesn_ptr);
  if (_RESULT_TYPE_SUCCESS(UFSRV_SESSION_RESULT_PTR(sesn_ptr))) return (size_t)(intptr_t)_RESULT_USERDATA(UFSRV_SESSION_RESULT_PTR(sesn_ptr));

  return 0;
}

/**
 * 	@brief: return the number of servers present in a given geogroup area
 */
static UFSRVResult *
_CacheBackendUfsrvConfigGetGeogroupSize(UfsrvSession *sesn_ptr)
{
  int rescode = RESCODE_BACKEND_DATA;

  PersistenceBackend *pers_ptr  = THREAD_CONTEXT_SESSION_CACHEBACKEND;
  redisReply *        redis_ptr = NULL;

  if (!(redis_ptr = (*pers_ptr->send_command)(sesn_ptr, pers_ptr, REDIS_CMD_CONFIG_UFSRV_MEMBERS_SZ,
                                              UfsrvConfigServerClass(),
                                              UFSRV_SESSION_GEOGROUP(sesn_ptr)))) goto return_redis_error;

  if (redis_ptr->type == REDIS_REPLY_INTEGER)
  // && redis_ptr->integer==1) //it can still suceed with 0 if the element is already in the set
  {
    long long group_sz = redis_ptr->integer;
    freeReplyObject(redis_ptr);
    _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), (void *) (uintptr_t)group_sz, RESULT_TYPE_SUCCESS, RESCODE_BACKEND_DATA);
  }

  if (redis_ptr->type == REDIS_REPLY_ERROR) goto return_redis_error;
  if (redis_ptr->type == REDIS_REPLY_NIL) goto return_redis_error;

return_redis_error:
  if (IS_EMPTY(redis_ptr)) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: CACHE BACKEND: NO REPLY RECEIVED...", __func__, pthread_self());
  }
  if (redis_ptr->type == REDIS_REPLY_ERROR) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: REDIS RESULTSET. Error: '%s'", __func__, pthread_self(), redis_ptr->str);
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }
  if (redis_ptr->type == REDIS_REPLY_NIL) {
    syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: NIL SET", __func__, pthread_self());
    rescode = RESCODE_BACKEND_DATA;
    goto return_error;
  }

return_error:
  freeReplyObject(redis_ptr);
  _RETURN_RESULT_RES(UFSRV_SESSION_RESULT_PTR(sesn_ptr), NULL, RESULT_TYPE_ERR, rescode);
}

//todo fix me this needs to be refactored to read off UfsrvConfigxxxx() which will have had a pass at the config file already
//currently there is no corresponding one for 'ufsrv_workers_thread_pool'. see OnThreadPoolAvailable()
size_t
GetJobWorkersPoolSize(size_t default_sz)
{
  size_t pool_size = UfsrvConfigUfsrvWorkersThreadPool();

  if (pool_size < 1) pool_size = default_sz;

  return pool_size;
}

static UFSRVResult *_CacheBackendUfsrvConfigRegisterUfserverActivity(PersistenceBackend *pers_ptr, time_t activity_time,
                                                                     UFSRVResult *       res_ptr);

bool
UfsrvConfigRegisterUfsrverActivity(PersistenceBackend *pers_ptr, time_t activity_time)
{
  UFSRVResult res = {0};
  _CacheBackendUfsrvConfigRegisterUfserverActivity(pers_ptr, activity_time, &res);
  if (res.result_type == RESULT_TYPE_SUCCESS) return true;

  return false;
}

/**
 * 	@brief Register presence with the Configuration Server. The registration requires the server to specify its geo_group, which
 * 	is a configuration file setting.
 * 	@returns: RESULT_TYPE_SUCCESS
 * 	@WARNING: This doesn't have Session linked to it so must use send_command_sessionless
 */
static UFSRVResult *
_CacheBackendUfsrvConfigRegisterUfserverActivity(PersistenceBackend *pers_ptr, time_t activity_time,
                                                 UFSRVResult *       res_ptr)
{
  int         rescode   = RESCODE_BACKEND_DATA;
  redisReply *redis_ptr = NULL;

  if (!(redis_ptr = (*pers_ptr->send_command_sessionless)(pers_ptr, REDIS_CMD_CONFIG_UFSRV_MEMBER_ATTR_LAST_SET,
                                                          UfsrvConfigServerClass(), UfsrvConfigUfsrvGeogroup(),
                                                          UfsrvConfigServerInstanceId(),
                                                          activity_time))) goto redis_connectivity_error;

  if ((redis_ptr->type == REDIS_REPLY_STATUS) && (strcasecmp(redis_ptr->str, "OK") == 0)) {
    //NOTE: redis_ptr->integer is not relevant for HMSET
    _RETURN_RESULT_RES(res_ptr, (void *) (uintptr_t)redis_ptr->integer, RESULT_TYPE_SUCCESS, RESCODE_BACKEND_DATA)
  }

  if (redis_ptr->type == REDIS_REPLY_ERROR) goto redis_error_reply;
  if (redis_ptr->type == REDIS_REPLY_NIL) goto redis_error_nil;

redis_connectivity_error:
  syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: CACHE BACKEND: NO REPLY RECEIVED...", __func__, pthread_self());
  goto return_error;

redis_error_reply:
  syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: REDIS RESULTSET. Error: '%s'", __func__, pthread_self(), redis_ptr->str);
  rescode = RESCODE_BACKEND_DATA;
  goto return_error_free;

redis_error_nil:
  syslog(LOG_DEBUG, "%s {pid:'%lu}: ERROR: NIL SET", __func__, pthread_self());
  rescode = RESCODE_BACKEND_DATA;
  goto return_error_free;

return_error_free:
  freeReplyObject(redis_ptr);

return_error: _RETURN_RESULT_RES(res_ptr, NULL, RESULT_TYPE_ERR, rescode)
}
