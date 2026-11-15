#!/usr/bin/env python3
"""
ufsrv_console_script_generator/generate.py

Generates a server's static command-console dispatch tree + O(1) lookup table.

The *logic* (UfsrvConsoleScript type, UfsrvConsoleScriptResolve, the generic
group-node walk) lives in ufsrvcorelib; the *generated tree + table* is owned by
each consumer server (ufsrvwebsock, ufsrvapi, …) because every server exposes a
different command hierarchy.

The generator derives the node tree from the path list, emits each node as a
static UfsrvConsoleScript (children populated via the GCC flexible-array static
init extension), and emits a flat perfect-hash *pointer index* keyed by full
path for O(1) lookup.

    python3 generate.py --catalogue <server>_catalogue.py --out <generated_dir>

Catalogue format (a plain Python module):

    COMMANDS = [
        # (path, description, is_group, methods, handler_prefix)
        ("/internals",                        "ufsrv internals",     True,  ("GET",), None),
        ("/internals/config/runtime",         "runtime config",      False, ("GET",), "UfsrvConsoleInternalsConfigRuntime"),
        ...
    ]

- group node (is_group=True): on_get/on_info are the generic group walk; no
  on_del/on_put; handler_prefix is None.
- leaf node  (is_group=False): on_get/on_del/on_put/on_info come from
  <handler_prefix>OnGet / OnDel / OnPut / OnInfo.
"""

import argparse
import os
import runpy
import sys


_METHOD_SIG = {
    "GET": "BufferDescriptor *{cb}(const char *, const char *, const char *, BufferDescriptor *);",
    "DEL": "BufferDescriptor *{cb}(const char *, const char *, const char *, BufferDescriptor *);",
    "PUT": "BufferDescriptor *{cb}(const char *, const char *, const char *, BufferDescriptor *);",
}
_INFO_SIG = "BufferDescriptor *{cb}(BufferDescriptor *);"


def _hash(path, A, M):
    h = 0
    for ch in path:
        h = (h * A + ord(ch)) % M
    return h


def find_perfect_hash(paths):
    n = len(paths)
    for M in range(max(n * 2, 8), 512):
        for A in (31, 33, 37, 39, 41, 131, 133, 137, 1000003):
            used = set()
            slot_of = {}
            ok = True
            for p in paths:
                h = _hash(p, A, M)
                if h in used:
                    ok = False
                    break
                used.add(h)
                slot_of[p] = h
            if ok:
                return A, M, slot_of
    raise SystemExit("generate.py: no collision-free hash found for %d paths" % n)


def _var_name(path):
    return "node" + path.replace("/", "_")


def _short_name(path):
    return path.rstrip("/").rsplit("/", 1)[-1]


def _cb(base, method):
    return "%sOn%s" % (base, method.title())


def _build_tree(commands):
    """Return a nested dict: {segment: {subtree..., '@': entry}}."""
    tree = {}
    for entry in commands:
        path = entry[0]
        t = tree
        for seg in (s for s in path.split("/") if s):
            t = t.setdefault(seg, {})
        t["@"] = entry
    return tree


def _emit_nodes(commands):
    """Return C source: wrapper forward decls, then nodes (post-order), then wrappers."""
    tree = _build_tree(commands)
    ordered = []

    def collect(node):
        for c in sorted((k for k in node if k != "@")):
            collect(node[c])
        ordered.append(node)

    for seg in sorted(tree):
        collect(tree[seg])

    lines = []

    # 1. on_info wrapper forward declarations
    for node in ordered:
        wrapper = "sOnInfo_" + node["@"][0].strip("/").replace("/", "_")
        lines.append("static BufferDescriptor *%s(BufferDescriptor *buffer);" % wrapper)
    lines.append("")

    # 2. node definitions (post-order: children before parents)
    for node in ordered:
        path, desc, is_group, methods, prefix = node["@"]
        children = sorted((k for k in node if k != "@"))
        var = _var_name(path)
        name = _short_name(path)
        wrapper = "sOnInfo_" + path.strip("/").replace("/", "_")

        if is_group:
            child_vars = ["&" + _var_name(node[c]["@"][0]) for c in children]
            child_inits = ", ".join(child_vars + ["NULL"])
            lines.append(
                "static UfsrvConsoleScript %s = {\n"
                "  .node_name = \"%s\",\n"
                "  .node_description = \"%s\",\n"
                "  .is_group_node = true,\n"
                "  .op_methods_supported = { .is_get = 1 },\n"
                "  .ops = { .on_get = UfsrvConsoleGroupOnGet, .on_info = %s },\n"
                "  .nodes = { %s },\n"
                "};\n" % (var, name, desc, wrapper, child_inits)
            )
        else:
            method_bits = ", ".join(".is_%s = 1" % m.lower() for m in methods)
            ops_items = [".on_%s = %s" % (m.lower(), _cb(prefix, m)) for m in methods]
            ops_items.append(".on_info = %s" % wrapper)
            ops = ", ".join(ops_items)
            lines.append(
                "static UfsrvConsoleScript %s = {\n"
                "  .node_name = \"%s\",\n"
                "  .node_description = \"%s\",\n"
                "  .op_methods_supported = { %s },\n"
                "  .ops = { %s },\n"
                "};\n" % (var, name, desc, method_bits, ops)
            )

    # 3. on_info wrapper definitions (after nodes so &node resolves)
    for node in ordered:
        path, _d, is_group, _m, _p = node["@"]
        var = _var_name(path)
        wrapper = "sOnInfo_" + path.strip("/").replace("/", "_")
        info_fn = "UfsrvConsoleGroupOnInfo" if is_group else "UfsrvConsoleNodeMetadata"
        lines.append(
            "static BufferDescriptor *\n"
            "%s(BufferDescriptor *buffer)\n"
            "{\n"
            "  return %s(&%s, buffer);\n"
            "}\n" % (wrapper, info_fn, var)
        )

    return "\n".join(lines)


