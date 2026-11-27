/*
 * http_request_context_type.h
 *
 *  Created on: 6 Nov 2016
 *      Author: ayman
 *
 * Moved here from ufnetcorelib: BaseThreadContext embeds this type by value,
 * so the definition has to live with the thread context rather than with the
 * HTTP layer. libcurl and json-c are therefore public dependencies of this
 * library's headers.
 */

#ifndef UFSRVCORELIB_HTTP_REQUEST_CONTEXT_TYPE_H
#define UFSRVCORELIB_HTTP_REQUEST_CONTEXT_TYPE_H

#include <json-c/json.h>
#include <curl/curl.h>

struct RawBuffer_ {
    char *memory;
    size_t size;
  };
  typedef struct RawBuffer_ RawBuffer_;

struct HttpRequestContext {
	CURL *curl;
	CURLcode curl_code;
	char curl_error_str[CURL_ERROR_SIZE];

	struct RawBuffer_ rb;

	struct json_object *jobj;
	enum json_tokener_error jerr;
	struct json_tokener *jtok;

};
typedef struct HttpRequestContext HttpRequestContext;

#endif /* UFSRVCORELIB_HTTP_REQUEST_CONTEXT_TYPE_H */
