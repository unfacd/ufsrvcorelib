/**
 * @file main.c
 * @brief Minimal piped-eval demo for the ufsrvscriptlib drop-in library.
 *
 * Reads a Lua script from stdin and prints its marshalled result to stdout:
 *
 *   printf 'return 1 + 2\n'                 | ./ufsrvscriptlib_demo   # 3
 *   printf 'return string.rep("x", 4)\n'    | ./ufsrvscriptlib_demo   # xxxx
 *   printf 'error("boom")\n'                | ./ufsrvscriptlib_demo   # {"error":"…boom…"}
 *
 * UfsrvScriptEval() is the one-shot convenience: it bootstraps a sandboxed VM,
 * runs the script, marshals the single return value into a uflib BufferDescriptor
 * (nil → "null", boolean → "true"/"false", else lua_tostring), and closes the VM.
 *
 * For a long-lived VM (persistent globals) or to register the native modules
 * (json, protobuf, …), use UfsrvScriptEvalOpen() + UfsrvScriptLuaState() +
 * UfsrvScript*Register() + UfsrvScriptEvalRun() instead.
 */
#include <ufsrvscriptlib/ufsrvscriptlib.h>

#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include <stdio.h>
#include <stdlib.h>

/* Read all of stdin into a NUL-terminated, growable buffer. */
static char *
read_stdin(void)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    int ch;

    if (!buf)
        return NULL;
    while ((ch = getchar()) != EOF) {
        if (len + 1 >= cap) {
            char *grown;
            cap *= 2;
            grown = realloc(buf, cap);
            if (!grown) {
                free(buf);
                return NULL;
            }
            buf = grown;
        }
        buf[len++] = (char)ch;
    }
    buf[len] = '\0';
    return buf;
}

int
main(void)
{
    char *script = read_stdin();
    BufferDescriptor *out;

    if (!script) {
        fprintf(stderr, "ufsrvscriptlib_demo: failed to read stdin\n");
        return 1;
    }

    out = UfsrvScriptEval(script, NULL);
    free(script);
    if (!out) {
        fprintf(stderr, "ufsrvscriptlib_demo: eval failed (out of memory)\n");
        return 1;
    }

    printf("%s\n", out->data ? out->data : "");
    BufferDescriptorRelease(out);
    free(out);
    return 0;
}
