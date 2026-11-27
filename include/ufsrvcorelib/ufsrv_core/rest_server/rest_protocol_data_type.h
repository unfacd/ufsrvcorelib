/**
 * @file rest_protocol_data_type.h
 * @brief Application context of one REST protocol slot.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_REST_PROTOCOL_DATA_TYPE_H
#define UFSRVCORELIB_REST_PROTOCOL_DATA_TYPE_H

#include <stddef.h>

#include <ufsrvcorelib/ufsrv_core/http/utils_protocol_http.h>
#include <ufsrvcorelib/ufsrv_core/rest_server/rest_server.h>
#include <ufsrvcorelib/ufsrv_core/rest_server/rest_session_type.h>

typedef struct RestProtocolData
{
  UfsrvRestServer *server;        ///< Bound listen sockets and their worker threads
  ProtocolHttp *   http;          ///< Route table with the auth and error handlers
  RestSession *    sessions;      ///< worker_count objects; index i belongs to worker i
  size_t           worker_count;  ///< One session and one thread per worker
} RestProtocolData;

#endif //UFSRVCORELIB_REST_PROTOCOL_DATA_TYPE_H
