#!/usr/bin/env python3
"""
ufsrv_config_verify_host — audit the HOST's joined (N>=1) composed deep-tree
output against the cold catalogues and the enforced rewrite conventions.

Exit 0 = every check passed; exit 1 = one or more gaps (each named).

Unlike ufsrv_config_verify.py (which is N=1 and coupled to the library's own
self-test layout), this verifier targets exactly the risks the HOST pipeline
introduces: the multi-library JOIN (enum renumbering, table merge, count sums)
and the deep-tree include rewrite.

Run (from the compose orchestrator, before the .composed stamp):

  python3 ufsrv_config_verify_host.py \\
      --catalogue ufsrvcorelib=.../ufsrv_config_generator_fields.c.input \\
      --catalogue ufnetcorelib=.../ufsrv_config_generator_fields.c.input \\
      --generated-private src/ufsrv_core/ufsrv_config/generated \\
      --generated-public  src/include/ufsrvcorelib/ufsrv_core/ufsrv_config/generated \\
      --invariant-dir     src/ufsrv_core/ufsrv_config
"""
import argparse
import os
import re
import sys

# ──────────────────────────────────────────────────────────────────────────────
# Text helpers
# ──────────────────────────────────────────────────────────────────────────────


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def parse_literals_header(path):
    """Parse ufsrv_config_literals_defs.h → {macro_name: literal_string}."""
    if not path or not os.path.exists(path):
        return {}
    literals = {}
    for m in re.finditer(r'#define\s+(CONF_FILE_LITERAL_\w+)\s+"([^"]*)"', read(path)):
        literals[m.group(1)] = m.group(2)
    return literals


def braced(text, start):
    """Return (inner, end) of the balanced {…} beginning at text[start]=='{'."""
    depth = 0
    i = start
    n = len(text)
    while i < n:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1 : i], i + 1
        i += 1
    return None, -1


def split_entries(body):
    """Split a top-level comma-separated {…} list into per-entry bodies."""
    entries = []
    i = 0
    n = len(body)
    while True:
        j = body.find("{", i)
        if j == -1:
            break
        inner, end = braced(body, j)
        if inner is None:
            break
        entries.append(inner)
        i = end
    return entries


def enum_suffix(name):
    """Map a catalogue field name to the generated enum identifier suffix.

    Field names are normally "snake_case" strings; three fields use the
    CONF_FILE_LITERAL_* macro as their .name, whose literal prefix is stripped.
    """
    if name.startswith("CONF_FILE_LITERAL_"):
        name = name[len("CONF_FILE_LITERAL_"):]
    return re.sub(r"[^A-Za-z0-9]+", "_", name).upper().strip("_")


def pascal_case(name):
    """Convert a catalogue field name to PascalCase (accessor suffix).

    Strips the CONF_FILE_LITERAL_ prefix for the macro-named fields, mirroring
    enum_suffix().
    """
    if name.startswith("CONF_FILE_LITERAL_"):
        name = name[len("CONF_FILE_LITERAL_"):]
    return "".join(part.capitalize() for part in name.split("_"))


# c_lang_type → generic AsString marshaller (mirrors the generator's map and the
# library verifier's GENERIC_AS_STRING_MAP).  Any other type dispatches to a
# hand-authored stub UfsrvConfig<Name>AsString_<type-suffix>.
GENERIC_AS_STRING_MAP = {
    "int": "GenericIntAsString",
    "double": "GenericDoubleAsString",
    "bool": "GenericBoolAsString",
    "char *": "GenericCharPtrAsString",
    "unsigned": "GenericUnsignedAsString",
    "unsigned char": "GenericUnsignedCharAsString",
    "size_t": "GenericSizeTAsString",
    "time_t": "GenericTimeTAsString",
    "pid_t": "GenericPidTAsString",
    "struct timeval": "GenericTimevalAsString",
    "struct timeval *": "GenericTimevalPtrAsString",
}


def as_string_type_suffix(c_lang_type):
    """Sanitize c_lang_type to the hand-authored helper suffix.

    'struct timeval' → 'struct_timeval', 'SSL_CTX *' → 'SSL_CTX_pointer'.
    """
    out = []
    for ch in c_lang_type:
        if ch == " ":
            out.append("_")
        elif ch == "*":
            out.append("pointer")
        else:
            out.append(ch)
    return "".join(out)


