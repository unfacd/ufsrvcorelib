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

#include <uflib/buffer_descriptor/buffer_descriptor.h>
#include <uflib/standard_c_includes.h>
#include <uflib/standard_net_includes.h>
#include <ufsrvcorelib/ufsrv_core/instrumentation/instrumentation_backend.h>


static int 	should_send(float sample_rate);
static void statsd_prepare(InstrumentationBackend *link, char *stat_name, const char *delta_sign, ssize_t stat_value, const char *type, float sample_rate, char *message, size_t buflen, int lf);
static int 	send_stat(InstrumentationBackend *link, char *stat_name, const char *delta_sign, ssize_t stat_value, const char *type, float sample_rate, struct sockaddr_in *instrumentation_backend_server);
/* will change the original string */
static void cleanup(char *stat);

/**
 * Provides a low level address resolution of the instrumentation backend server hostname (ips, or domain name).
 * This allows for reuse of that address translation across other users of the destination backend server (statsd).
 * @param host domain name or ip address of the target server
 * @param port service port number. diagnostic purposes only
 * @param instrumentation_backend_server a shared structure to hold address translation.
 * @return 1 on success
 */
PUBLIC_API int
InstrumentationBackendServerInit(const char *host, int port, struct sockaddr_in *instrumentation_backend_server)
{
	memset(instrumentation_backend_server, 0, sizeof(struct sockaddr_in));
	instrumentation_backend_server->sin_family = AF_INET;
	instrumentation_backend_server->sin_port = htons(port);

	struct addrinfo *result = NULL, hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	int error;
	if ((error = getaddrinfo(host, NULL, &hints, &result))) {
		syslog(LOG_ERR, "InstrumentationBackendServerInit (error: '%d'): COULD NOT invoke 'getaddrinfo' on BackendInstrumentation....", error);
		return errno;
	}

	memcpy(&instrumentation_backend_server->sin_addr, &((struct sockaddr_in*)result->ai_addr)->sin_addr, sizeof(struct in_addr));
	freeaddrinfo(result);

	syslog(LOG_ERR, "InstrumentationBackendServerInit SUCCESSFULLY INITIALISED InstrumentationBackendServer %s:%d....", host, port);

    return 1;

}

/**
 * This is a direct socket with the InstrumentationBackend server, specific to each user, along with pre-translated server
 * address in InstrumentationBackendServer() they are used to send UDP packets to the instrumentation server.
 * Typically, each thread will retain a local copy of this for its direct comm with the server.
 * @param ns Name space specific to the caller used in packets originating from this caller.
 * @param instrum_backend_provided[IN] Optionally, pre-allocated structure by user to persist relevant information.
 * @param server_class Additional fixed identifying information to log into the packets
 * @param serverid_by_uservoid additional fixed identifying information to log into the packets
 * @return User allocated, or dynamic allocation of the server structure.
 * @dynamic_memory EXPORTS InstrumentationBackend * if not provided by user
 */
