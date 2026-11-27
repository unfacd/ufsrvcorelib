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

#include <uflib/net/utils_net.h>
#include <ufsrvcorelib/ufsrv_core/command_console/command_console_thread.h>
#include <ufsrvcorelib/ufsrv_core/session/session_call_flags.h>
#include <ufsrvcorelib/ufsrv_core/session/sessions_utils.h>
#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_shim.h>
#include "ufsrv_core/session/sesstion_stat_flags.h"
#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <uflib/adt/adt_distinct_array.h>
#include <openssl/evp.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <syslog.h>
#include <unistd.h>

//read chunk size for the command console input stream
#define COMMAND_CONSOLE_READ_BUF_SIZE 4096

static void sPrintSslError(SSL *ssl, int error_code);
static int  sConsoleWriteAll(SSL *ssl, int sock, const char *buf, int len);
static void sDispatchCommand(ConsoleCommandHandler handler, SSL *ssl, int sock, char *buf, int n);
static bool sConsolePeerAllowed(SSL *ssl);

static ConsoleCommandHandler s_command_handler;

void *
ThreadCommandConsoleClient(void *ptr)
{
  UfsrvSession *sesn_ptr = ptr;
  Socket * s_ptr    = sesn_ptr->ssptr;
  SSL *    ssl      = SESNSTATUS_IS_SET(sesn_ptr->stat, SESNSTATUS_SECURE) ? sesn_ptr->session_crypto.ssl : NULL;
  char     buf[COMMAND_CONSOLE_READ_BUF_SIZE + 1];

  while (1) {
    int            ready;
    fd_set         fd;
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};

    //OpenSSL may hold decrypted bytes not yet visible to select(): poll them first
    if (ssl && SSL_pending(ssl) > 0) {
      ready = 1;
    }
    else {
      FD_ZERO(&fd);
      FD_SET(s_ptr->sock, &fd);
      ready = select(s_ptr->sock + 1, &fd, NULL, NULL, &timeout);
    }

    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      syslog(LOG_ERR, "%s: select error: %s", __func__, strerror(errno));
      break;
    }
    if (ready == 0) {
      continue; //timeout, poll again
    }

    int n;
    if (ssl) {
      ERR_clear_error();
      n = SSL_read(ssl, buf, COMMAND_CONSOLE_READ_BUF_SIZE);
      if (n <= 0) {
        int err = SSL_get_error(ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
          continue; //non-blocking retry
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
          syslog(LOG_INFO, "%s: command console peer closed the connection", __func__);
        }
        else {
          syslog(LOG_ERR, "%s: SSL_read error (%d): %s", __func__, err, ERR_error_string(ERR_get_error(), NULL));
        }
        break;
      }
    }
    else {
      n = read(s_ptr->sock, buf, COMMAND_CONSOLE_READ_BUF_SIZE);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
          continue;
        }
        syslog(LOG_ERR, "%s: read error: %s", __func__, strerror(errno));
        break;
      }
      if (n == 0) {
        syslog(LOG_INFO, "%s: command console peer closed the connection", __func__);
        break;
      }
    }

    if (s_command_handler) {
      sDispatchCommand(s_command_handler, ssl, s_ptr->sock, buf, n);
    }
    else {
      //no handler registered — echo the raw bytes back (round-trip test)
      syslog(LOG_INFO, "%s: received %d bytes from command console peer — echoing back", __func__, n);
      if (sConsoleWriteAll(ssl, s_ptr->sock, buf, n) < 0) {
        break;
      }
    }
  }

  //teardown the (optionally encrypted) connection
  if (ssl) {
    SSL_shutdown(ssl); //best effort: send close_notify
    SSL_free(ssl);
  }
  close(s_ptr->sock);
  free(s_ptr);
  //NOTE: the Session object is intentionally not freed here — no public teardown API
  //exists for non-recycler sessions (matches pre-existing behaviour).

  return NULL;
}

