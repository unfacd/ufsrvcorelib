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

/**
 * @file transmission_message_type.h
 * @brief Payload and metadata submitted to the session transport.
 */
#ifndef UFSRVCORELIB_TRANSMISSION_MESSAGE_TYPE_H
#define UFSRVCORELIB_TRANSMISSION_MESSAGE_TYPE_H

#include <stddef.h>

enum {
  TRANSMSG_TEXT = 0,
  TRANSMSG_SOCKMSG,
  TRANSMSG_PROTOBUF
};

/*! Message descriptor for the outgoing session transport. */
typedef struct TransmissionMessage {
  unsigned type;        ///< Payload kind.
  unsigned long gid;    ///< Server-wide event ID.
  unsigned long eid;    ///< Command event ID.
  unsigned long fid;    ///< Fence ID.
  unsigned long timestamp; ///< Event timestamp.
  size_t len;           ///< Packed payload size.
  void *msg;            ///< Optional source object or SocketMessage.
  void *msg_packed;     ///< Owned packed payload transferred to the transport for text and protobuf.
} TransmissionMessage;

#endif
