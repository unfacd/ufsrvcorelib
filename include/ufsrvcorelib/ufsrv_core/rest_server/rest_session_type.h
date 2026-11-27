//
// Created by ayman on 23/11/2026.
//

/**
 * @file rest_session_type.h
 * @brief rest_session_type
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 * Created by ayman on 23/11/2026.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_REST_SESSION_TYPE_H
#define UFSRVCORELIB_REST_SESSION_TYPE_H

#include "ufsrv_core/http/http_session.h"
#include "ufsrv_core/session/ufsrv_session_type.h"

typedef struct RestSession
{
  UfsrvSession ufsrv_session;  ///< Core session; its protocol_session_data points at http
  HttpSession  http;           ///< Exchange whose socket the worker accepts on
} RestSession;

#endif //UFSRVCORELIB_REST_SESSION_TYPE_H