def _emit_forward_decls(commands):
    fwd = []
    seen = set()
    for _p, _d, is_group, methods, prefix in commands:
        if is_group:
            continue
        for m in methods:
            cb = _cb(prefix, m)
            if cb not in seen:
                seen.add(cb)
                fwd.append(_METHOD_SIG[m].format(cb=cb))
    return "\n".join(fwd)


def _emit_source(A, M, slots, commands):
    rows = []
    for entry in sorted(commands, key=lambda c: slots[c[0]]):
        path = entry[0]
        rows.append("  [%d] = { .full_path = \"%s\", .node = &%s }," % (slots[path], path, _var_name(path)))

    return (
        "/**\n"
        " * @file ufsrv_console_script_dispatch_generated.c\n"
        " * @brief generated by ufsrvcorelib generate.py — DO NOT EDIT\n"
        " *        defines the static command tree + perfect-hash dispatch table.\n"
        " */\n"
        "#include <ufsrvcorelib/ufsrv_console_script/ufsrv_console_script.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define UFSRV_CONSOLE_SCRIPT_TABLE_SIZE %d\n"
        "\n"
        "%s\n"
        "\n"
        "%s\n"
        "\n"
        "typedef struct {\n"
        "  const char *full_path;\n"
        "  const UfsrvConsoleScript *node;\n"
        "} ufsrv_console_script_entry_t;\n"
        "\n"
        "static const ufsrv_console_script_entry_t\n"
        "ufsrv_console_script_table[UFSRV_CONSOLE_SCRIPT_TABLE_SIZE] = {\n"
        "%s\n"
        "};\n"
        "\n"
        "unsigned long\n"
        "UfsrvConsoleScriptHash(const char *path)\n"
        "{\n"
        "  unsigned long h = 0;\n"
        "  while (*path) {\n"
        "    h = (h * %d + (unsigned char)*path) %% %d;\n"
        "    path++;\n"
        "  }\n"
        "  return h;\n"
        "}\n"
        "\n"
        "const UfsrvConsoleScript *\n"
        "UfsrvConsoleScriptLookup(const char *path)\n"
        "{\n"
        "  const ufsrv_console_script_entry_t *e = &ufsrv_console_script_table[UfsrvConsoleScriptHash(path)];\n"
        "  if (e->full_path && strcmp(e->full_path, path) == 0) {\n"
        "    return e->node;\n"
        "  }\n"
        "  return NULL;\n"
        "}\n"
    ) % (
        M,
        _emit_forward_decls(commands),
        _emit_nodes(commands),
        "\n".join(rows),
        A, M,
    )


def main():
    ap = argparse.ArgumentParser(description="generate a server's console dispatch tree + table")
    ap.add_argument("--catalogue", required=True, help="path to a .py module defining COMMANDS")
    ap.add_argument("--out", required=True, help="directory to write the generated .c into")
    args = ap.parse_args()

    catalogue = runpy.run_path(args.catalogue)
    commands = catalogue.get("COMMANDS")
    if not commands:
        raise SystemExit("generate.py: %s did not define COMMANDS" % args.catalogue)

    paths = [c[0] for c in commands]
    assert len(paths) == len(set(paths)), "duplicate path in COMMANDS"

    A, M, slots = find_perfect_hash(paths)

    os.makedirs(args.out, exist_ok=True)
    out_file = os.path.join(args.out, "ufsrv_console_script_dispatch_generated.c")
    with open(out_file, "w") as f:
        f.write(_emit_source(A, M, slots, commands))

    print("perfect hash: A=%d M=%d (table size %d, %d keys)" % (A, M, M, len(paths)))
    print("wrote %s" % out_file)


if __name__ == "__main__":
    main()
