/**
 * @file message_queue_msg_payload_type.h
 * @brief Carrier record exchanged between the message-queue subscriber and a
 *        job executor.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_MESSAGE_QUEUE_MSG_PAYLOAD_TYPE_H
#define UFSRVCORELIB_MESSAGE_QUEUE_MSG_PAYLOAD_TYPE_H

#include <stddef.h>

/**
 * @brief Carrier message exchanged with the message-queue parser.
 *
 * verb and topic identify the work, delegator_type selects the worker job
 * descriptor, work_exec is the entry point, and payload carries request
 * specific data when the verb alone is not enough.
 */
struct MessageQueueMsgPayload {
  char *verb;
  char *topic;
  unsigned char *payload;
  size_t payload_sz;
  unsigned delegator_type;
  int (*work_exec)();
};
typedef struct MessageQueueMsgPayload MessageQueueMsgPayload;

#endif