# ──────────────────────────────────────────────────────────────────────────────
# Catalogue parsing
# ──────────────────────────────────────────────────────────────────────────────


def parse_catalogue(path):
    """Parse a cold catalogue .c.input → list of field dicts."""
    text = read(path)
    m = re.search(r"core_fields\s*\[\s*\]\s*=\s*\{", text)
    if not m:
        raise ValueError("%s: core_fields[] not found" % path)
    body, _ = braced(text, m.end() - 1)
    if body is None:
        raise ValueError("%s: core_fields[] brace mismatch" % path)

    fields = []
    for entry in split_entries(body):
        name = None
        nm = re.search(r"\.name\s*=\s*(?:\"([^\"]*)\"|NULL|0|([A-Za-z_][A-Za-z0-9_]*))", entry)
        if nm:
            name = nm.group(1) if nm.group(1) is not None else nm.group(2)
        if name is None:
            continue  # sentinel { .name = NULL } / { .name = 0 }

        dm = re.search(r"\.domain\s*=\s*UFSRV_CONFIG_DOMAIN_(\w+)", entry)
        domain = dm.group(1) if dm else ""

        kf = re.search(r"\.config_file_key\s*=\s*(CONF_FILE_LITERAL_\w+)", entry)
        config_file_key = kf.group(1) if kf else None

        vt = re.search(r"\.value_type\s*=\s*UFSRV_CONFIG_VALUE_(\w+)", entry)
        value_type = vt.group(1) if vt else ""

        ct = re.search(r"\.c_lang_type\s*=\s*\"([^\"]*)\"", entry)
        c_lang_type = ct.group(1) if ct else ""

        val = re.search(r"\.validation\s*=\s*\{[^}]*\.type\s*=\s*UFSRV_CONFIG_CHECK_(\w+)", entry)
        validation = val.group(1) if val else "NONE"

        fields.append({
            "name": name,
            "domain": domain,
            "config_file_key": config_file_key,
            "value_type": value_type,
            "c_lang_type": c_lang_type,
            "validation": validation,
            "is_keep_custom_getter": bool(re.search(r"\.is_keep_custom_getter\s*=\s*true", entry)),
            "is_keep_custom_setter": bool(re.search(r"\.is_keep_custom_setter\s*=\s*true", entry)),
            "is_as_ptr_getter": bool(re.search(r"\.is_as_ptr_getter\s*=\s*true", entry)),
        })
    return fields


# ──────────────────────────────────────────────────────────────────────────────
# Generated-file parsing
# ──────────────────────────────────────────────────────────────────────────────


def parse_enums(text):
    """→ (config, runtime): each a dict name→ordinal, plus '_count' key."""
    out = {}
    for dom in ("CONFIG", "RUNTIME"):
        m = re.search(r"enum\s+Ufsrv%sField\s*\{" % ("Config" if dom == "CONFIG" else "Runtime"), text)
        if not m:
            raise ValueError("enum Ufsrv%sField not found" % ("Config" if dom == "CONFIG" else "Runtime"))
        block, _ = braced(text, m.end() - 1)
        d = {}
        count = None
        for name, num in re.findall(r"UFSRV_%s_FIELD_([A-Z0-9_]+)\s*=\s*(\d+)" % dom, block):
            if name == "COUNT":
                count = int(num)
            else:
                d[name] = int(num)
        d["_count"] = count
        out[dom] = d
    return out


def parse_field_tables(text):
    """→ dict arr name → (declared_size, [designated index suffixes])."""
    out = {}
    for arr in ("ufsrv_config_field_table", "ufsrv_runtime_field_table",
                "ufsrv_config_field_defaults_table", "ufsrv_runtime_field_defaults_table"):
        m = re.search(r"\b%s\s*\[(\d+)\]\s*=\s*\{" % re.escape(arr), text)
        if not m:
            out[arr] = None
            continue
        size = int(m.group(1))
        block, _ = braced(text, m.end() - 1)
        suffixes = re.findall(r"\[UFSRV_(?:CONFIG|RUNTIME)_FIELD_([A-Z0-9_]+)\]\s*=", block)
        out[arr] = (size, suffixes)
    return out


