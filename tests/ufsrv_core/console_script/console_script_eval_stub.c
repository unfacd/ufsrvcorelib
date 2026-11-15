/**
 * @file console_script_eval_stub.c
 * @brief Test doubles for the two evaluator entry points the resolver calls.
 *
 * The suite exercises path matching and handler dispatch only. Pulling in
 * ufsrv_console_script_eval.c would drag the Lua VM and the JSON path engine
 * into a test of the trie, so the three evaluator symbols the resolver calls
 * are stubbed here: EVAL and pipelines return nothing, and a path expression is
 * a pass-through.
 */

#include <uflib/buffer_descriptor/buffer_descriptor.h>

BufferDescriptor *
UfsrvConsoleScriptEvalLua(const char *script, BufferDescriptor *buffer)
{
	(void)script;
	(void)buffer;
	return NULL;
}

BufferDescriptor *
UfsrvConsoleScriptSelectPath(const char *jsonpath, const char *node_output, BufferDescriptor *buffer)
{
	(void)jsonpath;
	(void)node_output;
	return buffer;
}

BufferDescriptor *
UfsrvConsoleScriptEvalPipeline(const char *pipeline, const char *node_output, BufferDescriptor *buffer)
{
	(void)pipeline;
	(void)node_output;
	return buffer;
}
