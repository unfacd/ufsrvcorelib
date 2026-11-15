/**
 * @file ufsrv_console_script_eval.c
 * @brief command-console EVAL: ->eval pipeline + sandboxed Lua engine (ufsrvscriptlib)
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

#include "ufsrv_console_script_priv.h"
#include <ufsrvscriptlib/ufsrvscriptlib_eval.h>
#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>
#include <csonpath_json-c.h>

//Per-thread persistent sandboxed VM (ufsrvscriptlib). Created once and never
//closed — one per console connection thread, so no locking (design §10). The
//default sandbox policy applies (strict globals on, os/package off).
static _Thread_local UfsrvScriptLua *s_lua;

static UfsrvScriptLua *
sLua(void)
{
	if (!s_lua) {
		s_lua = UfsrvScriptEvalOpen(NULL);
	}
	return s_lua;
}

//Append a JSON error object, replacing any existing buffer contents. Used only
//for pipeline parse/selection errors — Lua errors are handled by UfsrvScriptEvalRun.
static BufferDescriptor *
sEmitError(BufferDescriptor *buffer, const char *msg)
{
	if (!buffer) {
		buffer = calloc(1, sizeof(BufferDescriptor));
		if (!buffer) {
			return NULL;
		}
		BufferDescriptorInit(buffer, 256);
	} else {
		buffer->size = 0;
		if (buffer->data) {
			buffer->data[0] = '\0';
		}
	}
	BufferDescriptorAppendFormatted(buffer, "{\"error\":\"%s\"}", msg ? msg : "unknown error");
	return buffer;
}

//JSON path selection via csonpath (full JSONPath: wildcards, filters, unions, …).
//Returns a malloc'd JSON string of the selected value, or NULL on error/no match.
static char *
sJsonPathSelect(const char *json_str, const char *path)
{
	json_object *root = json_tokener_parse(json_str);
	struct csonpath *cjp;
	json_object *sel;
	const char *text;
	char *out;

	if (!root) {
		return NULL;
	}

	cjp = csonpath_new(path);
	if (!cjp) {
		json_object_put(root);
		return NULL;
	}

	sel = csonpath_find_first(cjp, root);
	text = sel ? json_object_to_json_string_ext(sel, JSON_C_TO_STRING_PLAIN) : NULL;
	out = text ? strdup(text) : NULL;

	csonpath_destroy(cjp);
	json_object_put(root);
	return out;
}

//Replace every "$<" in @p body with @p value. Caller frees the result.
static char *
sSubstitute(const char *body, const char *value)
{
	size_t vlen = strlen(value);
	size_t n = 0;
	const char *p = body;
	char *out, *o;

	while ((p = strstr(p, "$<")) != NULL) {
		n++;
		p += 2;
	}
	//upper bound (over-allocates by 2n, safe)
	out = malloc(strlen(body) + n * vlen + 1);
	if (!out) {
		return NULL;
	}

	o = out;
	p = body;
	while (*p) {
		if (p[0] == '$' && p[1] == '<') {
			memcpy(o, value, vlen);
			o += vlen;
			p += 2;
		} else {
			*o++ = *p++;
		}
	}
	*o = '\0';
	return out;
}

//Evaluate a "jsonpath? ->eval(lua_body)" pipeline against @p node_output.
static BufferDescriptor *
sEvalPipeline(const char *pipeline, const char *node_output, BufferDescriptor *buffer)
{
	const char *arrow = strstr(pipeline, "->");
	const char *eval_part;
	const char *open, *close;
	char *value = NULL;
	char *body = NULL;
	char *script = NULL;
	BufferDescriptor *r;

	if (!arrow) {
		return sEmitError(buffer, "malformed pipeline (missing '->')");
	}

	//jsonpath selection (before "->"), or the full output when empty
	if (arrow != pipeline) {
		size_t jlen = (size_t)(arrow - pipeline);
		char *jpath = strndup(pipeline, jlen);
		if (!jpath) {
			return sEmitError(buffer, "out of memory");
		}
		value = sJsonPathSelect(node_output, jpath);
		free(jpath);
		if (!value) {
			return sEmitError(buffer, "json path selection failed");
		}
	} else {
		value = strdup(node_output ? node_output : "null");
		if (!value) {
			return sEmitError(buffer, "out of memory");
		}
	}

	eval_part = arrow + 2;  //skip "->"
	if (strncmp(eval_part, "eval(", 5) != 0) {
		free(value);
		return sEmitError(buffer, "malformed pipeline (expected 'eval(')");
	}

	open = eval_part + 5;        //after "eval("
	close = strrchr(open, ')');  //last ')'
	if (!close) {
		free(value);
		return sEmitError(buffer, "malformed pipeline (missing ')')");
	}
	body = strndup(open, (size_t)(close - open));
	if (!body) {
		free(value);
		return sEmitError(buffer, "out of memory");
	}
	script = sSubstitute(body, value);
	free(body);
	free(value);
	if (!script) {
		return sEmitError(buffer, "out of memory");
	}

	//run the substituted script in the sandboxed VM (truncates + rewrites buffer)
	r = UfsrvScriptEvalRun(sLua(), script, buffer);
	free(script);
	return r;
}

BufferDescriptor *
UfsrvConsoleScriptEvalLua(const char *script, BufferDescriptor *buffer)
{
	return UfsrvScriptEvalRun(sLua(), script, buffer);
}

BufferDescriptor *
UfsrvConsoleScriptEvalPipeline(const char *pipeline, const char *node_output, BufferDescriptor *buffer)
{
	return sEvalPipeline(pipeline, node_output, buffer);
}

BufferDescriptor *
UfsrvConsoleScriptSelectPath(const char *jsonpath, const char *node_output, BufferDescriptor *buffer)
{
	char *value = sJsonPathSelect(node_output, jsonpath);

	if (!value) {
		return sEmitError(buffer, "json path selection failed");
	}

	if (!buffer) {
		buffer = calloc(1, sizeof(BufferDescriptor));
		if (!buffer) {
			free(value);
			return NULL;
		}
		BufferDescriptorInit(buffer, 256);
	} else {
		buffer->size = 0;
		if (buffer->data) {
			buffer->data[0] = '\0';
		}
	}
	BufferDescriptorAppendFormatted(buffer, "%s", value);
	free(value);
	return buffer;
}
