/**
 * @file utils_json.h
 * @brief Small json-c accessors shared by every layer.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef UFSRVCORELIB_UTILS_JSON_H
#define UFSRVCORELIB_UTILS_JSON_H

#include <json-c/json.h>

/**
 * @brief Fetch a child object by key.
 * @return The child, or NULL when the key is absent or the root is not an object.
 */
static inline struct json_object *
json__get(json_object *rootObj, const char *key)
{
  struct json_object *returnObj;

  if (json_object_object_get_ex(rootObj, key, &returnObj)) return returnObj;

  return NULL;
}

#endif
