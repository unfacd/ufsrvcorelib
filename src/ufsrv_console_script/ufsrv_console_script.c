/**
 * @file ufsrv_console_script.c
 * @brief command-console dynamic registration + dispatch (G3: resolver)
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <ufsrvcorelib/ufsrv_console_script/ufsrv_console_script.h>

#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>

#include "ufsrv_console_script_priv.h"

#define UFSRV_CONSOLE_NODE_PATH_MAX 256

//Internal trie node — one per path segment. The root (s_root) has a NULL segment.
typedef struct UfsrvConsoleScriptNode
{
  char *segment;                             //owned copy; NULL for the root
  char *description;                         //owned copy; NULL if none
  UfsrvConsoleScriptHandler handler;         //copied descriptor (zero for auto-created groups)
  struct UfsrvConsoleScriptNode **children;  //sorted by segment
  size_t n_children;
  size_t cap_children;
} UfsrvConsoleScriptNode;

static UfsrvConsoleScriptNode s_root;  //zero-initialised (static storage)

//forward declarations for the mutually-recursive info emitters
static BufferDescriptor *sEmitNodeInfo(const UfsrvConsoleScriptNode *node, BufferDescriptor *buffer);
static BufferDescriptor *sEmitLeafMetadata(const UfsrvConsoleScriptNode *node, BufferDescriptor *buffer);
static BufferDescriptor *sEmitGroupInfo(const UfsrvConsoleScriptNode *node, BufferDescriptor *buffer);

//Compare a length-limited segment against a NUL-terminated child segment.
static int
sSegmentCmp(const char *segment, size_t len, const char *child_segment)
{
	size_t child_len = strlen(child_segment);
	size_t min_len = len < child_len ? len : child_len;
	int cmp = memcmp(segment, child_segment, min_len);
	if (cmp) {
		return cmp;
	}
	if (len < child_len) {
		return -1;
	}
	if (len > child_len) {
		return 1;
	}
	return 0;
}

//Find the child named [segment, segment+len), or NULL.
static UfsrvConsoleScriptNode *
sFindChild(UfsrvConsoleScriptNode *node, const char *segment, size_t len)
{
	size_t i;
	for (i = 0; i < node->n_children; i++) {
		if (sSegmentCmp(segment, len, node->children[i]->segment) == 0) {
			return node->children[i];
		}
	}
	return NULL;
}

//Find the child named [segment, segment+len), or create it (sorted insert).
static UfsrvConsoleScriptNode *
sFindOrCreateChild(UfsrvConsoleScriptNode *node, const char *segment, size_t len)
{
	size_t i, pos;
	UfsrvConsoleScriptNode *child;

	child = sFindChild(node, segment, len);
	if (child) {
		return child;
	}

	child = calloc(1, sizeof(*child));
	if (!child) {
		return NULL;
	}
	child->segment = strndup(segment, len);
	if (!child->segment) {
		free(child);
		return NULL;
	}

	if (node->n_children == node->cap_children) {
		size_t new_cap = node->cap_children ? node->cap_children * 2 : 4;
		UfsrvConsoleScriptNode **grow = realloc(node->children, new_cap * sizeof(UfsrvConsoleScriptNode *));
		if (!grow) {
			free(child->segment);
			free(child);
			return NULL;
		}
		node->children = grow;
		node->cap_children = new_cap;
	}

	for (pos = 0; pos < node->n_children; pos++) {
		if (sSegmentCmp(segment, len, node->children[pos]->segment) < 0) {
			break;
		}
	}
	memmove(&node->children[pos + 1], &node->children[pos],
	        (node->n_children - pos) * sizeof(UfsrvConsoleScriptNode *));
	node->children[pos] = child;
	node->n_children++;

	return child;
}

//Last-wins replace of the handler + description at the final node.
static int
sReplaceHandler(UfsrvConsoleScriptNode *node, const char *description, const UfsrvConsoleScriptHandler *handler)
{
	bool desc_changed =
		(node->description && !description) ||
		(!node->description && description) ||
		(node->description && description && strcmp(node->description, description) != 0);

	//handler: last-wins, no-op when memcmp-identical
	if (memcmp(&node->handler, handler, sizeof(*handler)) != 0) {
		node->handler = *handler;
	}

	if (desc_changed) {
		char *dup = description ? strdup(description) : NULL;
		if (description && !dup) {
			return -1;
		}
		free(node->description);
		node->description = dup;
	}

	return 0;
}

//Recursively free a heap node and all its descendants.
static void
sDestroyNode(UfsrvConsoleScriptNode *node)
{
	size_t i;
	for (i = 0; i < node->n_children; i++) {
		sDestroyNode(node->children[i]);
	}
	free(node->children);
	free(node->segment);
	free(node->description);
	free(node);
}

//Map a method bit to its string name.
static const char *
sMethodName(unsigned m)
{
	switch (m) {
	case UFSRV_CONSOLE_METHOD_GET:  return "GET";
	case UFSRV_CONSOLE_METHOD_DEL:  return "DEL";
	case UFSRV_CONSOLE_METHOD_PUT:  return "PUT";
	default:                        return "?";
	}
}

//Map a method token (case-insensitive) to a UfsrvConsoleMethod bit, or -1.
static int
sMethodFromString(const char *method)
{
	if (!method) {
		return -1;
	}
	if (!strcasecmp(method, "GET")) {
		return UFSRV_CONSOLE_METHOD_GET;
	}
	if (!strcasecmp(method, "DEL") || !strcasecmp(method, "DELETE")) {
		return UFSRV_CONSOLE_METHOD_DEL;
	}
	if (!strcasecmp(method, "PUT")) {
		return UFSRV_CONSOLE_METHOD_PUT;
	}
	if (!strcasecmp(method, "EVAL")) {
		return UFSRV_CONSOLE_METHOD_EVAL;
	}
	return -1;
}

//Return @p buffer, or allocate + init one when NULL.
static BufferDescriptor *
sBufferGet(BufferDescriptor *buffer)
{
	if (buffer) {
		return buffer;
	}
	buffer = calloc(1, sizeof(BufferDescriptor));
	if (!buffer) {
		return NULL;
	}
	BufferDescriptorInit(buffer, 256);
	return buffer;
}

//Emit a leaf node's metadata: name, description, advertised methods.
static BufferDescriptor *
sEmitLeafMetadata(const UfsrvConsoleScriptNode *node, BufferDescriptor *buffer)
{
	bool first = true;
	unsigned m;

	buffer = sBufferGet(buffer);
	if (!buffer) {
		return NULL;
	}

	BufferDescriptorAppendFormatted(buffer, "{\"name\":\"%s\",\"description\":\"%s\",\"methods\":[",
		node->segment ? node->segment : "",
		node->description ? node->description : "");

	for (m = UFSRV_CONSOLE_METHOD_GET; m <= UFSRV_CONSOLE_METHOD_PUT; m <<= 1) {
		if (node->handler.methods & m) {
			BufferDescriptorAppendFormatted(buffer, "%s\"%s\"", first ? "" : ",", sMethodName(m));
			first = false;
		}
	}
	BufferDescriptorAppendFormatted(buffer, "]}");
	return buffer;
}

//Emit a group node's info: enumerate children and aggregate their info.
static BufferDescriptor *
sEmitGroupInfo(const UfsrvConsoleScriptNode *node, BufferDescriptor *buffer)
{
	bool first = true;
	size_t i;

	buffer = sBufferGet(buffer);
	if (!buffer) {
		return NULL;
	}

	BufferDescriptorAppendFormatted(buffer, "{");
	for (i = 0; i < node->n_children; i++) {
		UfsrvConsoleScriptNode *child = node->children[i];
		BufferDescriptor child_buf = {0};
		BufferDescriptor *child_info;

		if (!first) {
			BufferDescriptorAppendFormatted(buffer, ",");
		}
		first = false;

		BufferDescriptorAppendFormatted(buffer, "\"%s\":", child->segment);

		BufferDescriptorInit(&child_buf, 256);
		child_info = sEmitNodeInfo(child, &child_buf);
		if (child_info && child_info->data && child_info->size > 0) {
			BufferDescriptorAppendFormatted(buffer, "%s", child_info->data);
		} else {
			BufferDescriptorAppendFormatted(buffer, "null");
		}
		BufferDescriptorRelease(&child_buf);
	}
	BufferDescriptorAppendFormatted(buffer, "}");
	return buffer;
}

//Emit a node's info: group walk when it has children, else leaf metadata.
static BufferDescriptor *
sEmitNodeInfo(const UfsrvConsoleScriptNode *node, BufferDescriptor *buffer)
{
	if (node->n_children > 0) {
		return sEmitGroupInfo(node, buffer);
	}
	return sEmitLeafMetadata(node, buffer);
}

//Walk the trie for @p path.
//
//A node flagged is_stemmed is a sub-router: the walk stops the moment it is
//reached and everything below it belongs to that node's handler, so any node
//registered underneath is shadowed by design. @p remainder then points at the
//text just past the stem's own segment ("" when the stem itself was addressed),
//still inside the caller's path string. For a non-stemmed path @p remainder is
//NULL and @p node_out is the exact node.
static bool
sWalk(const char *path, UfsrvConsoleScriptNode **node_out, const char **remainder)
{
	const char *p = path;
	UfsrvConsoleScriptNode *node = &s_root;

	while (*p == '/') {
		p++;
	}
	if (!*p) {
		return false;//empty path
	}

	while (*p) {
		const char *end = p;
		while (*end && *end != '/') {
			end++;
		}
		if (end == p) {
			p = end;
			while (*p == '/') {
				p++;
			}
			continue;//skip empty segment
		}

		node = sFindChild(node, p, (size_t)(end - p));
		if (!node) {
			return false;//no such path
		}

		p = end;
		while (*p == '/') {
			p++;
		}

		if (node->handler.options.is_stemmed) {
			*node_out = node;
			*remainder = p;//"" when the stem itself was addressed
			return true;
		}
	}

	*node_out = node;
	*remainder = NULL;//exact, non-stemmed match
	return true;
}

PUBLIC_API int
UfsrvConsoleScriptRegister(const char *path, const char *description, const UfsrvConsoleScriptHandler *handler)
{
	const char *p;
	UfsrvConsoleScriptNode *node;

	if (!path || !handler) {
		return -1;
	}

	p = path;
	while (*p == '/') {
		p++;
	}
	if (!*p) {
		return -1;//empty path (or just '/')
	}

	node = &s_root;
	while (*p) {
		const char *end = p;
		while (*end && *end != '/') {
			end++;
		}
		if (end == p) {
			p = end;
			while (*p == '/') {
				p++;
			}
			continue;//skip empty segment
		}

		node = sFindOrCreateChild(node, p, (size_t)(end - p));
		if (!node) {
			return -1;
		}
		p = end;
		while (*p == '/') {
			p++;
		}
	}

	return sReplaceHandler(node, description, handler);
}

PUBLIC_API void
UfsrvConsoleScriptDestroy(void)
{
	size_t i;
	for (i = 0; i < s_root.n_children; i++) {
		sDestroyNode(s_root.children[i]);
	}
	free(s_root.children);
	s_root.children = NULL;
	s_root.n_children = 0;
	s_root.cap_children = 0;
}

//Apply the path expression (jsonpath / ->eval) to the dispatched method's output.
static BufferDescriptor *
sApplyPathExpression(BufferDescriptor *out, const char *path_expression)
{
	if (!out || !path_expression || !*path_expression) {
		return out;//no path expression
	}
	if (strstr(path_expression, "->eval")) {
		return UfsrvConsoleScriptEvalPipeline(path_expression, out->data ? out->data : "", out);
	}
	return UfsrvConsoleScriptSelectPath(path_expression, out->data ? out->data : "", out);
}

static BufferDescriptor *
sInvokeHandler(BufferDescriptor *(*callback)(ScriptHandlerContext *),
               const char *stemmed_path, ScriptHandlerUserData *user_data,
               BufferDescriptor *buffer)
{
	if (!callback) {
		return NULL;
	}

	ScriptHandlerContext ctx = {
		.stemmed_path = stemmed_path,
		.user_data    = user_data,
		.buffer       = buffer,
	};

	return callback(&ctx);
}

PUBLIC_API BufferDescriptor *
UfsrvConsoleScriptResolve(const char *method, const char *path, BufferDescriptor *buffer)
{
	const char *path_expression = NULL;
	const char *node_path = path;
	char node_buf[UFSRV_CONSOLE_NODE_PATH_MAX];
	const char *colon;
	UfsrvConsoleScriptNode *node;
	int m;

	if (!method || !path) {
		return NULL;
	}

	m = sMethodFromString(method);
	if (m < 0) {
		return NULL;//405: unknown method
	}

	//standalone EVAL: @p path IS the lua script (rest of line) — no ':' split, no node lookup
	if (m == UFSRV_CONSOLE_METHOD_EVAL) {
		return UfsrvConsoleScriptEvalLua(path, buffer);
	}

	//split "node_path[:pipeline]" on the first ':'
	colon = strchr(path, ':');
	if (colon) {
		size_t len = (size_t)(colon - path);
		if (len >= sizeof(node_buf)) {
			return NULL;//node path too long
		}
		memcpy(node_buf, path, len);
		node_buf[len] = '\0';
		node_path = node_buf;
		path_expression = colon + 1;
	}

	//@p remainder is NULL for an exact, non-stemmed match; for a stemmed node it
	//is "" (stem addressed directly) or the text after the stem.
	const char *remainder = NULL;

	if (!sWalk(node_path, &node, &remainder)) {
		return NULL;//404: unknown path
	}

	switch (m) {
	case UFSRV_CONSOLE_METHOD_GET: {
		BufferDescriptor *out;
		if (node->handler.on_get) {
			out = sInvokeHandler(node->handler.on_get, remainder, node->handler.user_data, buffer);
		} else if (remainder == NULL && node->n_children > 0) {
			out = sEmitGroupInfo(node, buffer);
		} else {
			out = NULL;
		}
		return sApplyPathExpression(out, path_expression);
	}

	case UFSRV_CONSOLE_METHOD_DEL: {
		BufferDescriptor *out = sInvokeHandler(node->handler.on_del, remainder, node->handler.user_data, buffer);
		return sApplyPathExpression(out, path_expression);
	}

	case UFSRV_CONSOLE_METHOD_PUT: {
		BufferDescriptor *out = sInvokeHandler(node->handler.on_put, remainder, node->handler.user_data, buffer);
		return sApplyPathExpression(out, path_expression);
	}

	default:
		return NULL;
	}
}