//Compute the connecting client's SPKI SHA-256 hash and test it against the
//console deny/god allow lists. Returns true if the peer is permitted.
//
//The deny list takes precedence, then the god list. The browser list is not yet
//wired. Lookup uses DistinctArrayIsItemStored (O(1) hash lookup); a constant-time
//CRYPTO_memcmp scan is a possible hardening follow-up (see the console design doc §16).
static bool
sConsolePeerAllowed(SSL *ssl)
{
  X509 *peer = SSL_get_peer_certificate(ssl);
  if (!peer) {
    return false;
  }

  unsigned char spki[EVP_MAX_MD_SIZE];
  unsigned int  spki_len = 0;
  X509_PUBKEY *public_key = X509_get_X509_PUBKEY(peer);
  if (!public_key ||
      ASN1_item_digest(ASN1_ITEM_rptr(X509_PUBKEY), EVP_sha256(), public_key, spki, &spki_len) != 1 ||
      spki_len != 32) {
    X509_free(peer);
    return false;
  }
  X509_free(peer);

  //32 bytes -> 64 lowercase hex chars + NUL (the format stored in the allow lists)
  static const char hex_digits[] = "0123456789abcdef";
  char              spki_hex[65];
  for (unsigned int i = 0; i < spki_len; i++) {
    spki_hex[i * 2]     = hex_digits[(spki[i] >> 4) & 0x0f];
    spki_hex[i * 2 + 1] = hex_digits[spki[i] & 0x0f];
  }
  spki_hex[spki_len * 2] = '\0';

  DistinctArray *deny = UfsrvConfigSslCommandConsoleDenyListAsPtr();
  if (deny && DistinctArrayIsItemStored(deny, (uint8_t*)spki_hex)) {
    syslog(LOG_WARNING, "%s: command console client SPKI '%s' is deny-listed", __func__, spki_hex);
    return false;
  }

  DistinctArray *god = UfsrvConfigSslCommandConsoleAllowListGodAsPtr();
  if (god && DistinctArrayIsItemStored(god, (uint8_t*)spki_hex)) {
    return true;
  }

  return false;
}

//s_ptr: command console listening socket
int
AnswerCommandConsoleRequest(Socket *s_ptr)
{
  int                nsocket, sin_size, opt = 1;
  struct sockaddr_in hisaddr;

  sin_size = sizeof(struct sockaddr_in);

  nsocket = accept(s_ptr->sock, (struct sockaddr*)&hisaddr, (socklen_t*)&sin_size);

  if (nsocket < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      return 0; //nothing pending
    }
    syslog(LOG_ERR, "%s: ERROR: COULD NOT accept connection: %s", __func__, strerror(errno));

    return 0;
  }

  setsockopt(nsocket, SOL_SOCKET, SO_KEEPALIVE, (void*)&opt, sizeof(int));

  //one SSL object per connection: an SSL* represents a single TLS session and must
  //never be shared across connections
  SSL *ssl = NULL;
  if (UfsrvConfigSslCommandConsoleIsEnabled()) {
    SSL_CTX *ssl_ctx = UfsrvConfigSslCtx();
    if (!ssl_ctx) {
      syslog(LOG_ERR, "%s: ERROR: command console SSL context not initialised", __func__);
      close(nsocket);

      return 0;
    }

    ssl = SSL_new(ssl_ctx);
    if (!ssl) {
      syslog(LOG_ERR, "%s: ERROR: COULD NOT CREATE SSL object: %s", __func__, ERR_error_string(ERR_get_error(), NULL));
      close(nsocket);

      return 0;
    }

    SSL_set_fd(ssl, nsocket);
    ERR_clear_error();
    int accept_return_status = SSL_accept(ssl);
    if (accept_return_status <= 0) {
      sPrintSslError(ssl, accept_return_status);
      SSL_free(ssl);
      close(nsocket);

      return 0;
    }

    syslog(LOG_INFO, "%s: SECURE command console session established (cipher: '%s')", __func__, SSL_get_cipher(ssl));

    //mutual-TLS allow-list gate: the connecting client's SPKI SHA-256 hash must
    //be present in the allow lists (deny list excluded). Runs only when client
    //certificates are required.
    if (UfsrvConfigSslConsoleIsClientCertificateRequired() && !sConsolePeerAllowed(ssl)) {
      syslog(LOG_WARNING, "%s: command console client rejected — SPKI hash not allow-listed", __func__);
      SSL_shutdown(ssl);
      SSL_free(ssl);
      close(nsocket);
      return 0;
    }
  }

  Socket *s_ptr_console_client = calloc(1, sizeof(Socket));
  if (!s_ptr_console_client) {
    if (ssl) SSL_free(ssl);
    close(nsocket);

    return 0;
  }

  s_ptr_console_client->sock = nsocket;
  strcpy(s_ptr_console_client->haddress, (char*)inet_ntoa(hisaddr.sin_addr));
  strcpy(s_ptr_console_client->address, "localhost");
  s_ptr_console_client->hport = ntohs(hisaddr.sin_port);
  s_ptr_console_client->port  = UfsrvConfigCommandConsolePort();

  UfsrvSession *sesn_ptr = InstantiateUfsrvSession(s_ptr_console_client, NULL, CALL_FLAG_HASH_SESSION_LOCALLY, -1);
  if (!sesn_ptr) {
    syslog(LOG_ERR, "%s: ERROR: COULD NOT INSTANTIATE command console session", __func__);
    if (ssl) SSL_free(ssl);
    close(nsocket);
    free(s_ptr_console_client);

    return 0;
  }

  sesn_ptr->session_crypto.ssl = ssl;
  if (ssl) {
    SESNSTATUS_SET(sesn_ptr->stat, SESNSTATUS_SECURE);
  }
  sesn_ptr->session_id = GenerateSessionIdLocally();

  syslog(LOG_INFO, ">> %s: Command Console connection from '%s': Creating Command Console Client thread...", __func__,
         sesn_ptr->ssptr->haddress);

  pthread_t      th;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int rc = pthread_create(&th, &attr, ThreadCommandConsoleClient, sesn_ptr);
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    syslog(LOG_ERR, "%s: ERROR: COULD NOT CREATE command console client thread: %s", __func__, strerror(rc));
    if (ssl) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
    }
    close(nsocket);
    free(s_ptr_console_client);

    return 0;
  }

  return nsocket;
}