def parse_field_names(text):
    """→ dict {enum_suffix: field_name} from the CONFIG + RUNTIME field tables.

    Keys each designated entry's .field_name by its enum suffix.  The
    defaults tables are UfsrvConfigValue arrays (no .field_name), so they are
    excluded by scoping the search to the two field-table arrays only.
    """
    out = {}
    for arr in ("ufsrv_config_field_table", "ufsrv_runtime_field_table"):
        m = re.search(r"\b%s\s*\[(\d+)\]\s*=\s*\{" % re.escape(arr), text)
        if not m:
            continue
        block, _ = braced(text, m.end() - 1)
        for sm, fn in re.findall(
                r"\[UFSRV_(?:CONFIG|RUNTIME)_FIELD_([A-Z0-9_]+)\]\s*=\s*\{"
                r"[\s\S]*?\.field_name\s*=\s*\"([^\"]*)\"",
                block):
            out[sm] = fn
    return out


def parse_field_table_on_value_available(text):
    """→ dict {enum_suffix: on_value_available_fn} from the CONFIG + RUNTIME
    field tables.

    Keys each designated entry's .on_value_available by its enum suffix, so the
    joined output can be checked for bridge/generic-setter wiring that survived
    the multi-library join.  The defaults tables are UfsrvConfigValue arrays (no
    lifecycle block), so they are excluded by scoping the search to the two
    field-table arrays only.
    """
    out = {}
    for arr in ("ufsrv_config_field_table", "ufsrv_runtime_field_table"):
        m = re.search(r"\b%s\s*\[(\d+)\]\s*=\s*\{" % re.escape(arr), text)
        if not m:
            continue
        block, _ = braced(text, m.end() - 1)
        for sm, fn in re.findall(
                r"\[UFSRV_(?:CONFIG|RUNTIME)_FIELD_([A-Z0-9_]+)\]\s*=\s*\{"
                r"[\s\S]*?\.on_value_available\s*=\s*(\w+)\s*,",
                block):
            out[sm] = fn
    return out


def parse_keymap(text):
    m = re.search(r"#define\s+UFSRV_CONFIG_KEY_MAP_COUNT\s+(\d+)", text)
    count = int(m.group(1)) if m else None
    entries = re.findall(r"CONF_FILE_LITERAL_([A-Z0-9_]+)\s*,\s*UFSRV_CONFIG_FIELD_([A-Z0-9_]+)", text)
    return count, entries


def parse_symbol_table(text):
    """Parse data_config_symbols_core_priv.h g_ufsrv_config_symbols[] →
    list of (key, type) tuples, in file order (so duplicates stay visible).

    `key` is the raw `.key` initialiser — either a CONF_FILE_LITERAL_* macro or
    a "quoted" literal lua key.  `type` is the UFSRV_CONFIG_VALUE_* suffix, or
    None when the entry carries no `.type`.  Returns None if the array is absent.
    """
    m = re.search(r"g_ufsrv_config_symbols\s*\[\s*\]\s*=\s*\{", text)
    if not m:
        return None
    body, _ = braced(text, m.end() - 1)
    if body is None:
        return None
    out = []
    for entry in split_entries(body):
        km = re.search(r"\.key\s*=\s*(CONF_FILE_LITERAL_\w+|\"[^\"]*\")", entry)
        if not km:
            continue
        tm = re.search(r"\.type\s*=\s*UFSRV_CONFIG_VALUE_(\w+)", entry)
        out.append((km.group(1), tm.group(1) if tm else None))
    return out


def parse_dispatch(text):
    entries = re.findall(r"\{\s*\"([^\"]+)\"\s*,\s*UFSRV_(?:CONFIG|RUNTIME)_FIELD_([A-Z0-9_]+)\s*\}", text)
    has_sentinel = bool(re.search(r"\{\s*NULL\s*,\s*-1\s*\}", text))
    return entries, has_sentinel


def parse_validate(text):
    """→ list of field suffixes referenced in UfsrvConfigValidate()."""
    m = re.search(r"UfsrvConfigValidate\s*\(\s*void\s*\)\s*\{", text)
    if not m:
        return None
    body, _ = braced(text, m.end() - 1)
    return re.findall(r"UFSRV_CONFIG_FIELD_([A-Z0-9_]+)", body)


def parse_master_includes(text):
    return re.findall(r"#include\s+\"([^\"]+)\"", text)


def function_names(text):
    """→ set of accessor function names. Return type sits on its own line above
    the name, so match identifiers at column 0 followed by '('."""
    return set(re.findall(r"^([A-Za-z_][A-Za-z0-9_]*)\s*\(", text, re.MULTILINE))