PUBLIC_API InstrumentationBackend *
InstrumentationBackendInit(const char * _Nullable ns, InstrumentationBackend * _Nullable instrum_backend_provided, const char * _Nullable server_class, int serverid_by_uservoid)
{
  InstrumentationBackend *instrum_backend = NULL;

  if (IS_PRESENT(instrum_backend_provided)) instrum_backend = instrum_backend_provided;
  else instrum_backend = calloc(1, sizeof(InstrumentationBackend));

	if ((instrum_backend->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		syslog(LOG_ERR, "InstrumentationBackendInit COULD NOT create socket for BackendInstrumentation....");

		if (IS_EMPTY(instrum_backend_provided)) free(instrum_backend);
		return NULL;
	}

	//TODO consider connecting the UDP socket: A UDP socket in the connected state will only receive datagrams that originate from the given remote address. It is therefore feasible to use functions such as read or recv in place of recvfrom. Similarly the given remote address becomes the default for outgoing datagrams, therefore it is feasible to use write or send in place of sendto.
  //if (connect(instrum_backend->socket, (struct sockaddr *) &masterptr->instrumentation_backend_server, sizeof(masterptr->instrumentation_backend_server)) == -1) {
    //
  //}
	if (IS_STR_LOADED(ns)) {
		strncpy(instrum_backend->name_space, ns, SBUF-1);
	} else {
		snprintf(instrum_backend->name_space, SBUF-1, "%s-%d", server_class, serverid_by_uservoid);
	}

	return instrum_backend;

}

PUBLIC_API void
InstrumentationBackendReset(InstrumentationBackend *link)
{
  if (link->socket != -1) {
    close(link->socket);
    link->socket = -1;
  }

  free(link);
}

static int should_send(float sample_rate)
{
  if (sample_rate < 1.0) {
      float p = ((float)random() / (float)RAND_MAX);
      return sample_rate > p;
  } else {
      return 1;
  }
}

/* will change the original string */
__attribute__((unused)) static void
cleanup(char *stat_name)
{
    char *p;
    for (p = stat_name; *p; p++) {
        if (*p == ':' || *p == '|' || *p == '@') {
            *p = '_';
        }
    }
}

//inventory:+2|g
static void
statsd_prepare(InstrumentationBackend *link, char *stat_name, const char *delta_sign, ssize_t stat_value, const char *type, float sample_rate, char *message, size_t buflen, int lf)
{
  //save some cycles. This is invoked in a controlled environment: we never include invalid characters in stat names
  //cleanup(stat_name);

  if (likely(sample_rate == 1.0)) {
      snprintf(message, buflen, "%s.%s:%s%zd|%s%s", link->name_space, stat_name, delta_sign, stat_value, type, lf ? "\n" : "");
  } else {
      snprintf(message, buflen, "%s.%s:%s%zd|%s|@%.2f%s", link->name_space, stat_name, delta_sign, stat_value, type, sample_rate, lf ? "\n" : "");
  }
}

static int
send_stat(InstrumentationBackend *link, char *stat_name, const char *delta_sign, ssize_t stat_value, const char *type, float sample_rate, struct sockaddr_in *instrumentation_backend_server)
{
  char message[SMBUF];
  if (!should_send(sample_rate)) {
    return 0;
  }

  statsd_prepare(link, stat_name, delta_sign, stat_value, type, sample_rate, message, SMBUF, 1);

  return statsd_send(link, message, instrumentation_backend_server);
}

PUBLIC_API int
statsd_send(InstrumentationBackend *link, const char *message, struct sockaddr_in *instrumentation_backend_server)
{
  int slen = sizeof(*instrumentation_backend_server);

  //TODO consider connecting the socket. See comment above about connection UDP sockets
  if (sendto(link->socket, message, strlen(message), 0, (struct sockaddr *) instrumentation_backend_server, slen) == -1) {
    perror("sendto");
    return -1;
  }

  return 0;
}

/* public interface */
PUBLIC_API int
statsd_count(InstrumentationBackend *link, char *stat_name, ssize_t stat_value, float sample_rate, struct sockaddr_in *instrumentation_backend_server)
{
    return send_stat(link, stat_name, "", stat_value, "c", sample_rate, instrumentation_backend_server);
}

PUBLIC_API int
statsd_dec(InstrumentationBackend *link, char *stat_name, float sample_rate, struct sockaddr_in *instrumentation_backend_server)
{
    return statsd_count(link, stat_name, -1, sample_rate, instrumentation_backend_server);
}

PUBLIC_API int
statsd_inc(InstrumentationBackend *link, char *stat_name, float sample_rate, struct sockaddr_in *instrumentation_backend_server)
{
    return statsd_count(link, stat_name, 1, sample_rate, instrumentation_backend_server);
}

/**
 *  supports deltas as per statsite server:
 *  inventory:100|g
		inventory:-5|g
		inventory:+2|g
 */
PUBLIC_API int
statsd_gauge(InstrumentationBackend *link, char *stat_name, ssize_t value, struct sockaddr_in *instrumentation_backend_server)
{
    return send_stat(link, stat_name, "", value, "g", 1.0, instrumentation_backend_server);
}

PUBLIC_API int
statsd_gauge_inc(InstrumentationBackend *link, char *stat_name, ssize_t value, struct sockaddr_in *instrumentation_backend_server)
{
    return send_stat(link, stat_name, "+", value, "g", 1.0, instrumentation_backend_server);
}

PUBLIC_API int
statsd_gauge_dec(InstrumentationBackend *link, char *stat_name, ssize_t value, struct sockaddr_in *instrumentation_backend_server)
{
    return send_stat(link, stat_name, "-", value, "g", 1.0, instrumentation_backend_server);
}

//inventory:+2|g
PUBLIC_API int
statsd_timing(InstrumentationBackend *link, char *stat_name, ssize_t ms, struct sockaddr_in *instrumentation_backend_server)
{
    return send_stat(link, stat_name, "", ms, "ms", 1.0, instrumentation_backend_server);
}

PUBLIC_API BufferDescriptor *
InstrumentationBackendAsString(const InstrumentationBackend *backend, BufferDescriptor *buffer_provided)
{
	if (IS_PRESENT(backend) && IS_PRESENT(buffer_provided)) {
		if (BufferDescriptorAppendFormatted(buffer_provided, "socket:\"%d\", name_space:\"%s\"", backend->socket, backend->name_space) != 0) {
			return buffer_provided;
		}
	}
	return NULL;
}