Socket *
SetupCommandConsole(void)
{
  int socket;

  if ((socket = SetupListeningSocket(UfsrvConfigCommandConsoleAddress(), UfsrvConfigCommandConsolePort(), SOCK_TCP, SOCKOPT_IP4 | SOCKOPT_REUSEADDRE)) > 0) {
    Socket *s_ptr = calloc(1, (sizeof(Socket)));
    if (!s_ptr) {
      close(socket);
      return NULL;
    }

    s_ptr->type = SOCK_COMMAND_CONSOLE;
    s_ptr->sock = socket;
    strcpy(s_ptr->address, "localhost");
    strcpy(s_ptr->haddress, "localhost");

    syslog(LOG_INFO, ">> AnswerCommandConsoleRequest: Successfully created Command Console on port %d (fd=%d)...", UfsrvConfigCommandConsolePort(), s_ptr->sock);

    return s_ptr;
  }
  else {
    syslog(LOG_INFO, ">> AnswerCommandConsoleRequest: ERROR: COUL NOT create Command Console port %d (error: '%s')...", UfsrvConfigCommandConsolePort(), strerror(errno));
  }

  return NULL;
}

//Write exactly len bytes back to the peer, handling partial writes and
//non-blocking retry (SSL_ERROR_WANT_READ/WRITE, EAGAIN/EWOULDBLOCK/EINTR).
//Returns len on success, -1 on error.
static int
sConsoleWriteAll(SSL *ssl, int sock, const char *buf, int len)
{
  int written = 0;

  while (written < len) {
    int w;

    if (ssl) {
      ERR_clear_error();
      w = SSL_write(ssl, buf + written, len - written);
      if (w <= 0) {
        int err = SSL_get_error(ssl, w);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
          continue; //non-blocking retry
        }
        syslog(LOG_ERR, "%s: SSL_write error (%d): %s", __func__, err, ERR_error_string(ERR_get_error(), NULL));
        return -1;
      }
    }
    else {
      w = write(sock, buf + written, len - written);
      if (w < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
          continue;
        }
        syslog(LOG_ERR, "%s: write error: %s", __func__, strerror(errno));
        return -1;
      }
      if (w == 0) {
        return -1; //peer closed
      }
    }

    written += w;
  }

  return written;
}

void
ConsoleThreadRegisterCommandHandler(ConsoleCommandHandler handler)
{
  s_command_handler = handler;
}