# ──────────────────────────────────────────────────────────────────────────────
# Checks
# ──────────────────────────────────────────────────────────────────────────────

class Reporter:
    def __init__(self):
        self.failures = []

    def fail(self, code, msg):
        self.failures.append((code, msg))

    def ok(self, code, msg):
        print("  PASS %-4s %s" % (code, msg))

    def warn(self, code, msg):
        print("  WARN %-4s %s" % (code, msg))


def check_includes(reporter, files):
    """V01–V06: the final rewritten include forms."""
    banned_angle = re.compile(r"#include\s+<ufsrvcorelib/ufsrv_core/ufsrv_config/generated/")
    flat = re.compile(r"#include\s+\"generated/")
    local_deep_ok = re.compile(r"#include\s+\"ufsrv_core/ufsrv_config/generated/([^\"]+)\"")
    sibling_c = re.compile(r"#include\s+\"([a-z0-9_]+_generated\.c)\"")
    bare_invariant = re.compile(r"#include\s+\"ufsrv_config_generic_callbacks_priv\.h\"")

    violations = []
    seen_local_deep = set()
    seen_siblings = set()
    seen_invariant = False
    for path in files:
        for line in read(path).splitlines():
            s = line.strip()
            if not s.startswith("#include"):
                continue
            if banned_angle.search(s):
                violations.append("%s: %s" % (os.path.basename(path), s))
            elif flat.search(s):
                violations.append("%s: %s" % (os.path.basename(path), s))
            else:
                lm = local_deep_ok.search(s)
                sm = sibling_c.search(s)
                if lm:
                    seen_local_deep.add(lm.group(1))
                elif sm:
                    seen_siblings.add(sm.group(1))
                elif bare_invariant.search(s):
                    seen_invariant = True
    if violations:
        reporter.fail("V01/V02", "residual un-rewritten includes:\n" + "\n".join("      " + v for v in violations))
    else:
        reporter.ok("V01/V02", "no residual angle-bracket/flat generated includes")

    expected_deep = {"ufsrv_config_field_enums_generated.h", "ufsrv_config_accessor_decls_generated.h"}
    if expected_deep.issubset(seen_local_deep):
        reporter.ok("V03", "public generated headers use LOCAL-deep quoted form")
    else:
        reporter.fail("V03", "missing LOCAL-deep include for: %s" % sorted(expected_deep - seen_local_deep))

    expected_sibs = {"data_ufsrv_config_field_tables_generated.c",
                     "ufsrv_config_accessor_defs_generated.c",
                     "ufsrv_config_validate_generated.c"}
    if expected_sibs.issubset(seen_siblings):
        reporter.ok("V04", "private generated .c siblings use bare-quoted form")
    else:
        reporter.fail("V04", "missing sibling .c include for: %s" % sorted(expected_sibs - seen_siblings))

    if seen_invariant:
        reporter.ok("V05/V06", "bare invariant + canonical includes present")
    else:
        reporter.fail("V05/V06", "bare 'ufsrv_config_generic_callbacks_priv.h' not found")


def check_as_string_stubs(reporter, stub_fields, dpr_dir):
    """V50: the merged ufsrv_config_stubs_as_string_priv_generated.h (plus any
    already-existing ufsrv_config_stubs_as_string_priv.h) declares exactly the
    non-standard AsString stub marshallers the catalogues require — nothing
    missing, nothing orphaned.

    Each non-standard c_lang_type field dispatches to a hand-authored stub
    UfsrvConfig<Name>AsString_<type-suffix>; these declarations live in the
    merged deep-tree header produced by the compose step.
    """
    merged_path = os.path.join(dpr_dir, "ufsrv_config_stubs_as_string_priv_generated.h")
    existing_path = os.path.join(dpr_dir, "ufsrv_config_stubs_as_string_priv.h")

    merged_text = read(merged_path) if os.path.exists(merged_path) else ""
    existing_text = read(existing_path) if os.path.exists(existing_path) else ""
    combined = merged_text + "\n" + existing_text

    # Exact function-name match — a substring check would wrongly accept
    # `…AsString_uint8_t` as present inside `…AsString_uint8_t_pointer`.
    declared = set(re.findall(r"\bBufferDescriptor\s*\*\s*(\w+)\s*\(", combined))

    expected = {
        "UfsrvConfig%sAsString_%s" % (pascal_case(f["name"]), as_string_type_suffix(f["c_lang_type"]))
        for _, f in stub_fields
    }

    missing = sorted(expected - declared)
    orphans = sorted(declared - expected)

    if missing:
        reporter.fail("V50", "AsString stub missing (%d/%d declared): %s" %
                      (len(expected) - len(missing), len(expected), ", ".join(missing)))
    elif stub_fields:
        reporter.ok("V50", "all %d non-standard AsString stubs declared" % len(expected))
    else:
        reporter.ok("V50", "no non-standard AsString fields — nothing to merge")

    if orphans:
        reporter.warn("V50", "orphan AsString stub declarations (not in catalogue): %s" %
                      ", ".join(orphans))