static void
sDispatchCommand(ConsoleCommandHandler handler, SSL *ssl, int sock, char *buf, int n)
{
  char *method, *path, *p;

  buf[n] = '\0';

  //skip leading whitespace, then read the method token
  p = buf;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  method = p;
  while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
    p++;
  }
  if (*p) {
    *p++ = '\0';
  }
  //skip whitespace, then take the rest of the line as the path (free-form Lua
  //in EVAL / ->eval needs spaces, so path is the remainder, not a single token)
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  path = p;
  //trim trailing whitespace (CR/LF, space, tab)
  p    = path + strlen(path);
  while (p > path && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\r' || p[-1] == '\n')) {
    *--p = '\0';
  }

  if (!*method || !*path) {
    return; //malformed request
  }

  BufferDescriptor bd;
  BufferDescriptorInit(&bd, 256);
  BufferDescriptor *result = handler(method, path, &bd);
  if (result && result->data && result->size > 0) {
    sConsoleWriteAll(ssl, sock, result->data, (int)result->size);
    sConsoleWriteAll(ssl, sock, "\n", 1);
  }
  BufferDescriptorRelease(&bd);
}

static void
sPrintSslError(SSL *ssl, int error_code)
{
  int           parsed_error_code = SSL_get_error(ssl, error_code);
  unsigned long err               = ERR_get_error();

  switch (parsed_error_code) {
  case SSL_ERROR_NONE:
    syslog(LOG_ERR, "SSL_ERROR_NONE (%d): %s", error_code, ERR_error_string(err, NULL));
    break;

  case SSL_ERROR_WANT_READ:
    syslog(LOG_ERR, "SSL_ERROR_WANT_READ (%d): operation would block — retry", error_code);
    break;

  case SSL_ERROR_WANT_WRITE:
    syslog(LOG_ERR, "SSL_ERROR_WANT_WRITE (%d): operation would block — retry", error_code);
    break;

  case SSL_ERROR_ZERO_RETURN:
    syslog(LOG_ERR, "SSL_ERROR_ZERO_RETURN (%d): peer closed the connection", error_code);
    break;

  case SSL_ERROR_SYSCALL:
    syslog(LOG_ERR, "SSL_ERROR_SYSCALL (%d): I/O error (%s)", error_code, ERR_error_string(err, NULL));
    break;

  case SSL_ERROR_SSL:
    syslog(LOG_ERR, "SSL_ERROR_SSL (%d): TLS protocol error (%s)", error_code, ERR_error_string(err, NULL));
    break;

  default:
    syslog(LOG_ERR, "UNKNOWN SSL ERROR (%d): %s", error_code, ERR_error_string(err, NULL));
    break;
  } //switch
}


/*! @brief Standalone console listener and its single service thread. */
struct UfsrvCommandConsole
{
  Socket *listener;              ///< Listening socket owned by the service.
  SSL_CTX *tls;                   ///< TLS context used by accepted connections.
  ConsoleCommandHandler handler;  ///< Host command resolver.
  pthread_t thread;               ///< Joinable TLS listener thread.
  pthread_mutex_t mutex;          ///< Protects shutdown and active descriptor handoff.
  bool stopping;                  ///< Shutdown requested by the host.
  int client_fd;                  ///< Active socket, or -1 between connections.
};

static int
sAcceptPinnedCertificate(int verified, X509_STORE_CTX *certificate)
{
  (void)verified;
  (void)certificate;
  // Authentication is completed by the shared SPKI allow/deny check.
  return 1;
}

static char *
sConsoleCredentialPath(const char *file)
{
  if (!file || !*file) return NULL;
  if (*file == '/') return strdup(file);
  char *path = NULL;
  if (asprintf(&path, "%s/etc/%s", UfsrvConfigConfigDir(), file) < 0) return NULL;
  return path;
}

static SSL_CTX *
sStandaloneConsoleTls(void)
{
  SSL_CTX *tls = SSL_CTX_new(TLS_server_method());
  if (!tls) return NULL;

  char *chain = sConsoleCredentialPath(UfsrvConfigSslCommandConsoleCertificateFile());
  char *key = sConsoleCredentialPath(UfsrvConfigSslCommandConsoleKeyFile());
  const bool loaded = chain && key && SSL_CTX_set_min_proto_version(tls, TLS1_2_VERSION) &&
                      SSL_CTX_use_certificate_chain_file(tls, chain) == 1 &&
                      SSL_CTX_use_PrivateKey_file(tls, key, SSL_FILETYPE_PEM) == 1 &&
                      SSL_CTX_check_private_key(tls) == 1;
  free(chain);
  free(key);
  if (!loaded) {
    syslog(LOG_ERR, "Could not load command console TLS credentials: %s",
           ERR_error_string(ERR_get_error(), NULL));
    SSL_CTX_free(tls);
    return NULL;
  }

  if (UfsrvConfigSslConsoleIsClientCertificateRequired()) {
    SSL_CTX_set_verify(tls, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       sAcceptPinnedCertificate);
  }
  return tls;
}

static void
sServeConsoleConnection(UfsrvCommandConsole *console, int fd)
{
  const struct timeval timeout = {.tv_sec = 5};
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) return;

  SSL *ssl = SSL_new(console->tls);
  if (!ssl) return;
  if (SSL_set_fd(ssl, fd) == 1 && SSL_accept(ssl) == 1 &&
      (!UfsrvConfigSslConsoleIsClientCertificateRequired() || sConsolePeerAllowed(ssl))) {
    char line[COMMAND_CONSOLE_READ_BUF_SIZE + 1];
    size_t used = 0;
    while (used < COMMAND_CONSOLE_READ_BUF_SIZE) {
      int received = SSL_read(ssl, line + used, COMMAND_CONSOLE_READ_BUF_SIZE - (int)used);
      if (received <= 0) break;
      char *end = memchr(line + used, '\n', (size_t)received);
      if (end) {
        sDispatchCommand(console->handler, ssl, fd, line, (int)(end - line));
        break;
      }
      used += (size_t)received;
    }
    SSL_shutdown(ssl);
  }
  SSL_free(ssl);
}

static void *
sStandaloneConsoleThread(void *context)
{
  UfsrvCommandConsole *console = context;
  sigset_t blocked;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &blocked, NULL);

  struct pollfd listener = {.fd = console->listener->sock, .events = POLLIN};
  for (;;) {
    const int ready = poll(&listener, 1, 100);
    pthread_mutex_lock(&console->mutex);
    if (console->stopping) {
      pthread_mutex_unlock(&console->mutex);
      break;
    }
    if (ready <= 0 || !(listener.revents & POLLIN)) {
      pthread_mutex_unlock(&console->mutex);
      continue;
    }

    int fd = accept(listener.fd, NULL, NULL);
    console->client_fd = fd;
    pthread_mutex_unlock(&console->mutex);
    if (fd < 0) continue;

    sServeConsoleConnection(console, fd);
    pthread_mutex_lock(&console->mutex);
    close(fd);
    console->client_fd = -1;
    pthread_mutex_unlock(&console->mutex);
  }
  return NULL;
}

UfsrvCommandConsole *
UfsrvCommandConsoleStart(ConsoleCommandHandler handler)
{
  if (!handler || !UfsrvConfigSslCommandConsoleIsEnabled()) return NULL;
  UfsrvCommandConsole *console = calloc(1, sizeof(*console));
  if (!console) return NULL;
  console->handler = handler;
  console->client_fd = -1;

  if (pthread_mutex_init(&console->mutex, NULL) != 0) {
    free(console);
    return NULL;
  }
  console->tls = sStandaloneConsoleTls();
  if (console->tls) console->listener = SetupCommandConsole();
  if (console->listener) {
    const int fd = console->listener->sock;
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
        pthread_create(&console->thread, NULL, sStandaloneConsoleThread, console) == 0)
      return console;
  }

  if (console->listener) close(console->listener->sock);
  free(console->listener);
  SSL_CTX_free(console->tls);
  pthread_mutex_destroy(&console->mutex);
  free(console);
  return NULL;
}

void
UfsrvCommandConsoleStop(UfsrvCommandConsole *console)
{
  if (!console) return;
  pthread_mutex_lock(&console->mutex);
  console->stopping = true;
  shutdown(console->listener->sock, SHUT_RDWR);
  if (console->client_fd >= 0) shutdown(console->client_fd, SHUT_RDWR);
  pthread_mutex_unlock(&console->mutex);
  pthread_join(console->thread, NULL);

  close(console->listener->sock);
  free(console->listener);
  SSL_CTX_free(console->tls);
  pthread_mutex_destroy(&console->mutex);
  free(console);
}