def check_symbol_table(reporter, symbol_entries, config_keys, literal_map):
    """V51: every config-file backing-store key is present in the symbol table.

    symbol_entries: list of (key, type) from parse_symbol_table, or None when
                    g_ufsrv_config_symbols[] could not be located.
    config_keys:    {CONF_FILE_LITERAL_* macro → value_type} for CONFIG-domain
                    fields carrying a config_file_key (the backing store).
    literal_map:    {macro → literal string} from ufsrv_config_literals_defs.h;
                    may be empty, in which case macro-form keys compare by name.

    The two structures key config entries differently (macro vs possible literal
    string), so both sides are normalised to the resolved literal string before
    the set comparison.  Missing backing-store keys and duplicate symbol-table
    keys are hard failures (a missing key means ResolveSymbols cannot bind a
    config field; a duplicate breaks the bsearch).  Type drift and orphan keys
    are reported as warnings only.
    """
    if symbol_entries is None:
        reporter.warn("V51", "skipped — g_ufsrv_config_symbols[] not found in symbols header")
        return

    def norm(key):
        if key.startswith("CONF_FILE_LITERAL_"):
            return literal_map.get(key, key)
        return key.strip('"')

    sym_types = {}
    duplicates = set()
    for key, typ in symbol_entries:
        canon = norm(key)
        if canon in sym_types:
            duplicates.add(canon)
        else:
            sym_types[canon] = typ

    backing_types = {norm(macro): vt for macro, vt in config_keys.items()}

    missing = sorted(set(backing_types) - set(sym_types))
    orphans = sorted(set(sym_types) - set(backing_types))
    type_mismatch = sorted(
        k for k in set(backing_types) & set(sym_types)
        if sym_types[k] != backing_types[k]
    )

    if missing:
        reporter.fail("V51", "backing-store key missing from symbol table (%d): %s" %
                      (len(missing), ", ".join(missing)))
    if duplicates:
        reporter.fail("V51", "symbol-table duplicate key (%d): %s" %
                      (len(duplicates), ", ".join(sorted(duplicates))))
    if type_mismatch:
        reporter.warn("V51", "symbol-table type != backing-store value_type (%d): %s" %
                      (len(type_mismatch), ", ".join(type_mismatch)))
    if orphans:
        reporter.warn("V51", "symbol-table keys absent from backing store (%d): %s" %
                      (len(orphans), ", ".join(orphans)))
    if not (missing or duplicates):
        reporter.ok("V51", "all %d backing-store keys present in symbol table" % len(backing_types))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--catalogue", action="append", required=True,
                    help="lib=path to a cold catalogue .c.input (repeatable, ordered)")
    ap.add_argument("--generated-private", required=True)
    ap.add_argument("--generated-public", required=True)
    ap.add_argument("--literals-header", default=None,
                    help="Path to ufsrv_config_literals_defs.h (for key-map sort check)")
    ap.add_argument("--symbols-header", default=None,
                    help="Path to data_config_symbols_core_priv.h (symbol-table ↔ backing-store check)")
    args = ap.parse_args()

    rep = Reporter()

    # ── Parse the catalogues (source of truth) ────────────────────────────────
    catalogues = []
    for spec in args.catalogue:
        lib, _, path = spec.partition("=")
        catalogues.append((lib, path, parse_catalogue(path)))

    # ── Derived expected state ────────────────────────────────────────────────
    all_fields = []  # (domain, enum_suffix, config_file_key, validation, is_keep_custom_setter, is_as_ptr_getter)
    for lib, path, fields in catalogues:
        for f in fields:
            suffix = enum_suffix(f["name"])
            all_fields.append((f["domain"], suffix, f["config_file_key"],
                               f["validation"], f["is_keep_custom_setter"], f["is_as_ptr_getter"]))

    exp_config = set()
    exp_runtime = set()
    exp_keymap = set()
    exp_validated = set()
    exp_custom_setter = set()
    exp_asptr = set()
    for domain, suffix, ck, val, custom_setter, asptr in all_fields:
        if domain == "CONFIG":
            exp_config.add(suffix)
            if ck:
                exp_keymap.add(suffix)
        elif domain == "RUNTIME":
            exp_runtime.add(suffix)
        if val != "NONE":
            exp_validated.add(suffix)
        if custom_setter:
            exp_custom_setter.add(suffix)
        if asptr:
            exp_asptr.add(suffix)

    exp_config_count = len(exp_config)
    exp_runtime_count = len(exp_runtime)
    exp_total_count = exp_config_count + exp_runtime_count
    exp_keymap_count = len(exp_keymap)
    exp_validated_count = len(exp_validated)

    # ── Read generated files ──────────────────────────────────────────────────
    gpriv = args.generated_private
    gpub = args.generated_public

    def gp(name):
        return os.path.join(gpub, name)

    def gpr(name):
        return os.path.join(gpriv, name)

    enums_text = read(gp("ufsrv_config_field_enums_generated.h"))
    tables_text = read(gpr("data_ufsrv_config_field_tables_generated.c"))
    keymap_text = read(gpr("data_ufsrv_config_literal_to_field_map_priv_generated.h"))
    dispatch_text = read(gpr("data_ufsrv_config_dispatch_table_priv_generated.h"))
    validate_text = read(gpr("ufsrv_config_validate_generated.c"))
    master_text = read(gpr("ufsrv_config_getters_setters_generated.c"))
    decls_text = read(gp("ufsrv_config_accessor_decls_generated.h"))
    defs_text = read(gpr("ufsrv_config_accessor_defs_generated.c"))

    # ── Enum checks ───────────────────────────────────────────────────────────
    enums = parse_enums(enums_text)
    for dom, exp in (("CONFIG", exp_config), ("RUNTIME", exp_runtime)):
        got = enums[dom]
        got_count = got.pop("_count", None)
        exp_count = len(exp)
        if got_count == exp_count:
            rep.ok("V10/V11", "%s count == %d" % (dom, exp_count))
        else:
            rep.fail("V10/V11", "%s count mismatch: expected %d, got %s" % (dom, exp_count, got_count))
        names = set(got)
        if names == exp:
            rep.ok("V12", "%s enum names == catalogue union (no orphan/dup)" % dom)
        else:
            rep.fail("V12", "%s enum name mismatch — missing %s / orphan %s" %
                     (dom, sorted(exp - names), sorted(names - exp)))
        # density: ordinals are exactly 0..count-1
        ords = sorted(got.values())
        if ords == list(range(exp_count)):
            rep.ok("V12", "%s ordinals dense 0..%d" % (dom, exp_count - 1))
        else:
            rep.fail("V12", "%s ordinals not dense: %s" % (dom, ords))

    # ── Table-slot + join checks ──────────────────────────────────────────────
    tables = parse_field_tables(tables_text)
    for arr, exp in (("ufsrv_config_field_table", exp_config),
                     ("ufsrv_runtime_field_table", exp_runtime),
                     ("ufsrv_config_field_defaults_table", exp_config),
                     ("ufsrv_runtime_field_defaults_table", exp_runtime)):
        got = tables.get(arr)
        if got is None:
            rep.fail("V20", "%s array not found" % arr)
            continue
        size, suffixes = got
        if size == len(exp) and len(suffixes) == len(exp) and set(suffixes) == exp:
            rep.ok("V20/V21/V22", "%s: declared[%d] == %d slots == catalogue union" % (arr, size, len(exp)))
        else:
            rep.fail("V20/V21/V22", "%s: declared[%d], %d entries, expected %d; missing %s / orphan %s" %
                     (arr, size, len(suffixes), len(exp),
                      sorted(exp - set(suffixes)), sorted(set(suffixes) - exp)))

    # V23 — every field-table entry carries a .field_name matching its enum identity
    field_names = parse_field_names(tables_text)
    missing_fn = sorted((exp_config | exp_runtime) - set(field_names))
    bad_fn = sorted((s, field_names[s]) for s in (exp_config | exp_runtime)
                    if s in field_names and enum_suffix(field_names[s]) != s)
    if not missing_fn and not bad_fn:
        rep.ok("V23", "field_name present + matches enum identity for all %d entries" % exp_total_count)
    else:
        rep.fail("V23", "field_name: missing %s; mismatched %s" % (missing_fn, bad_fn))

    # V52 — every is_keep_custom_setter field's joined field-table entry wires
    # .on_value_available = UfsrvConfig<Name>Setter (the generated lifecycle
    # bridge), i.e. the bridge wiring survived the multi-library join.  The
    # standalone verifier's S04 checks this per-library; V52 checks the joined
    # table the host actually compiles.
    on_value_available = parse_field_table_on_value_available(tables_text)
    custom_setter_fields = [f for _, _, fields in catalogues for f in fields
                            if f["is_keep_custom_setter"]]
    bridge_missing = []
    bridge_bad = []
    bridge_ok = 0
    for f in custom_setter_fields:
        suffix = enum_suffix(f["name"])
        expected = "UfsrvConfig%sSetter" % pascal_case(f["name"])
        actual = on_value_available.get(suffix)
        if actual is None:
            bridge_missing.append(suffix)
        elif actual != expected:
            bridge_bad.append((suffix, actual, expected))
        else:
            bridge_ok += 1
    if not custom_setter_fields:
        rep.ok("V52", "no is_keep_custom_setter fields — nothing to verify")
    elif not bridge_missing and not bridge_bad:
        rep.ok("V52", "field-table .on_value_available bridge wiring: %d/%d" %
               (bridge_ok, len(custom_setter_fields)))
    else:
        rep.fail("V52", "bridge wiring — missing %s; mismatched %s" %
                 (bridge_missing, bridge_bad))

    # ── Key-map / dispatch / validate ─────────────────────────────────────────
    km_count, km_entries = parse_keymap(keymap_text)
    km_names = {e[1] for e in km_entries}
    if km_count == exp_keymap_count and km_names == exp_keymap:
        rep.ok("V30", "key-map count %d == catalogue; entries == union" % exp_keymap_count)
    else:
        rep.fail("V30", "key-map mismatch — count %s vs %d; missing %s / orphan %s" %
                 (km_count, exp_keymap_count, sorted(exp_keymap - km_names), sorted(km_names - exp_keymap)))

    # V33 — key-map entries sorted by resolved literal string (bsearch invariant)
    if args.literals_header:
        literal_map = parse_literals_header(args.literals_header)
        resolved = []
        unresolved = []
        for lit_suffix, _ in km_entries:
            macro = "CONF_FILE_LITERAL_" + lit_suffix
            s = literal_map.get(macro)
            if s is None:
                unresolved.append(macro)
            else:
                resolved.append(s)
        if unresolved:
            rep.fail("V33", "key-map literals not in defs header: %s" % ", ".join(unresolved))
        elif resolved == sorted(resolved) and len(set(resolved)) == len(resolved):
            rep.ok("V33", "key-map %d entries sorted by literal (strcmp order), no duplicates" % len(resolved))
        else:
            rep.fail("V33", "key-map NOT sorted by literal string — bsearch invariant broken")
    else:
        rep.warn("V33", "skipped — no --literals-header provided")

    # ── V51 — symbol table ↔ backing-store reconciliation ─────────────────────
    # Every config-file key in the joined backing store (keyed by
    # CONF_FILE_LITERAL_*) must have a matching entry in the hand-maintained lua
    # symbol table (data_config_symbols_core_priv.h / g_ufsrv_config_symbols[]).
    literal_map = parse_literals_header(args.literals_header) if args.literals_header else {}
    if not args.symbols_header:
        rep.warn("V51", "skipped — no --symbols-header provided")
    elif not os.path.exists(args.symbols_header):
        rep.warn("V51", "skipped — symbols header not found: %s" % args.symbols_header)
    else:
        config_keys = {}
        for _lib, _path, _fields in catalogues:
            for _f in _fields:
                if _f["domain"] == "CONFIG" and _f["config_file_key"]:
                    config_keys[_f["config_file_key"]] = _f["value_type"]
        check_symbol_table(rep, parse_symbol_table(read(args.symbols_header)),
                           config_keys, literal_map)

    disp_entries, disp_sentinel = parse_dispatch(dispatch_text)
    disp_names = {e[1] for e in disp_entries}
    if len(disp_entries) == exp_total_count and disp_names == (exp_config | exp_runtime) and disp_sentinel:
        rep.ok("V31", "dispatch %d entries == total fields; single sentinel" % exp_total_count)
    else:
        rep.fail("V31", "dispatch mismatch — entries %d vs %d; sentinel=%s" %
                 (len(disp_entries), exp_total_count, disp_sentinel))

    val_fields = parse_validate(validate_text)
    if val_fields is not None and set(val_fields) == exp_validated:
        rep.ok("V32", "validate has %d blocks == validated fields" % exp_validated_count)
    else:
        rep.fail("V32", "validate mismatch — got %s, expected %s" %
                 (val_fields, sorted(exp_validated)))

    # ── Accessor parity ───────────────────────────────────────────────────────
    # Custom getter/setter accessors are declared extern in the generated decls
    # but defined hand-authored in the contributing libraries (transitively
    # linked) — they are legitimately absent from the generated defs file.
    custom_accessor_names = set()
    for lib, _, fields in catalogues:
        for f in fields:
            pascal = pascal_case(f["name"])
            if f["is_keep_custom_getter"]:
                custom_accessor_names.add("UfsrvConfig%s" % pascal)
                if f["is_as_ptr_getter"]:
                    custom_accessor_names.add("UfsrvConfig%sAsPtr" % pascal)
            if f["is_keep_custom_setter"]:
                custom_accessor_names.add("UfsrvConfigSet%s" % pascal)

    decl_names = function_names(decls_text)
    def_names = function_names(defs_text)
    # decl set must equal generated defs ∪ custom (hand-authored) accessors.
    missing_defs = sorted(decl_names - def_names - custom_accessor_names)  # declared, no def
    orphan_defs = sorted(def_names - decl_names)                           # def, no decl
    if decl_names and not missing_defs and not orphan_defs:
        rep.ok("V40", "accessor decls == defs + custom (%d functions)" % len(decl_names))
    else:
        msg = "accessor decl/def mismatch (decl %d, def %d, custom %d)" % (
            len(decl_names), len(def_names), len(custom_accessor_names))
        if missing_defs:
            msg += "; no definition for %s" % ", ".join(missing_defs)
        if orphan_defs:
            msg += "; no declaration for %s" % ", ".join(orphan_defs)
        rep.fail("V40", msg)

    # ── Master include TU ─────────────────────────────────────────────────────
    master_incs = parse_master_includes(master_text)
    expect_master = {"ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h",
                     "ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h",
                     "data_ufsrv_config_field_tables_generated.c",
                     "ufsrv_config_accessor_defs_generated.c",
                     "ufsrv_config_validate_generated.c"}
    if set(master_incs) == expect_master:
        rep.ok("V41", "master include aggregates exactly the 3 .c fragments")
    else:
        rep.fail("V41", "master include set mismatch: %s" % sorted(master_incs))

    # ── AsString stub merge ───────────────────────────────────────────────────
    _dpr = os.path.dirname(gpriv)
    stub_fields = [(lib, f) for lib, _, fields in catalogues for f in fields
                   if f["c_lang_type"] not in GENERIC_AS_STRING_MAP]
    check_as_string_stubs(rep, stub_fields, _dpr)

    # ── Include-path checks across all generated files ────────────────────────
    check_includes(rep, [
        gp("ufsrv_config_field_enums_generated.h"),
        gp("ufsrv_config_accessor_decls_generated.h"),
        gpr("data_ufsrv_config_field_tables_generated.c"),
        gpr("data_ufsrv_config_dispatch_table_priv_generated.h"),
        gpr("data_ufsrv_config_literal_to_field_map_priv_generated.h"),
        gpr("ufsrv_config_accessor_defs_generated.c"),
        gpr("ufsrv_config_getters_setters_generated.c"),
        gpr("ufsrv_config_validate_generated.c"),
    ])

    # ── Report ────────────────────────────────────────────────────────────────
    if rep.failures:
        print("\nufsrv_config_verify_host: %d check(s) FAILED" % len(rep.failures))
        for code, msg in rep.failures:
            print("  FAIL %-7s %s" % (code, msg))
        return 1
    print("\nufsrv_config_verify_host: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
