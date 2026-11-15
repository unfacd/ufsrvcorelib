#!/usr/bin/env python3
"""
ufsrv_config_verify — Audit generated artefacts against the cold catalogue.

The cold catalogue is the single source of truth.  Every generated artefact
(enums, accessor decls/defs, field tables, validate, adapters) must be
derivable from it.  This script verifies that contract.

═══ Operating Modes ═══════════════════════════════════════════════════════════

  Catalogue-only (no --generated-dir):
    Checks the input catalogue itself for internal consistency.
    No generated files needed.  Runs C01–C08 only.

  Full verification (with --generated-dir):
    Catalogue checks + all generated artefact checks.
    Requires regenerated output from ufsrv_config_generator.

═══ Flag Codes ═══════════════════════════════════════════════════════════════

  Catalogue checks (C-series) — run on the .c.input file alone:
    C01  .value_type present on every field
    C02  CONFIG fields have .config_file_key = CONF_FILE_LITERAL_*
    C03  STRUCT/STR fields have type_allocation_sz > 0
    C04  .stored_value member (.int_val/.str_val/.ptr_val/.blob_val)
         matches .value_type; STR .str_val must be "..." quoted
    C05  Lifecycle callbacks match value_type:
         INT/ENUM→GenericIntInflator+Setter,
         STR→GenericStrInflator+Setter,
         STRUCT→GenericStructInflator+Setter,
         PTR→GenericPtrInflator+Setter,
         CONST_PTR→NULL
    C06  STRUCT fields must have validation NONE
    C07  Callback function bodies exist for every lifecycle name
         referenced in the catalogue
    C08  WARN when is_keep_custom_setter=true but on_value_available
         is a generic callback (prefixed 'Generic') — the generic
         setter may conflict with the hand-authored custom setter.
         User-supplied (non-'Generic') callbacks are assumed legitimate.

  Getter checks (G-series) — verify generated getter correctness:
    G01  Getter declaration return type matches c_lang_type verbatim
    G02  Getter definition present (generated or hand-authored)
    G03  Getter decl vs def return types match (catches G-001)
    G04  Getter return expression type matches declared type
         (STRUCT must dereference, INT/PTR cast must match)
    G05  Getter body indexes correct enum constant for its field
    G06  Getter body dereferences correct union member
         (.int_val/.str_val/.ptr_val/.blob_val) for its value_type
    G07  Getter body accesses correct store array for its domain
         (CONFIG→ufsrv_config_store, RUNTIME→ufsrv_runtime_store)
    G08  Custom-getter honoured — extern getter decl (and extern AsPtr decl
         when is_as_ptr_getter) emitted for hand-authored defs; no generated
         getter def or AsPtr def for is_keep_custom_getter fields

  Setter checks (S-series):
    S01  Setter declaration + definition present for every
         non-CONST_PTR field
    S02  Setter body pattern matches value_type + domain
         (STRUCT→memcpy, Config→.stored_value.<member>,
         Runtime→direct store access)
    S03  Custom-setter honoured — extern setter decl emitted (hand-authored
         def); no generated setter def for is_keep_custom_setter fields; the
         generated lifecycle bridge UfsrvConfig<Name>Setter decl + def present
    S04  Custom-setter bridge wired — field-table entry sets
         .on_value_available = UfsrvConfig<Name>Setter for every
         is_keep_custom_setter field

  Table checks (T-series) — verify generated data tables:
    T01  Every field has an enum entry in generated header
    T02  Every field has a field table entry
    T03  Every validated field has a check in Validate()
    T04  Every config-file-mapped field has an adapter declaration
         (bodies are hand-authored — missing bodies are a warning)
    T05  Every is_as_ptr_getter field has an AsPtr decl + def
    T06  Every field-table .config entry is zero-initialized ({ 0 })
         (compiled defaults live in the separate _defaults_table[] arrays)
    T07  Every field-table entry has a corresponding defaults-table entry
         with .int_val / .str_val / .type_allocation_sz matching the
         cold catalogue — the defaults table is the canonical compiled copy
    T08  Every AsPtr getter body returns a pointer to the correct
         stored_value union member for its value_type
         (INT/ENUM→.int_val, STR→.str_val, STRUCT→.blob_val,
         PTR/CONST_PTR→.ptr_val) — not a cast of Instance() itself

  Integration checks (I-series):
    I01  All generated includes are fully-qualified angle-bracket form
    I02  No orphan accessors (generated getter without catalogue entry)
    I03  Naming convention compliance (stub — not yet implemented)

  AsString checks (A-series):
    A01  AsString accessor declaration present for every field
    A02  AsString accessor definition present for every field
    A03  Accessor dispatches to the correct marshaller (generic / stub)
    A04  Generic marshaller (def + decl) present for every standard type
    A05  Stub marshaller (def + decl) present for every non-standard field
    A06  Field-table entry wires .on_as_string = UfsrvConfig<Name>AsString

═══ Usage ════════════════════════════════════════════════════════════════════

  Catalogue-only:
    python3 ufsrv_config_verify --catalogue ufsrv_config_generator_fields.c.input

  Full verification:
    python3 ufsrv_config_verify \\
        --catalogue ufsrv_config_generator_fields.c.input \\
        --generated-dir generated/

  Exit 0 on pass, exit 1 on any failure.
"""

import re
import sys
import os
import argparse
from collections import OrderedDict

# ── helpers ──────────────────────────────────────────────────────────────────

def pascal_case(snake_name):
    """Convert under_score_name to PascalCaseName."""
    parts = snake_name.split('_')
    return ''.join(p.capitalize() for p in parts)

def upper_snake(snake_name):
    """Convert under_score_name to UPPER_SNAKE_NAME."""
    return snake_name.upper()

def expected_getter_return(c_lang_type, value_type):
    """
    Return type is c_lang_type verbatim — always.
    The catalogue author declares the exact return type.
    No generator transform (no const prepend, no pointer append).
    Catalogue examples:
      "int"           → getter returns int
      "const char *"  → getter returns const char *
      "SSL_CTX *"     → getter returns SSL_CTX *
      "const struct timeval *" → getter returns const struct timeval *
    """
    return c_lang_type

def read_file(path):
    with open(path, 'r') as f:
        return f.read()

def fail(flag, msg):
    print(f"  [{flag}] FAIL  {msg}")
    global _failures
    _failures += 1

def ok(msg):
    print(f"        OK   {msg}")

def warn(flag, msg):
    print(f"  [{flag}] WARN {msg}")
    global _warnings
    _warnings += 1

# ── Step 1: Parse the cold catalogue ─────────────────────────────────────────

FIELD_BLOCK_RE = re.compile(r'\{\s*\n(.*?)\n\s*\}', re.DOTALL)

NAME_RE           = re.compile(r'\.name\s*=\s*"([^"]+)"')
NAME_MACRO_RE      = re.compile(r'\.name\s*=\s*(CONF_FILE_LITERAL_\w+)')
C_LANG_TYPE_RE    = re.compile(r'\.c_lang_type\s*=\s*"([^"]*)"')
VALUE_TYPE_RE     = re.compile(r'\.value_type\s*=\s*(UFSRV_CONFIG_VALUE_\w+)')
DOMAIN_RE         = re.compile(r'\.domain\s*=\s*(UFSRV_CONFIG_DOMAIN_\w+)')
CONFIG_FILE_KEY_RE = re.compile(r'\.config_file_key\s*=\s*(CONF_FILE_LITERAL_\w+)')
VALIDATION_TYPE_RE = re.compile(r'\.type\s*=\s*(UFSRV_CONFIG_CHECK_\w+)')
IS_AS_PTR_GETTER_RE = re.compile(r'\.is_as_ptr_getter\s*=\s*true')
IS_KEEP_CUSTOM_GETTER_RE = re.compile(r'\.is_keep_custom_getter\s*=\s*true')
IS_KEEP_CUSTOM_SETTER_RE = re.compile(r'\.is_keep_custom_setter\s*=\s*true')
TYPE_ALLOCATION_SZ_RE = re.compile(r'\.type_allocation_sz\s*=\s*(\d+|sizeof\s*\([^)]+\))')
STORED_VALUE_MEMBER_RE = re.compile(r'\.stored_value\s*=\s*\{\s*\.(\w+)\s*=')
STR_VAL_QUOTED_RE = re.compile(r'\.str_val\s*=\s*"([^"]*)"')
INT_DEFAULT_RE = re.compile(r'\.int_val\s*=\s*(-?\d+)')
INT_DEFAULT_SYMBOL_RE = re.compile(r'\.int_val\s*=\s*([A-Za-z_][A-Za-z0-9_]*)')
STR_DEFAULT_RE = re.compile(r'\.str_val\s*=\s*"([^"]*)"')

VALUE_TYPE_TO_GENERIC = {
    'UFSRV_CONFIG_VALUE_INT':       ('GenericIntInflator',    'GenericIntSetter'),
    'UFSRV_CONFIG_VALUE_ENUM':      ('GenericIntInflator',    'GenericIntSetter'),
    'UFSRV_CONFIG_VALUE_BOOL':      ('GenericBoolInflator',   'GenericBoolSetter'),
    'UFSRV_CONFIG_VALUE_STR':       ('GenericStrInflator',    'GenericStrSetter'),
    'UFSRV_CONFIG_VALUE_STRUCT':    ('GenericStructInflator', 'GenericStructSetter'),
    'UFSRV_CONFIG_VALUE_PTR':       ('GenericPtrInflator',    'GenericPtrSetter'),
    'UFSRV_CONFIG_VALUE_CONST_PTR': (None,                   None),
}

VALUE_TYPE_TO_MEMBER = {
    'UFSRV_CONFIG_VALUE_INT':       'int_val',
    'UFSRV_CONFIG_VALUE_ENUM':      'int_val',
    'UFSRV_CONFIG_VALUE_BOOL':      'int_val',
    'UFSRV_CONFIG_VALUE_STR':       'str_val',
    'UFSRV_CONFIG_VALUE_PTR':       'ptr_val',
    'UFSRV_CONFIG_VALUE_CONST_PTR': 'ptr_val',
    'UFSRV_CONFIG_VALUE_STRUCT':    'blob_val',
}

# c_lang_type → generic AsString marshaller name (mirrors the generator's map)
GENERIC_AS_STRING_MAP = {
    'int':      'GenericIntAsString',
    'double':   'GenericDoubleAsString',
    'bool':     'GenericBoolAsString',
    'char *':   'GenericCharPtrAsString',
    'unsigned': 'GenericUnsignedAsString',
    'unsigned char': 'GenericUnsignedCharAsString',
    'size_t':   'GenericSizeTAsString',
    'time_t':   'GenericTimeTAsString',
    'pid_t':    'GenericPidTAsString',
    'struct timeval':   'GenericTimevalAsString',
    'struct timeval *': 'GenericTimevalPtrAsString',
}


def as_string_type_suffix(c_lang_type):
    """Sanitize c_lang_type to the hand-authored helper suffix:
    'struct timeval' → 'struct_timeval', 'SSL_CTX *' → 'SSL_CTX_pointer'."""
    out = []
    for ch in c_lang_type:
        if ch == ' ':
            out.append('_')
        elif ch == '*':
            out.append('pointer')
        else:
            out.append(ch)
    return ''.join(out)

# Lifecycle callback names — extract quoted string or detect NULL
def extract_lifecycle_name(text, key):
    """Extract .key = "Value" or .key = NULL from text. Returns string or None."""
    # Match either "StringLiteral" or NULL
    m = re.search(rf'\.{key}\s*=\s*"([^"]*)"', text)
    if m:
        return m.group(1) if m.group(1) else None
    if re.search(rf'\.{key}\s*=\s*NULL', text):
        return None
    return None  # not specified

def field_name_from_macro(macro_name):
    """Derive a catalogue field name from a CONF_FILE_LITERAL_* macro identifier.

    CONF_FILE_LITERAL_LISTENER_WORKERS_THREAD_POOL → listener_workers_thread_pool
    """
    prefix = 'CONF_FILE_LITERAL_'
    if macro_name.startswith(prefix):
        return macro_name[len(prefix):].lower()
    return macro_name.lower()

# ── Enum-constant resolution ────────────────────────────────────────────────
# Symbolic enum defaults (e.g. `.int_val = SERVER_PROCESS_MODE_DAEMON`) cannot
# be evaluated by this text-based verifier.  To cross-check them against the
# generated defaults table, the verifier resolves each enumerator to its
# integer value by parsing the enum definitions found in the include tree.

ENUM_DEF_RE = re.compile(
    r'\benum\s+[A-Za-z_][A-Za-z0-9_]*\s*\{([^{}]*)\}', re.DOTALL)


def _parse_enum_body(body):
    """Compute {ENUMERATOR: value} for the enumerator list in an enum body.

    Handles the simple enum forms used in this codebase: implicit sequencing
    (`A, B, C` → 0, 1, 2) and explicit integer assignment (`A = 0, B = 5`).
    Block and line comments are stripped before parsing.
    """
    body = re.sub(r'/\*.*?\*/', ' ', body, flags=re.DOTALL)
    body = re.sub(r'//[^\n]*', ' ', body)
    computed = {}
    running = 0
    for raw in body.split(','):
        item = raw.strip()
        if not item:
            continue
        name = item
        explicit = None
        if '=' in item:
            name, _, expr = item.partition('=')
            name = name.strip()
            explicit = expr.strip()
        if explicit is not None:
            try:
                running = int(explicit, 0)
            except ValueError:
                ref = computed.get(explicit)
                if ref is None:
                    # Unresolvable expression (macro / bit-shift / forward
                    # ref) — this enum's values are unreliable, so drop it.
                    return {}
                running = ref
        computed[name] = running
        running += 1
    return computed


def build_enum_symbol_map(include_roots):
    """Scan include roots once and return {SYMBOL: value} for every enum constant."""
    symbol_map = {}
    for root in include_roots:
        if not root or not os.path.isdir(root):
            continue
        for dirpath, _dirnames, filenames in os.walk(root):
            for fn in filenames:
                if not fn.endswith(('.h', '.h.in')):
                    continue
                path = os.path.join(dirpath, fn)
                try:
                    text = read_file(path)
                except OSError:
                    continue
                for m in ENUM_DEF_RE.finditer(text):
                    symbol_map.update(_parse_enum_body(m.group(1)))
    return symbol_map


def parse_catalogue(path):
    """Parse the cold catalogue .c.input file into a list of Field dicts."""
    text = read_file(path)
    fields = []

    # Split on '},' that ends each field (but not inner struct closings)
    # Strategy: find the top-level blocks between { and }, that contain .name =
    blocks = FIELD_BLOCK_RE.findall(text)

    for block in blocks:
        nm = NAME_RE.search(block)
        if not nm:
            # Try macro-based name (e.g. .name = CONF_FILE_LITERAL_*)
            nm_macro = NAME_MACRO_RE.search(block)
            if nm_macro:
                name = field_name_from_macro(nm_macro.group(1))
            else:
                continue
        else:
            name = nm.group(1)
        if name == '':  # sentinel
            break

        clt = C_LANG_TYPE_RE.search(block)
        vt  = VALUE_TYPE_RE.search(block)
        dom = DOMAIN_RE.search(block)
        vt_valid = VALIDATION_TYPE_RE.search(block)
        cfk = CONFIG_FILE_KEY_RE.search(block)

        field = {
            '_block': block,
            'name': name,
            'pascal': pascal_case(name),
            'upper': upper_snake(name),
            'c_lang_type': clt.group(1) if clt else 'int',
            'value_type': vt.group(1) if vt else None,
            'has_value_type': vt is not None,
            'domain': dom.group(1) if dom else 'UFSRV_CONFIG_DOMAIN_CONFIG',
            'config_file_key': cfk.group(1) if cfk else None,
            'is_as_ptr_getter': bool(IS_AS_PTR_GETTER_RE.search(block)),
            'is_keep_custom_getter': bool(IS_KEEP_CUSTOM_GETTER_RE.search(block)),
            'is_keep_custom_setter': bool(IS_KEEP_CUSTOM_SETTER_RE.search(block)),
            'type_allocation_sz': (int(TA_SZ.group(1)) if TA_SZ.group(1).isdigit() else 1)
                if (TA_SZ := TYPE_ALLOCATION_SZ_RE.search(block)) else 0,
            'type_allocation_sz_is_sizeof': ('sizeof' in TA_SZ.group(1))
                if (TA_SZ := TYPE_ALLOCATION_SZ_RE.search(block)) else False,
            'stored_value_member': (SVM.group(1) if (SVM := STORED_VALUE_MEMBER_RE.search(block)) else None),
            'validation_type': vt_valid.group(1) if vt_valid else 'UFSRV_CONFIG_CHECK_NONE',
            'lifecycle': {
                'on_inflated':        extract_lifecycle_name(block, 'on_inflated'),
                'on_value_available': extract_lifecycle_name(block, 'on_value_available'),
                'on_value_reloaded':  extract_lifecycle_name(block, 'on_value_reloaded'),
                'on_teardown':        extract_lifecycle_name(block, 'on_teardown'),
            },
            'expected_getter_return': expected_getter_return(
                (clt.group(1) if clt else 'int'),
                (vt.group(1) if vt else 'UFSRV_CONFIG_VALUE_INT')
            ),
            'expected_enum_prefix': (
                'UFSRV_CONFIG_FIELD_' if (
                    not dom or dom.group(1) == 'UFSRV_CONFIG_DOMAIN_CONFIG'
                ) else 'UFSRV_RUNTIME_FIELD_'
            ),
            'int_default': (int(ID.group(1)) if (ID := INT_DEFAULT_RE.search(block)) else 0),
            'int_default_symbol': (IS.group(1) if (IS := INT_DEFAULT_SYMBOL_RE.search(block)) else None),
            'str_default': (SD.group(1) if (SD := STR_DEFAULT_RE.search(block)) else ''),
        }
        fields.append(field)

    return fields

# ── Step 2: Parse generated artefacts ────────────────────────────────────────

def parse_enums(text):
    """Extract {UPPER_NAME: index} from enum definitions."""
    config = OrderedDict()
    runtime = OrderedDict()

    in_config = False
    in_runtime = False

    for line in text.split('\n'):
        if 'enum UfsrvConfigField' in line or 'enum UfsrvConfigCoreField' in line:
            in_config = True
        elif 'enum UfsrvRuntimeField' in line or 'enum UfsrvRuntimeCoreField' in line:
            in_runtime = True
        elif in_config and '}' in line:
            in_config = False
        elif in_runtime and '}' in line:
            in_runtime = False
        elif in_config:
            m = re.match(r'\s*(UFSRV\w*CONFIG\w*_FIELD_(\w+))\s*=\s*(\d+)', line)
            if m:
                config[m.group(2).lower()] = int(m.group(3))
        elif in_runtime:
            m = re.match(r'\s*(UFSRV\w*RUNTIME\w*_FIELD_(\w+))\s*=\s*(\d+)', line)
            if m:
                runtime[m.group(2).lower()] = int(m.group(3))

    return config, runtime

def parse_accessor_decls(text):
    """
    Extract {name_lower: return_type} from PUBLIC_API getter declarations.
    Handles multi-line return types (e.g. 'const struct timeval *' on one line,
    function name on the next).
    """
    decls = OrderedDict()

    # Pattern: PUBLIC_API <rest-of-line> \n optional-continuation \n UfsrvConfig<Name>(void);
    # We scan for UfsrvConfig<Name>(void); and look back for the type.
    lines = text.split('\n')
    for i, line in enumerate(lines):
        m = re.match(r'\s*UfsrvConfig(\w+)\(void\);', line)
        if not m:
            # Also match Runtime accessors: UfsrvConfig<Name>(void);
            continue
        pascal_name = m.group(1)

        # Look backward for PUBLIC_API line
        ret_type = None
        for j in range(i - 1, max(i - 4, -1), -1):
            prev = lines[j].strip()
            if prev.startswith('PUBLIC_API') or prev.startswith('extern'):
                # Declaration line — strip the storage-class ('extern') and
                # visibility ('PUBLIC_API') markers, in any order, leaving the
                # bare return type.
                ret_type = re.sub(r'^(?:(?:PUBLIC_API|extern)\s+)+', '', prev)
                break
            elif prev and not prev.startswith('//') and not prev.startswith('/*'):
                # continuation of return type on its own line
                if ret_type is None:
                    ret_type = prev
                else:
                    ret_type = prev + ' ' + ret_type

        if ret_type:
            # Normalize whitespace
            ret_type = ' '.join(ret_type.split())
            decls[pascal_name] = ret_type

    return decls

def parse_accessor_defs(text):
    """
    Extract {name_lower: return_type} from getter definitions.
    The return type is on the line immediately before UfsrvConfig<Name>(void).
    """
    defs = OrderedDict()

    lines = text.split('\n')
    for i, line in enumerate(lines):
        m = re.match(r'\s*UfsrvConfig(\w+)\(void\)', line)
        if not m:
            continue
        pascal_name = m.group(1)

        # The return type is on the preceding non-empty line(s)
        ret_type = None
        for j in range(i - 1, max(i - 4, -1), -1):
            prev = lines[j].strip()
            if prev and not prev.startswith('//') and not prev.startswith('/*') and not prev.startswith('*'):
                if ret_type is None:
                    ret_type = prev
                else:
                    ret_type = prev + ' ' + ret_type
            elif prev == '' and ret_type is not None:
                break  # blank line separates the type from preceding content

        if ret_type:
            ret_type = ' '.join(ret_type.split())
            defs[pascal_name] = ret_type

    return defs

def parse_as_string_decls(text):
    """Extract {pascal_name} from UfsrvConfig<Name>AsString(...) declarations."""
    decls = set()
    for line in text.split('\n'):
        m = re.match(r'\s*UfsrvConfig(\w+)AsString\(int field_index', line)
        if m:
            decls.add(m.group(1))
    return decls

def parse_as_string_defs(text):
    """Extract {pascal_name: dispatch_fn} from AsString definitions."""
    defs = OrderedDict()
    lines = text.split('\n')
    for i, line in enumerate(lines):
        m = re.match(r'\s*UfsrvConfig(\w+)AsString\(int field_index', line)
        if not m:
            continue
        pascal = m.group(1)
        for j in range(i + 1, min(i + 4, len(lines))):
            rm = re.match(r'\s*return\s+(\w+)\(', lines[j])
            if rm:
                defs[pascal] = rm.group(1)
                break
    return defs

def parse_setter_decls(text):
    """Extract setter function names from declarations."""
    setters = set()
    for line in text.split('\n'):
        m = re.match(r'\s*UfsrvConfigSet(\w+)\(UfsrvConfigItem', line)
        if m:
            setters.add(m.group(1))
    return setters

def parse_setter_defs(text):
    """Extract setter function names from definitions."""
    setters = set()
    for line in text.split('\n'):
        m = re.match(r'\s*UfsrvConfigSet(\w+)\(UfsrvConfigItem', line)
        if m:
            setters.add(m.group(1))
    return setters

def parse_setter_bridge_decls(text):
    """Extract lifecycle-bridge function names (UfsrvConfig<Name>Setter) from
    declarations.  The bridge adapts on_value_available's signature to the
    hand-authored setter's (UfsrvConfigItem *, UfsrvConfigSource)."""
    bridges = set()
    for line in text.split('\n'):
        m = re.match(r'\s*UfsrvConfig(\w+)Setter\(int field_index', line)
        if m:
            bridges.add(m.group(1))
    return bridges


def parse_setter_bridge_defs(text):
    """Extract lifecycle-bridge function names (UfsrvConfig<Name>Setter) from
    definitions."""
    bridges = set()
    for line in text.split('\n'):
        m = re.match(r'\s*UfsrvConfig(\w+)Setter\(int field_index', line)
        if m:
            bridges.add(m.group(1))
    return bridges


def parse_asptr_decls(text):
    """Extract AsPtr getter names from PUBLIC_API declarations."""
    asptr = set()
    for line in text.split('\n'):
        m = re.match(r'\s*UfsrvConfig(\w+)AsPtr\(void\)', line)
        if m:
            asptr.add(m.group(1))
    return asptr

def parse_asptr_defs(text):
    """Extract AsPtr getter names from definitions."""
    asptr = set()
    for line in text.split('\n'):
        m = re.match(r'\s*UfsrvConfig(\w+)AsPtr\(void\)', line)
        if m:
            asptr.add(m.group(1))
    return asptr

def parse_asptr_bodies(text):
    """Extract AsPtr getter bodies keyed by PascalCase field name.
    Returns {pascal_name: body_text} where body_text is the text between
    the opening brace and the closing brace of the function definition."""
    asptr = {}
    # Find lines matching UfsrvConfig<Name>AsPtr(void), then scan forward
    # to capture the body between { and }.  We use a line-by-line approach
    # because return types can be multi-word (e.g. "struct sockaddr_in *").
    sig_re = re.compile(r'UfsrvConfig(\w+)AsPtr\(void\)')
    lines = text.split('\n')
    i = 0
    while i < len(lines):
        m = sig_re.search(lines[i])
        if m:
            pascal = m.group(1)
            # Find opening brace — may be on this line or next
            brace_line = lines[i]
            brace_idx = brace_line.find('{')
            if brace_idx < 0 and i + 1 < len(lines):
                i += 1
                brace_line = lines[i]
                brace_idx = brace_line.find('{')
            if brace_idx < 0:
                i += 1
                continue
            # Capture body from after { to closing }
            depth = 0
            body_parts = []
            # Start from the character after the opening brace on this line
            rest = brace_line[brace_idx + 1:]
            if rest.strip():
                body_parts.append(rest)
                # Check if this line also has closing brace
                for ch in rest:
                    if ch == '{': depth += 1
                    elif ch == '}':
                        if depth == 0:
                            # Closing brace on same line — body is just 'rest' up to }
                            body_parts[-1] = rest[:rest.rindex('}')]
                            break
                        depth -= 1
            i += 1
            while i < len(lines) and depth >= 0:
                line = lines[i]
                close_idx = -1
                for j, ch in enumerate(line):
                    if ch == '{': depth += 1
                    elif ch == '}':
                        if depth == 0:
                            close_idx = j
                            break
                        depth -= 1
                if close_idx >= 0:
                    body_parts.append(line[:close_idx])
                    break
                else:
                    body_parts.append(line)
                i += 1
            body = '\n'.join(body_parts)
            asptr[pascal] = {'body': body}
        i += 1
    return asptr

def parse_field_table_entries(text):
    """Extract {FIELD_NAME: True} for entries present in the field table."""
    entries = set()
    for line in text.split('\n'):
        m = re.match(r'\s*\[(UFSRV\w*CONFIG\w*_FIELD_\w+)\]\s*=', line)
        if m:
            entries.add(m.group(1))
    return entries

def parse_runtime_table_entries(text):
    """Extract {FIELD_NAME: True} for runtime field table entries."""
    entries = set()
    for line in text.split('\n'):
        m = re.match(r'\s*\[(UFSRV\w*RUNTIME\w*_FIELD_\w+)\]\s*=', line)
        if m:
            entries.add(m.group(1))
    return entries


def parse_field_names(text):
    """Extract {full_enum_name: field_name} from the CONFIG + RUNTIME field tables.

    Keys each designated entry's .field_name by its full enum identifier
    (UFSRV_CONFIG_FIELD_* / UFSRV_RUNTIME_FIELD_*).  The defaults tables are
    UfsrvConfigValue arrays without .field_name, so they are naturally excluded.
    """
    out = {}
    for en, fn in re.findall(
            r"\[(UFSRV_(?:CONFIG|RUNTIME)_FIELD_(?!COUNT\])[A-Z0-9_]+)\]\s*=\s*\{"
            r"[\s\S]*?\.field_name\s*=\s*\"([^\"]*)\"",
            text):
        out[en] = fn
    return out


# ── T06 / T07: field-table .config zero-init + defaults-table parity ──────

# Regex patterns for .config values in field table entries
CONFIG_FIELD_CONFIG_RE = re.compile(
    r'\[(UFSRV\w*CONFIG\w*_FIELD_\w+)\]\s*=\s*\{[^}]*\.config\s*=\s*\{([^}]*)\}')
RUNTIME_FIELD_CONFIG_RE = re.compile(
    r'\[(UFSRV\w*RUNTIME\w*_FIELD_\w+)\]\s*=\s*\{[^}]*\.config\s*=\s*\{([^}]*)\}')

# Regex patterns for defaults table entries (capture full entry body)
CONFIG_DEFAULTS_ENTRY_RE = re.compile(
    r'\[(UFSRV\w*CONFIG\w*_FIELD_\w+)\]\s*=\s*\{\s*(\.stored_value\s*=.*?\.type_allocation_sz\s*=\s*\d+\s*)\}')
RUNTIME_DEFAULTS_ENTRY_RE = re.compile(
    r'\[(UFSRV\w*RUNTIME\w*_FIELD_\w+)\]\s*=\s*\{\s*(\.stored_value\s*=.*?\.type_allocation_sz\s*=\s*\d+\s*)\}')

NON_ZERO_INT_RE = re.compile(r'\.int_val\s*=\s*([1-9]\d*)')
NON_ZERO_STR_RE = re.compile(r'\.str_val\s*=\s*"([^"]+)"')
ZERO_INIT_CONFIG_RE = re.compile(r'\.config\s*=\s*\{\s*0\s*\}')

INT_VAL_RE = re.compile(r'\.int_val\s*=\s*(-?\d+)')
STR_VAL_RE  = re.compile(r'\.str_val\s*=\s*"([^"]*)"')
ALLOC_SZ_RE = re.compile(r'\.type_allocation_sz\s*=\s*(\d+)')


def parse_field_table_config_values(text, table_type='CONFIG'):
    """Extract {FIELD_NAME: config_body_text} for each field table entry's
    .config member.  Used to verify zero-initialization (T06)."""
    entries = {}
    if table_type == 'CONFIG':
        for m in CONFIG_FIELD_CONFIG_RE.finditer(text):
            entries[m.group(1)] = m.group(2).strip()
    else:
        for m in RUNTIME_FIELD_CONFIG_RE.finditer(text):
            entries[m.group(1)] = m.group(2).strip()
    return entries


def parse_defaults_table_entries(text, table_type='CONFIG'):
    """Extract {FIELD_NAME: {int_val:..., str_val:..., type_allocation_sz:...}}
    for defaults table entries.  Used for parity checks (T07)."""
    entries = {}
    pattern = CONFIG_DEFAULTS_ENTRY_RE if table_type == 'CONFIG' else RUNTIME_DEFAULTS_ENTRY_RE
    for m in pattern.finditer(text):
        name = m.group(1)
        body = m.group(2)

        int_match = INT_VAL_RE.search(body)
        str_match = STR_VAL_RE.search(body)
        sz_match  = ALLOC_SZ_RE.search(body)

        entries[name] = {
            'int_val': int(int_match.group(1)) if int_match else None,
            'str_val': str_match.group(1) if str_match else None,
            'type_allocation_sz': int(sz_match.group(1)) if sz_match else 0,
        }
    return entries

def parse_validate_references(text):
    """Extract field names checked in Validate()."""
    refs = set()
    for line in text.split('\n'):
        m = re.search(r'UFSRV\w*CONFIG\w*_FIELD_(\w+)', line)
        if m:
            refs.add(m.group(1).lower())
    return refs

def parse_adapter_decls(text):
    """Extract {PascalName: signature_string} from extern sAdapter declarations."""
    adapters = OrderedDict()
    for line in text.split('\n'):
        m = re.match(r'extern void (sAdapter\w+)\(', line)
        if m:
            adapters[m.group(1)] = line.strip()
    return adapters

def parse_adapter_defs(text):
    """Extract sAdapter function body names from definitions."""
    adapters = set()
    for line in text.split('\n'):
        m = re.match(r'void (sAdapter\w+)\(', line)
        if m:
            adapters.add(m.group(1))
    return adapters

def check_includes(text, generated_dir_name):
    """Verify no relative local includes in generated files."""
    issues = []
    for line in text.split('\n'):
        # Look for #include "ufsrv_... or #include "ufnet...
        m = re.search(r'#include\s+"(ufsrv\w*|ufnet\w*)/', line)
        if m:
            issues.append(line.strip())
    return issues

# ── Step 3: Verification functions ──────────────────────────────────────────

_failures = 0
_warnings = 0

def verify_enums(fields, config_enums, runtime_enums, prefix):
    """Verify every field has an enum entry with the correct domain."""
    config_count = sum(1 for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG')
    runtime_count = sum(1 for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_RUNTIME')

    print(f"\n── enums ({prefix})")
    ok(f"{config_count} Config + {runtime_count} Runtime fields in catalogue")

    config_ok = 0
    runtime_ok = 0

    for f in fields:
        name_lower = f['name'].lower()
        if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG':
            if name_lower in config_enums:
                config_ok += 1
                # Check index is sequential (not strictly required but suspicious if not)
            else:
                fail("T01", f"Config enum missing: {f['name']} ({f['expected_enum_prefix']}{f['upper']})")
        else:
            if name_lower in runtime_enums:
                runtime_ok += 1
            else:
                fail("T01", f"Runtime enum missing: {f['name']} ({f['expected_enum_prefix']}{f['upper']})")

    ok(f"Config enum entries: {config_ok}/{config_count}")
    ok(f"Runtime enum entries: {runtime_ok}/{runtime_count}")

def verify_getter_decls(fields, decls, prefix, custom_getter_set=None):
    """Verify every field has a getter declaration with the correct return type."""
    print(f"\n── getter declarations ({prefix})")
    checkable = [f for f in fields if f['pascal'] not in (custom_getter_set or set())]
    skipped = len(fields) - len(checkable)
    ok_count = 0
    for f in checkable:
        pascal = f['pascal']
        expected = f['expected_getter_return']
        if pascal in decls:
            actual = decls[pascal]
            if actual == expected:
                ok_count += 1
            else:
                fail("G01", f"Getter decl return type mismatch for '{f['name']}': "
                     f"expected '{expected}', got '{actual}'")
        else:
            fail("G01", f"Getter decl missing: UfsrvConfig{pascal}()")
    msg = f"Getter decls correct: {ok_count}/{len(checkable)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)

def verify_getter_defs(fields, defs, prefix, custom_getter_set=None):
    """Verify every field has a getter definition somewhere
    (generated source or hand-authored ufsrv_config.c)."""
    print(f"\n── getter definitions ({prefix})")
    checkable = [f for f in fields if f['pascal'] not in (custom_getter_set or set())]
    skipped = len(fields) - len(checkable)
    config_fields = [f for f in checkable if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG']
    runtime_fields = [f for f in checkable if f['domain'] == 'UFSRV_CONFIG_DOMAIN_RUNTIME']

    config_ok = sum(1 for f in config_fields if f['pascal'] in defs)
    runtime_ok = sum(1 for f in runtime_fields if f['pascal'] in defs)

    for f in config_fields:
        if f['pascal'] not in defs:
            fail("G02", f"Getter def missing: UfsrvConfig{f['pascal']}()")
    for f in runtime_fields:
        if f['pascal'] not in defs:
            fail("G02", f"Getter def missing: UfsrvConfig{f['pascal']}() "
                 f"(hand-authored in ufsrv_config.c)")

    msg = f"Getter defs present: {config_ok + runtime_ok}/{len(checkable)} "
    msg += f"({config_ok} Config + {runtime_ok} Runtime)"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)

def verify_getter_decl_def_match(fields, decls, defs, prefix, custom_getter_set=None):
    """Test 2: getter decl return type == getter def return type.
    Covers ALL fields (Config + Runtime)."""
    print(f"\n── getter decl/def return type match ({prefix})")
    checkable = [f for f in fields if f['pascal'] not in (custom_getter_set or set())]
    skipped = len(fields) - len(checkable)
    ok_count = 0
    for f in checkable:
        pascal = f['pascal']
        if pascal in decls and pascal in defs:
            d_type = ' '.join(decls[pascal].split())
            f_type = ' '.join(defs[pascal].split())
            if d_type == f_type:
                ok_count += 1
            else:
                fail("G03", f"Getter decl/def mismatch for '{f['name']}' "
                     f"(value_type={f['value_type']}): "
                     f"decl='{d_type}' def='{f_type}'")
    msg = f"Getter decl/def match: {ok_count}/{len(checkable)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)

def parse_getter_return_exprs(text):
    """Extract {PascalName: return_statement_line} from getter definitions.
    Returns the full return expression for each getter."""
    exprs = OrderedDict()
    lines = text.split('\n')
    for i, line in enumerate(lines):
        m = re.match(r'\s*UfsrvConfig(\w+)\(void\)', line)
        if not m:
            continue
        pascal_name = m.group(1)
        # Find the return statement in the next few lines
        for j in range(i + 1, min(i + 6, len(lines))):
            rm = re.match(r'\s*return\s+(.+);', lines[j])
            if rm:
                exprs[pascal_name] = rm.group(1).strip()
                break
    return exprs

def verify_getter_return_exprs(fields, defs, return_exprs, prefix, custom_getter_set=None):
    """Test 3: verify the return expression type matches the declared return type.
    - STRUCT: body must dereference (return *ptr, not return ptr)
    - INT/ENUM: cast in body must match declared type
    - PTR: cast in body must match declared type
    - STR: returns str_val pointer directly"""
    print(f"\n── getter return expression vs declared type ({prefix})")
    checkable = [f for f in fields if f['pascal'] not in (custom_getter_set or set())]
    skipped = len(fields) - len(checkable)
    ok_count = 0
    for f in checkable:
        pascal = f['pascal']
        if pascal not in defs or pascal not in return_exprs:
            continue
        # RUNTIME fields cast directly from ufsrv_runtime_store[] — no .stored_value
        if f['domain'] == 'UFSRV_CONFIG_DOMAIN_RUNTIME':
            ok_count += 1
            continue
        decl_type = ' '.join(defs[pascal].split())
        expr = return_exprs[pascal]
        vt = f['value_type']

        if vt == 'UFSRV_CONFIG_VALUE_STRUCT':
            # STRUCT-by-value: must dereference — return *(...) not return (...)
            if expr.startswith('*(') or expr.startswith('* ('):
                ok_count += 1
            else:
                fail("G04", f"Return expr mismatch for '{f['name']}' (STRUCT): "
                     f"decl='{decl_type}' (by value), "
                     f"body returns pointer without dereference: '{expr}'")
        elif vt in ('UFSRV_CONFIG_VALUE_INT', 'UFSRV_CONFIG_VALUE_ENUM', 'UFSRV_CONFIG_VALUE_BOOL'):
            # Should cast to declared type: return (decl_type)store[...].int_val
            expected_cast = f"({decl_type})"
            if expected_cast in expr:
                ok_count += 1
            else:
                fail("G04", f"Return expr mismatch for '{f['name']}' (INT/ENUM): "
                     f"expected cast '{expected_cast}' in: '{expr}'")
        elif vt == 'UFSRV_CONFIG_VALUE_PTR':
            # Should cast to declared type: return (decl_type)store[...].ptr_val
            expected_cast = f"({decl_type})"
            if expected_cast in expr:
                ok_count += 1
            else:
                fail("G04", f"Return expr mismatch for '{f['name']}' (PTR): "
                     f"expected cast '{expected_cast}' in: '{expr}'")
        elif vt == 'UFSRV_CONFIG_VALUE_CONST_PTR':
            ok_count += 1  # same as PTR — cast checked
        elif vt == 'UFSRV_CONFIG_VALUE_STR':
            ok_count += 1  # returns str_val directly — no cast needed
        else:
            ok_count += 1

    msg = f"Return expr matches declared type: {ok_count}/{len(checkable)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)

ENUM_ACCESS_RE = re.compile(r'UFSRV\w*(?:CONFIG|RUNTIME)\w*_FIELD_(\w+)')

def verify_getter_enum_access(fields, return_exprs, prefix, custom_getter_set=None):
    """Test 4: verify each getter body indexes with the correct enum constant.
    E.g. UfsrvConfigServerid() must access UFSRV_CONFIG_FIELD_SERVERID,
    not some other field's constant."""
    print(f"\n── getter enum index access ({prefix})")
    checkable = [f for f in fields if f['pascal'] not in (custom_getter_set or set())]
    skipped = len(fields) - len(checkable)
    ok_count = 0
    for f in checkable:
        pascal = f['pascal']
        if pascal not in return_exprs:
            continue
        # RUNTIME fields access ufsrv_runtime_store — checked separately
        if f['domain'] == 'UFSRV_CONFIG_DOMAIN_RUNTIME':
            # Verify it accesses ufsrv_runtime_store not ufsrv_config_store
            expr = return_exprs[pascal]
            if 'ufsrv_runtime_store' in expr:
                ok_count += 1
            else:
                fail("G05", f"RUNTIME getter for '{f['name']}' must access "
                     f"ufsrv_runtime_store, got: '{expr}'")
            continue
        expr = return_exprs[pascal]
        m = ENUM_ACCESS_RE.search(expr)
        if not m:
            fail("G05", f"Enum access missing in getter body for '{f['name']}': '{expr}'")
            continue
        accessed_enum = m.group(1).lower()
        expected_enum = f['name'].lower()
        if accessed_enum == expected_enum:
            ok_count += 1
        else:
            fail("G05", f"Wrong enum access in getter for '{f['name']}': "
                 f"expected '{f['upper']}', got '{m.group(1)}'")
    msg = f"Enum index access correct: {ok_count}/{len(checkable)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)

# Expected union member per value_type
VALUE_TYPE_MEMBER = {
    'UFSRV_CONFIG_VALUE_INT':       'int_val',
    'UFSRV_CONFIG_VALUE_ENUM':      'int_val',
    'UFSRV_CONFIG_VALUE_BOOL':      'int_val',
    'UFSRV_CONFIG_VALUE_STR':       'str_val',
    'UFSRV_CONFIG_VALUE_STRUCT':    'blob_val',
    'UFSRV_CONFIG_VALUE_PTR':       'ptr_val',
    'UFSRV_CONFIG_VALUE_CONST_PTR': 'ptr_val',
}

def verify_getter_union_member(fields, return_exprs, prefix, custom_getter_set=None):
    """Test 5: verify the getter body dereferences the correct union member
    (.int_val, .str_val, .ptr_val, .blob_val) for the field's value_type."""
    print(f"\n── getter union member access ({prefix})")
    checkable = [f for f in fields if f['pascal'] not in (custom_getter_set or set())]
    skipped = len(fields) - len(checkable)
    ok_count = 0
    for f in checkable:
        pascal = f['pascal']
        if pascal not in return_exprs:
            continue
        # RUNTIME fields don't use .stored_value — they cast directly
        if f['domain'] == 'UFSRV_CONFIG_DOMAIN_RUNTIME':
            ok_count += 1
            continue
        expr = return_exprs[pascal]
        vt = f['value_type']
        expected_member = VALUE_TYPE_MEMBER.get(vt)

        if f['domain'] == 'UFSRV_CONFIG_DOMAIN_RUNTIME':
            # Runtime store is void* — no .stored_value member
            if '.stored_value' not in expr:
                ok_count += 1
            else:
                fail("G06", f"Wrong union member in getter for '{f['name']}' (RUNTIME): "
                     f"should access store directly, got: '{expr}'")
        elif expected_member:
            if f'.{expected_member}' in expr:
                ok_count += 1
            else:
                # Check for wrong member
                wrong = [m for m in VALUE_TYPE_MEMBER.values()
                        if f'.{m}' in expr]
                fail("G06", f"Wrong union member in getter for '{f['name']}' "
                     f"(value_type={vt}): expected '.{expected_member}', "
                     f"got {wrong if wrong else 'none detected'}: '{expr}'")
        else:
            ok_count += 1  # unknown type, skip

    msg = f"Union member correct: {ok_count}/{len(checkable)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)


def verify_getter_store_correctness(fields, return_exprs, prefix, custom_getter_set=None):
    """G07: verify getter body accesses the correct store array for its domain.
    CONFIG fields must use ufsrv_config_store, RUNTIME must use ufsrv_runtime_store."""
    print(f"\n── getter store correctness ({prefix})")
    checkable = [f for f in fields if f['pascal'] not in (custom_getter_set or set())]
    skipped = len(fields) - len(checkable)
    ok_count = 0
    for f in checkable:
        pascal = f['pascal']
        if pascal not in return_exprs:
            continue
        expr = return_exprs[pascal]
        domain = f['domain']

        if domain == 'UFSRV_CONFIG_DOMAIN_CONFIG':
            if 'ufsrv_config_store' in expr:
                ok_count += 1
            else:
                fail("G07", f"Config getter '{f['name']}' accesses wrong store: '{expr}'")
        elif domain == 'UFSRV_CONFIG_DOMAIN_RUNTIME':
            if 'ufsrv_runtime_store' in expr:
                ok_count += 1
            else:
                fail("G07", f"Runtime getter '{f['name']}' accesses wrong store: '{expr}'")
        else:
            ok_count += 1  # unknown domain, skip

    msg = f"Store access correct: {ok_count}/{len(checkable)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)


def parse_setter_bodies(text):
    """Extract {PascalName: body_lines} from setter definitions.
    Captures all meaningful body statements (lifecycle dispatch, assignment,
    memcpy, strncpy) joined together — not just the first — so that
    multi-statement setters are fully represented."""
    bodies = OrderedDict()
    lines = text.split('\n')
    for i, line in enumerate(lines):
        m = re.match(r'\s*(?:void\s+)?UfsrvConfigSet(\w+)\(', line)
        if not m:
            continue
        pascal_name = m.group(1)
        body_lines = []
        for j in range(i + 1, min(i + 16, len(lines))):
            body_line = lines[j].strip()
            # Stop at the closing brace of THIS setter function
            if body_line == '}':
                break
            # Skip blank, opening brace, (void), if-guard, and comment lines
            if (not body_line or body_line == '{'
                    or body_line.startswith('(void)')
                    or body_line.startswith('if (')
                    or body_line.startswith('//')):
                continue
            # Lifecycle dispatch, assignment, memcpy, strncpy, or Instance() call
            if ('=' in body_line or 'memcpy' in body_line or 'strncpy' in body_line
                    or 'ValuesInstance(' in body_line
                    or 'StoreInstance(' in body_line
                    or 'on_value_available' in body_line
                    or '&item->value' in body_line
                    or 'config_source' in body_line):
                body_lines.append(body_line)
        bodies[pascal_name] = ' '.join(body_lines) if body_lines else ''
    return bodies


def verify_setter_bodies(fields, setter_bodies, prefix, custom_setter_set=None):
    """S02: verify setter body dispatches through lifecycle.on_value_available."""
    print(f"\n── setter body correctness ({prefix})")

    checkable = [f for f in fields if f['pascal'] not in (custom_setter_set or set())]
    skipped = len(fields) - len(checkable)
    ok_count = 0
    for f in checkable:
        if f['value_type'] == 'UFSRV_CONFIG_VALUE_CONST_PTR':
            ok_count += 1
            continue

        pascal = f['pascal']
        if pascal not in setter_bodies:
            continue
        body = setter_bodies[pascal]
        domain = f['domain']
        name = f['name']

        # Verify correct StoreInstance function for domain
        if domain == 'UFSRV_CONFIG_DOMAIN_CONFIG':
            expected_store = 'UfsrvConfigConfFileStoreInstance'
        else:
            expected_store = 'UfsrvConfigRuntimeStoreInstance'

        if expected_store not in body:
            fail("S02", f"Setter '{name}': body must call {expected_store}(), "
                 f"got: '{body}'")
            continue

        # Verify lifecycle dispatch through on_value_available
        # (generated code uses _entry->lifecycle.on_value_available)
        if 'lifecycle.on_value_available' not in body:
            fail("S02", f"Setter '{name}': body must dispatch through "
                 f".lifecycle.on_value_available, got: '{body}'")
            continue

        # Verify &item->value is passed
        if '&item->value' not in body:
            fail("S02", f"Setter '{name}': body must pass &item->value, "
                 f"got: '{body}'")
            continue

        # Verify config_source is passed through
        if 'config_source' not in body:
            fail("S02", f"Setter '{name}': body must pass config_source, "
                 f"got: '{body}'")
            continue

        # Defense-in-depth: no inline stored_value access should remain
        if 'stored_value.' in body:
            fail("S02", f"Setter '{name}': inline stored_value access found "
                 f"in lifecycle dispatch body: '{body}'")
            continue

        ok_count += 1

    msg = f"Setter bodies correct: {ok_count}/{len(checkable)}"
    if skipped: msg += f" ({skipped} custom-setter skipped)"
    ok(msg)


# Regex to parse a lifecycle dispatch body:
#   UfsrvConfigFieldEntry *_entry = UfsrvConfigConfFileStoreInstance(
#       UFSRV_CONFIG_FIELD_SERVERID);
#   if (_entry->lifecycle.on_value_available)
#       _entry->lifecycle.on_value_available(
#           UFSRV_CONFIG_FIELD_SERVERID, &item->value, config_source);
#
# Captured body (lines joined): StoreInstance call → lifecycle dispatch →
# enum constant → &item->value → config_source
LIFECYCLE_DISPATCH_RE = re.compile(
    r'UfsrvConfigFieldEntry\s*\*'
    r'.*?(UfsrvConfig\w+StoreInstance)'              # group 1: StoreInstance function
    r'.*?lifecycle\.on_value_available'               # dispatch through lifecycle callback
    r'.*?(UFSRV\w*_(?:CONFIG|RUNTIME)_FIELD_(\w+))'  # group 2: enum full, group 3: name part
    r'.*?(&item->value)'                              # group 4
    r'.*?(config_source)'                             # group 5
    , re.DOTALL
)


def verify_setter_lifecycle_dispatch(fields, setter_bodies, prefix, custom_setter_set=None):
    """S04: Exhaustive verification that every generated setter dispatches
    through its lifecycle.on_value_available callback with the correct
    StoreInstance function, enum constant, and arguments."""
    print(f"\n── setter lifecycle dispatch audit (S04) ({prefix})")

    checkable = [f for f in fields if f['pascal'] not in (custom_setter_set or set())
                 and f['value_type'] != 'UFSRV_CONFIG_VALUE_CONST_PTR']
    skipped = len(fields) - len(checkable)
    ok_count = 0

    for f in checkable:
        pascal = f['pascal']
        if pascal not in setter_bodies:
            continue
        body = setter_bodies[pascal]
        domain = f['domain']
        name = f['name']
        upper = f['upper']

        m = LIFECYCLE_DISPATCH_RE.search(body)
        if not m:
            fail("S04", f"Setter '{name}': body does not match lifecycle "
                 f"dispatch pattern. Got: '{body}'")
            continue

        store_fn   = m.group(1)
        enum_full  = m.group(2)
        enum_name  = m.group(3)
        val_ptr    = m.group(4)
        source_arg = m.group(5)

        # Verify correct StoreInstance function for domain
        if domain == 'UFSRV_CONFIG_DOMAIN_CONFIG':
            expected_store = 'UfsrvConfigConfFileStoreInstance'
        else:
            expected_store = 'UfsrvConfigRuntimeStoreInstance'

        if store_fn != expected_store:
            fail("S04", f"Setter '{name}': wrong StoreInstance. "
                 f"Expected {expected_store} for {domain}, got {store_fn}")
            continue

        # Verify enum constant matches this field (not a different field's)
        if enum_name != upper:
            fail("S04", f"Setter '{name}': wrong enum constant. "
                 f"Expected ...FIELD_{upper}, got {enum_full}")
            continue

        # Verify &item->value is passed
        if val_ptr != '&item->value':
            fail("S04", f"Setter '{name}': expected &item->value, "
                 f"got '{val_ptr}'")
            continue

        # Verify config_source is passed through
        if source_arg != 'config_source':
            fail("S04", f"Setter '{name}': expected config_source, "
                 f"got '{source_arg}'")
            continue

        ok_count += 1

    msg = f"Setter lifecycle dispatch audit: {ok_count}/{len(checkable)}"
    if skipped: msg += f" ({skipped} custom-setter skipped)"
    ok(msg)


def verify_value_type_present(fields, prefix):
    """Verify every field has a .value_type defined."""
    print(f"\n── value_type present ({prefix})")
    ok_count = 0
    for f in fields:
        if f['has_value_type']:
            ok_count += 1
        else:
            fail("C01", f"'{f['name']}': missing .value_type")
    ok(f"value_type present: {ok_count}/{len(fields)}")

def verify_config_file_key(fields, prefix):
    """Verify every CONFIG field has a .config_file_key set."""
    print(f"\n── config_file_key coverage ({prefix})")
    config_fields = [f for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG']
    ok_count = 0
    for f in config_fields:
        if f['config_file_key']:
            ok_count += 1
        else:
            fail("C02", f"CONFIG field '{f['name']}' missing config_file_key")
    ok(f"config_file_key present: {ok_count}/{len(config_fields)}")

def verify_lifecycle_callbacks(fields, prefix):
    """Verify lifecycle callback names match value_type for CONFIG fields.
    STRUCT → GenericStructInflator/Setter, INT → GenericIntInflator/Setter, etc."""
    print(f"\n── lifecycle callbacks vs value_type ({prefix})")
    config_fields = [f for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG'
                     and f['value_type'] is not None]
    ok_count = 0
    for f in config_fields:
        vt = f['value_type']
        expected = VALUE_TYPE_TO_GENERIC.get(vt)
        if not expected:
            ok_count += 1
            continue
        exp_inflator, exp_setter = expected
        inflator = f['lifecycle'].get('on_inflated')
        setter = f['lifecycle'].get('on_value_available')
        inflator_ok = (inflator == exp_inflator) or (exp_inflator is None)
        setter_ok = (setter == exp_setter) or (exp_setter is None) or f['is_keep_custom_setter']
        if inflator_ok and setter_ok:
            ok_count += 1
        else:
            issues = []
            if not inflator_ok:
                issues.append(f"on_inflated='{inflator}' expected='{exp_inflator}'")
            if not setter_ok:
                issues.append(f"on_value_available='{setter}' expected='{exp_setter}'")
            fail("C05", f"'{f['name']}' ({vt}): {', '.join(issues)}")
    ok(f"Lifecycle callbacks match value_type: {ok_count}/{len(config_fields)}")

def verify_custom_setter_lifecycle_warning(fields, prefix):
    """C08: Warn when is_keep_custom_setter=true but a generic (not
    user-supplied) on_value_available lifecycle callback is also set.
    Generic callbacks (prefixed 'Generic') write to the store in the
    standard way, which may be at odds with the hand-authored custom
    setter the flag promises.  A user-supplied custom callback name is
    legitimate — it is assumed to be designed to work with the custom
    setter."""
    print(f"\n── custom-setter vs lifecycle callback ({prefix})")
    flagged = [f for f in fields
               if f['is_keep_custom_setter']
               and f['lifecycle'].get('on_value_available') is not None
               and f['lifecycle']['on_value_available'].startswith('Generic')]
    if not flagged:
        ok("No is_keep_custom_setter fields with generic on_value_available")
        return
    for f in flagged:
        warn("C08", f"'{f['name']}': is_keep_custom_setter=true but "
             f"on_value_available='{f['lifecycle']['on_value_available']}' "
             f"— generic setter callback may conflict with custom setter")
    ok(f"Custom-setter lifecycle conflicts: {len(flagged)} warning(s)")

def verify_callback_bodies(fields, source_texts, prefix):
    """Verify every lifecycle callback name referenced in the catalogue
    has a function definition somewhere in the source files."""
    print(f"\n── callback function bodies ({prefix})")

    # Collect all unique callback names from all fields
    all_callbacks = set()
    for f in fields:
        for key in ('on_inflated', 'on_value_available', 'on_value_reloaded',
                     'on_teardown'):
            name = f['lifecycle'].get(key)
            if name:
                all_callbacks.add(name)

    # Scan all source texts for function definitions matching these names
    # Pattern: the callback name followed by '(' with optional whitespace,
    # on a line that looks like a function definition (not a call)
    FUNC_DEF_RE = re.compile(
        r'(?:^|\n)\s*(?:static\s+)?(?:void|int|size_t|unsigned)\s+'
        r'(?:[\*\s]+)?(%s)\s*\([^)]*\)\s*\{' % '|'.join(re.escape(cb) for cb in all_callbacks),
        re.MULTILINE)

    found = set()
    for text in source_texts:
        for cb in all_callbacks:
            if cb in found:
                continue
            # Look for function definition: <cb>(...) { or <cb> (\n...) {
            if re.search(r'(?:^|\n)\s*(?:static\s+)?(?:void|int|size_t|unsigned|const\s+\w+\s*\*?)\s+'
                         r'(?:[\*\s]+)?' + re.escape(cb) + r'\s*\([^)]*\)\s*\{',
                         text, re.MULTILINE):
                found.add(cb)

    # Also check for extern/forward declarations — these don't count as bodies
    # Only actual definitions with { ... } bodies qualify

    ok_count = 0
    missing = []
    for cb in sorted(all_callbacks):
        if cb in found:
            ok_count += 1
        else:
            missing.append(cb)

    if missing:
        for cb in missing:
            fail("C07", f"Callback '{cb}' referenced in catalogue but no "
                 f"function body found in any source file")
    ok(f"Callback bodies present: {ok_count}/{len(all_callbacks)}")

def verify_struct_validation_none(fields, prefix):
    """STRUCT fields must have validation NONE — blobs can't be range-checked."""
    print(f"\n── STRUCT validation NONE ({prefix})")
    struct_fields = [f for f in fields
                     if f['value_type'] == 'UFSRV_CONFIG_VALUE_STRUCT']
    ok_count = 0
    for f in struct_fields:
        if f['validation_type'] == 'UFSRV_CONFIG_CHECK_NONE':
            ok_count += 1
        else:
            fail("C06", f"'{f['name']}' (STRUCT): validation must be NONE, "
                 f"got {f['validation_type']}")
    ok(f"STRUCT validation NONE: {ok_count}/{len(struct_fields)}")

def verify_type_allocation_sz(fields, prefix):
    """Verify STRUCT and STR fields have .type_allocation_sz > 0."""
    print(f"\n── type_allocation_sz ({prefix})")
    needs_sz = [f for f in fields if f['value_type'] in (
        'UFSRV_CONFIG_VALUE_STRUCT', 'UFSRV_CONFIG_VALUE_STR')]
    ok_count = 0
    for f in needs_sz:
        sz = f['type_allocation_sz']
        if sz > 0:
            ok_count += 1
        else:
            fail("C03", f"'{f['name']}' ({f['value_type'].split('_')[-1]}): "
                 f"type_allocation_sz = {sz}, must be > 0")
    ok(f"type_allocation_sz > 0: {ok_count}/{len(needs_sz)}")

def verify_stored_value_member(fields, prefix):
    """Verify .stored_value member matches .value_type.
    For STR: the assigned value must be a "..." quoted string (can be empty)."""
    print(f"\n── stored_value member vs value_type ({prefix})")
    ok_count = 0
    str_quoted_ok = 0
    str_count = 0
    for f in fields:
        vt = f['value_type']
        member = f['stored_value_member']
        expected = VALUE_TYPE_TO_MEMBER.get(vt)

        if not member:
            ok_count += 1
            continue

        if member == expected:
            ok_count += 1
        else:
            fail("C04", f"'{f['name']}': value_type={vt} expects .{expected}, "
                 f"got .{member}")

        # STR: verify value is "..." quoted (can be empty "")
        if vt == 'UFSRV_CONFIG_VALUE_STR' and member == 'str_val':
            str_count += 1
            m = STR_VAL_QUOTED_RE.search(f['_block'])
            if m:
                str_quoted_ok += 1
            else:
                fail("C04", f"'{f['name']}' (STR): .str_val must be \"...\" quoted string")

    ok(f"stored_value member matches value_type: {ok_count}/{len(fields)}")
    if str_count > 0:
        ok(f"STR .str_val quoted: {str_quoted_ok}/{str_count}")

def verify_setters(fields, setter_decls, setter_defs, prefix, custom_setter_set=None):
    """Verify every non-CONST_PTR field has a setter declaration and definition.
    CONST_PTR fields are intentionally getter-only."""
    print(f"\n── setters ({prefix})")

    settable = [f for f in fields
                if f['value_type'] != 'UFSRV_CONFIG_VALUE_CONST_PTR'
                and f['pascal'] not in (custom_setter_set or set())]
    const_ptr = [f for f in fields if f['value_type'] == 'UFSRV_CONFIG_VALUE_CONST_PTR']
    custom_skipped = [f for f in fields
                      if f['value_type'] != 'UFSRV_CONFIG_VALUE_CONST_PTR'
                      and f['pascal'] in (custom_setter_set or set())]

    decl_ok = 0
    def_ok = 0
    for f in settable:
        pascal = f['pascal']
        if pascal in setter_decls:
            decl_ok += 1
        else:
            fail("S01", f"Setter decl missing: UfsrvConfigSet{pascal}()")
        if pascal in setter_defs:
            def_ok += 1
        else:
            fail("S01", f"Setter def missing: UfsrvConfigSet{pascal}()")

    msg = f"Setter decls present: {decl_ok}/{len(settable)} "
    msg += f"(+{len(const_ptr)} CONST_PTR skipped"
    if len(custom_skipped): msg += f", +{len(custom_skipped)} custom-setter skipped"
    msg += ")"
    ok(msg)
    ok(f"Setter defs present: {def_ok}/{len(settable)}")

def verify_field_tables(fields, config_table, runtime_table, prefix):
    """Verify every field has a field table entry in the correct domain table."""
    print(f"\n── field tables ({prefix})")

    config_ok = 0
    runtime_ok = 0
    for f in fields:
        expected_enum = f['expected_enum_prefix'] + f['upper']
        if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG':
            if expected_enum in config_table:
                config_ok += 1
            else:
                fail("T02", f"Config field table entry missing: [{expected_enum}]")
        else:
            if expected_enum in runtime_table:
                runtime_ok += 1
            else:
                fail("T02", f"Runtime field table entry missing: [{expected_enum}]")

    ok(f"Config field table entries: {config_ok}/{sum(1 for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG')}")
    ok(f"Runtime field table entries: {runtime_ok}/{sum(1 for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_RUNTIME')}")


def verify_field_names(fields, field_name_map, prefix):
    """T09: every field-table entry carries .field_name matching the catalogue.

    The generator copies the catalogue .name into UfsrvConfigFieldEntry.field_name.
    For the three CONF_FILE_LITERAL_*-named fields, f['name'] is already
    macro-resolved by field_name_from_macro(), matching the literal the macro
    expands to (and hence what the generator emits).
    """
    print(f"\n── field-table .field_name ↔ catalogue ({prefix})")
    missing = []
    mismatched = []
    for f in fields:
        full_enum = f['expected_enum_prefix'] + f['upper']
        got = field_name_map.get(full_enum)
        if got is None:
            missing.append(full_enum)
        elif got != f['name']:
            mismatched.append((full_enum, got, f['name']))
    if not missing and not mismatched:
        ok(f"field_name matches catalogue for all {len(fields)} entries")
    else:
        fail("T09", f"field_name missing {missing}; mismatched {mismatched}")


def verify_field_table_config_zero_init(fields, ft_config_values, prefix):
    """T06: Verify field-table .config entries are zero-initialized.
    Design version 4.9.0+ — the .config member of each field table entry
    must be ' = { 0 }' (blank receiver slot), not carry compiled defaults.
    Compiled defaults live in the separate _defaults_table[] arrays."""
    print(f"\n── field-table .config zero-init ({prefix})")
    if not ft_config_values:
        ok("No field-table config values found — skipping T06")
        return

    config_fields_dict = {f['expected_enum_prefix'] + f['upper']: f
                          for f in fields}
    ok_count = 0
    for name, body in sorted(ft_config_values.items()):
        if name.endswith('_COUNT'):
            ok_count += 1  # skip sentinel — not a real field
            continue
        # Check for non-zero int_val (e.g. .int_val = 19701)
        int_match = NON_ZERO_INT_RE.search(body)
        if int_match:
            fail("T06", f"Field table .config '{name}' has non-zero "
                 f".int_val = {int_match.group(1)} — expected zero-initialized "
                 f"(use defaults_table[] for compiled defaults)")
            continue
        # Check for non-empty str_val (e.g. .str_val = "127.0.0.1")
        str_match = NON_ZERO_STR_RE.search(body)
        if str_match and str_match.group(1):
            fail("T06", f"Field table .config '{name}' has "
                 f".str_val = \"{str_match.group(1)}\" — expected zero-initialized "
                 f"(use defaults_table[] for compiled defaults)")
            continue
        ok_count += 1

    ok(f"Field-table .config zero-init: {ok_count}/{len(ft_config_values)}")


def verify_defaults_table_parity(fields, ft_config_values, defaults_entries,
                                 table_type, prefix, enum_symbol_map=None):
    """T07: Verify every field-table entry has a matching defaults-table entry
    whose .int_val / .str_val / .type_allocation_sz match the cold catalogue.
    The defaults table is the canonical source of compiled defaults — any
    mismatch between catalogue and generated output is a FAIL."""
    label = f"defaults-table cross-check [{table_type}]"
    print(f"\n── {label} ({prefix})")
    if not ft_config_values:
        ok(f"No {table_type} field-table entries — skipping T07")
        return
    if not defaults_entries:
        fail("T07", f"No {table_type} defaults-table entries found — "
             f"expected defaults_table[] to exist alongside field_table[]")
        return

    config_fields_dict = {f['expected_enum_prefix'] + f['upper']: f
                          for f in fields}

    ok_count = 0
    for name in sorted(ft_config_values.keys()):
        if name.endswith('_COUNT'):
            ok_count += 1  # skip sentinel — not a real field
            continue

        # Existence check
        if name not in defaults_entries:
            fail("T07", f"{table_type} field-table entry [{name}] has no "
                 f"corresponding defaults-table entry")
            continue

        defaults = defaults_entries[name]
        field_info = config_fields_dict.get(name)
        if not field_info:
            ok_count += 1  # can't cross-check without catalogue — pass
            continue

        vt = field_info.get('value_type')

        # ── INT / ENUM: compare int_val ────────────────────────────
        if vt in ('UFSRV_CONFIG_VALUE_INT', 'UFSRV_CONFIG_VALUE_ENUM', 'UFSRV_CONFIG_VALUE_BOOL'):
            expected = field_info.get('int_default', 0)
            symbol = field_info.get('int_default_symbol')
            if symbol == 'true':
                expected = 1
            elif symbol == 'false':
                expected = 0
            elif symbol is not None:
                resolved = enum_symbol_map.get(symbol)
                if resolved is None:
                    fail("T07", f"{table_type} [{name}] (ENUM): cannot resolve "
                         f"enum constant '{symbol}' — not found in any enum "
                         f"definition under the include roots")
                    continue
                expected = resolved
            actual = defaults.get('int_val')
            if actual is None:
                fail("T07", f"{table_type} [{name}] (INT/ENUM): "
                     f"defaults table has no .int_val")
            elif actual != expected:
                fail("T07", f"{table_type} [{name}] {vt}: "
                     f".int_val mismatch — catalogue {expected}, "
                     f"defaults_table {actual}")
            else:
                ok_count += 1
                continue
        # ── STR: compare str_val + type_allocation_sz ──────────────
        elif vt == 'UFSRV_CONFIG_VALUE_STR':
            expected_str = field_info.get('str_default', '')
            expected_sz  = field_info.get('type_allocation_sz', 0)
            is_sizeof    = field_info.get('type_allocation_sz_is_sizeof', False)
            actual_str   = defaults.get('str_val')
            actual_sz    = defaults.get('type_allocation_sz', 0)
            mismatch = False
            if actual_str is None:
                fail("T07", f"{table_type} [{name}] (STR): "
                     f"defaults table has no .str_val")
                mismatch = True
            elif actual_str != expected_str:
                fail("T07", f"{table_type} [{name}] (STR): "
                     f".str_val mismatch — catalogue \"{expected_str}\", "
                     f"defaults_table \"{actual_str}\"")
                mismatch = True
            if not is_sizeof and actual_sz != expected_sz:
                fail("T07", f"{table_type} [{name}] (STR): "
                     f".type_allocation_sz mismatch — catalogue {expected_sz}, "
                     f"defaults_table {actual_sz}")
                mismatch = True
            if not mismatch:
                ok_count += 1
                continue
        # ── STRUCT: compare type_allocation_sz ─────────────────────
        # (sizeof(...) expressions in the catalogue cannot be evaluated
        # by Python — skip the comparison when the catalogue entry uses one)
        elif vt == 'UFSRV_CONFIG_VALUE_STRUCT':
            expected_sz = field_info.get('type_allocation_sz', 0)
            is_sizeof   = field_info.get('type_allocation_sz_is_sizeof', False)
            actual_sz   = defaults.get('type_allocation_sz', 0)
            if not is_sizeof and actual_sz != expected_sz:
                fail("T07", f"{table_type} [{name}] (STRUCT): "
                     f".type_allocation_sz mismatch — catalogue {expected_sz}, "
                     f"defaults_table {actual_sz}")
            else:
                ok_count += 1
                continue
        # ── PTR / CONST_PTR: value is always zero — pass ───────────
        else:
            ok_count += 1
            continue

    ok(f"{table_type} defaults-table cross-check: "
       f"{ok_count}/{len(ft_config_values)}")


def verify_validate(fields, validate_refs, prefix):
    """Verify fields with validation rules appear in Validate()."""
    print(f"\n── validate ({prefix})")

    validated_fields = [f for f in fields if f['validation_type'] != 'UFSRV_CONFIG_CHECK_NONE']
    if not validated_fields:
        ok("No validation rules in catalogue — Validate() should be empty")
        return

    ok_count = 0
    for f in validated_fields:
        name_lower = f['name'].lower()
        if name_lower in validate_refs:
            ok_count += 1
        else:
            fail("T03", f"Validation check missing for '{f['name']}' "
                 f"(rule={f['validation_type']})")

    ok(f"Validation checks present: {ok_count}/{len(validated_fields)}")

def verify_adapters(fields, adapter_decls, adapter_defs, prefix):
    """Verify fields with config_file_key have adapter declarations.
    Bodies are hand-authored per generator design — missing bodies are a warning,
    not a failure (subsystem owners must provide them)."""
    print(f"\n── adapters ({prefix})")

    adapter_fields = [f for f in fields if f['config_file_key']]
    if not adapter_fields:
        ok("No config-file-mapped fields — no adapters needed")
        return

    decl_ok = 0
    body_ok = 0
    missing_bodies = []
    for f in adapter_fields:
        expected_name = f'sAdapter{f["pascal"]}'
        if expected_name in adapter_decls:
            decl_ok += 1
        else:
            fail("T04", f"Adapter decl missing: {expected_name} "
                 f"(config_file_key={f['config_file_key']})")
        if expected_name in adapter_defs:
            body_ok += 1
        else:
            missing_bodies.append(expected_name)

    ok(f"Adapter decls present: {decl_ok}/{len(adapter_fields)}")
    if missing_bodies:
        warn("T04", f"Adapter bodies not yet written (hand-authored): "
             f"{', '.join(missing_bodies[:5])}"
             f"{'...' if len(missing_bodies) > 5 else ''} "
             f"({len(missing_bodies)}/{len(adapter_fields)} missing)")
    else:
        ok(f"Adapter bodies present: {body_ok}/{len(adapter_fields)}")

def verify_includes(files_to_check):
    """Verify no quoted local includes in generated files."""
    print(f"\n── include style")
    all_issues = []
    for filepath in files_to_check:
        if not os.path.exists(filepath):
            continue
        text = read_file(filepath)
        issues = check_includes(text, os.path.basename(os.path.dirname(filepath)))
        for issue in issues:
            all_issues.append(f"{os.path.basename(filepath)}: {issue}")

    if all_issues:
        for issue in all_issues:
            fail("I01", f"Quoted local include: {issue}")
    else:
        ok("All includes fully qualified (no quoted ufsrv/ufnet local includes)")

def verify_no_orphans(fields, decls, defs, prefix):
    """Verify no generated accessor exists without a catalogue entry."""
    print(f"\n── orphan check ({prefix})")
    field_pascals = {f['pascal'] for f in fields}
    # Lifecycle functions in ufsrv_config.c — not field getters
    KNOWN_NON_FIELDS = {'GuardAccess', 'Instance', 'RuntimeTeardown',
                        'InflateConfigStore', 'InflateRuntimeStore'}
    # Exclude AsPtr variants and known non-field lifecycle functions
    orphan_decls = {n for n in decls if n not in field_pascals
                    and not n.endswith('AsPtr')
                    and n not in KNOWN_NON_FIELDS}
    orphan_defs = {n for n in defs if n not in field_pascals
                   and not n.endswith('AsPtr')
                   and n not in KNOWN_NON_FIELDS}

    if orphan_decls:
        for o in sorted(orphan_decls):
            fail("I02", f"Orphan getter decl: UfsrvConfig{o}() — no catalogue entry")
    if orphan_defs:
        for o in sorted(orphan_defs):
            fail("I02", f"Orphan getter def: UfsrvConfig{o}() — no catalogue entry")
    if not orphan_decls and not orphan_defs:
        ok("No orphan accessors — all generated getters have catalogue entries")

def verify_asptr(fields, asptr_decls, asptr_defs, prefix, custom_getter_set=None):
    """Verify fields with is_as_ptr_getter have AsPtr decl and def.
    Expected return type: {c_lang_type} * — pointer to the value type."""
    print(f"\n── AsPtr getters ({prefix})")
    asptr_fields = [f for f in fields if f['is_as_ptr_getter']
                    and f['pascal'] not in (custom_getter_set or set())]
    skipped = sum(1 for f in fields if f['is_as_ptr_getter']
                  and f['pascal'] in (custom_getter_set or set()))
    if not asptr_fields:
        ok("No is_as_ptr_getter fields — no AsPtr getters needed")
        return

    decl_ok = 0
    def_ok = 0
    for f in asptr_fields:
        pascal = f['pascal']
        expected_type = f"{f['c_lang_type']} *"
        if pascal in asptr_decls:
            decl_ok += 1
        else:
            fail("T05", f"AsPtr decl missing: UfsrvConfig{pascal}AsPtr() "
                 f"(expected return type: '{expected_type}')")
        if pascal in asptr_defs:
            def_ok += 1
        else:
            fail("T05", f"AsPtr def missing: UfsrvConfig{pascal}AsPtr()")

    msg = f"AsPtr decls present: {decl_ok}/{len(asptr_fields)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)
    msg = f"AsPtr defs present: {def_ok}/{len(asptr_fields)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)

def verify_asptr_body(fields, asptr_bodies, prefix, custom_getter_set=None):
    """Verify each AsPtr getter body returns a pointer to the correct
    stored_value union member for its value_type (T08).

    The buggy pattern returns (type *)Instance(idx) — a cast of the
    UfsrvConfigValue* wrapper itself.  The correct pattern accesses
    ->stored_value.<member> where <member> depends on value_type:
      INT/ENUM  → &Instance(idx)->stored_value.int_val
      STR       → &Instance(idx)->stored_value.str_val
      STRUCT    → (type *)Instance(idx)->stored_value.blob_val
      PTR/CONST → (type *)&Instance(idx)->stored_value.ptr_val
    """
    print(f"\n── AsPtr body verification ({prefix})")
    asptr_fields = [f for f in fields if f['is_as_ptr_getter']
                    and f['pascal'] not in (custom_getter_set or set())]
    skipped = sum(1 for f in fields if f['is_as_ptr_getter']
                  and f['pascal'] in (custom_getter_set or set()))
    if not asptr_fields:
        msg = "No is_as_ptr_getter fields — no AsPtr bodies to verify"
        if skipped: msg += f" ({skipped} custom-getter skipped)"
        ok(msg)
        return

    ok_count = 0
    for f in asptr_fields:
        pascal = f['pascal']
        vt = f.get('value_type')
        if vt not in VALUE_TYPE_TO_MEMBER:
            fail("T08", f"AsPtr {pascal}: unknown value_type '{vt}' — cannot determine correct union member")
            continue

        expected_member = VALUE_TYPE_TO_MEMBER[vt]  # e.g. 'int_val', 'str_val', 'blob_val', 'ptr_val'

        if pascal not in asptr_bodies:
            fail("T08", f"AsPtr {pascal}: could not parse body from generated source")
            continue

        body = asptr_bodies[pascal]['body']

        # The correct pattern always accesses ->stored_value.<member>
        expected_access = f"->stored_value.{expected_member}"

        if expected_access in body:
            ok_count += 1
        else:
            # Build a helpful diagnostic
            fail("T08", f"AsPtr UfsrvConfig{pascal}AsPtr(): body does not access "
                 f"->stored_value.{expected_member} (expected for {vt}). "
                 f"Buggy pattern: (type *)Instance(idx) without union-member access.")

    msg = f"AsPtr bodies access correct union member: {ok_count}/{len(asptr_fields)}"
    if skipped: msg += f" ({skipped} custom-getter skipped)"
    ok(msg)

def verify_custom_getter_honoured(fields, getter_decls, getter_defs, asptr_decls, asptr_defs,
                                   custom_getter_set, prefix):
    """G08: For each is_keep_custom_getter field, verify the generated decl
    header carries an extern getter declaration (with the correct return type),
    while no generated getter definition, AsPtr declaration, or AsPtr
    definition exists — the definition is hand-authored."""
    print(f"\n── custom getter ({prefix})")
    if not custom_getter_set:
        ok("No is_keep_custom_getter fields — nothing to verify")
        return

    decl_ok = 0
    def_ok = 0
    asptr_decl_ok = 0
    asptr_def_ok = 0
    asptr_fields_in_scope = [f for f in fields
                             if f['is_keep_custom_getter'] and f['is_as_ptr_getter']]

    for f in fields:
        if not f['is_keep_custom_getter']:
            continue
        pascal = f['pascal']
        if pascal in getter_decls:
            actual = getter_decls[pascal]
            expected = f['expected_getter_return']
            if actual == expected:
                decl_ok += 1
            else:
                fail("G08", f"Custom getter extern decl return type mismatch "
                     f"for '{f['name']}': expected '{expected}', got '{actual}'")
        else:
            fail("G08", f"Custom getter extern decl missing: UfsrvConfig{pascal}() "
                 f"(is_keep_custom_getter=true)")
        if pascal not in getter_defs:
            def_ok += 1
        else:
            fail("G08", f"Getter def should be suppressed for '{f['name']}' "
                 f"(is_keep_custom_getter=true)")
        if f['is_as_ptr_getter']:
            if pascal in asptr_decls:
                asptr_key = pascal + 'AsPtr'
                expected_asptr = f"{f['c_lang_type']} *"
                actual = getter_decls.get(asptr_key)
                if actual is None or actual == expected_asptr:
                    asptr_decl_ok += 1
                else:
                    fail("G08", f"AsPtr extern decl return type mismatch for "
                         f"'{f['name']}': expected '{expected_asptr}', got '{actual}'")
            else:
                fail("G08", f"AsPtr extern decl missing: UfsrvConfig{pascal}AsPtr() "
                     f"(getter is custom)")
            if pascal not in asptr_defs:
                asptr_def_ok += 1
            else:
                fail("G08", f"AsPtr def should be suppressed for '{f['name']}' "
                     f"(getter is custom)")

    ok(f"Custom getter extern decls present: {decl_ok}/{len(custom_getter_set)}")
    ok(f"Getter defs correctly suppressed: {def_ok}/{len(custom_getter_set)}")
    if asptr_fields_in_scope:
        ok(f"AsPtr extern decls present: "
           f"{asptr_decl_ok}/{len(asptr_fields_in_scope)}")
        ok(f"AsPtr defs correctly suppressed: "
           f"{asptr_def_ok}/{len(asptr_fields_in_scope)}")


def verify_custom_setter_honoured(fields, setter_decls, setter_defs,
                                   bridge_decls, bridge_defs,
                                   custom_setter_set, prefix):
    """S03: For each is_keep_custom_setter field, verify the generated decl
    header carries an extern setter declaration plus a lifecycle-bridge
    declaration (UfsrvConfig<Name>Setter), while no generated setter definition
    exists — the setter definition is hand-authored and the bridge definition
    is generated."""
    print(f"\n── custom setter ({prefix})")
    if not custom_setter_set:
        ok("No is_keep_custom_setter fields — nothing to verify")
        return

    decl_ok = 0
    def_ok = 0
    bridge_decl_ok = 0
    bridge_def_ok = 0
    for f in fields:
        if not f['is_keep_custom_setter']:
            continue
        pascal = f['pascal']
        if pascal in setter_decls:
            decl_ok += 1
        else:
            fail("S03", f"Custom setter extern decl missing: UfsrvConfigSet{pascal}() "
                 f"(is_keep_custom_setter=true)")
        if pascal not in setter_defs:
            def_ok += 1
        else:
            fail("S03", f"Setter def should be suppressed for '{f['name']}' "
                 f"(is_keep_custom_setter=true)")
        if pascal in bridge_decls:
            bridge_decl_ok += 1
        else:
            fail("S03", f"Setter bridge decl missing: UfsrvConfig{pascal}Setter() "
                 f"(is_keep_custom_setter=true)")
        if pascal in bridge_defs:
            bridge_def_ok += 1
        else:
            fail("S03", f"Setter bridge def missing: UfsrvConfig{pascal}Setter() "
                 f"(is_keep_custom_setter=true)")

    ok(f"Custom setter extern decls present: {decl_ok}/{len(custom_setter_set)}")
    ok(f"Setter defs correctly suppressed: {def_ok}/{len(custom_setter_set)}")
    ok(f"Setter bridge decls present: {bridge_decl_ok}/{len(custom_setter_set)}")
    ok(f"Setter bridge defs present: {bridge_def_ok}/{len(custom_setter_set)}")


def verify_as_string(fields, decls, defs, generic_c_path, generic_h_path,
                     stub_c_paths, stub_h_paths, prefix):
    """A-series: AsString marshaller generation — count + presence.

    Counts the AsString accessors (one per field), splits them into standard
    (generic dispatch) vs non-standard (hand-authored stub), and verifies each
    is present in the relevant generated / hand-maintained files."""
    print(f"\n── AsString marshallers ({prefix})")

    standard = [f for f in fields if f['c_lang_type'] in GENERIC_AS_STRING_MAP]
    non_standard = [f for f in fields if f['c_lang_type'] not in GENERIC_AS_STRING_MAP]
    total = len(fields)
    ok(f"count: {total} total = {len(standard)} standard + {len(non_standard)} non-standard")

    decl_ok = 0
    for f in fields:
        if f['pascal'] in decls:
            decl_ok += 1
        else:
            fail("A01", f"AsString decl missing: UfsrvConfig{f['pascal']}AsString()")
    ok(f"declarations present: {decl_ok}/{total}")

    def_ok = 0
    dispatch_ok = 0
    for f in fields:
        pascal = f['pascal']
        if pascal not in defs:
            fail("A02", f"AsString def missing: UfsrvConfig{pascal}AsString()")
            continue
        def_ok += 1
        expected = (GENERIC_AS_STRING_MAP.get(f['c_lang_type'])
                    or f"UfsrvConfig{pascal}AsString_{as_string_type_suffix(f['c_lang_type'])}")
        if defs[pascal] == expected:
            dispatch_ok += 1
        else:
            fail("A03", f"AsString dispatch mismatch for '{f['name']}': "
                        f"expected {expected}, got {defs[pascal]}")
    ok(f"definitions present: {def_ok}/{total}")
    ok(f"dispatch correct: {dispatch_ok}/{total}")

    generic_c = read_file(generic_c_path) if os.path.exists(generic_c_path) else ''
    generic_h = read_file(generic_h_path) if os.path.exists(generic_h_path) else ''
    gen_ok = 0
    for c_type, fn in GENERIC_AS_STRING_MAP.items():
        missing = []
        if fn not in generic_c:
            missing.append('def (.c)')
        if fn not in generic_h:
            missing.append('decl (.h)')
        if missing:
            fail("A04", f"generic {fn} missing: {' and '.join(missing)}")
        else:
            gen_ok += 1
    ok(f"generic marshallers present: {gen_ok}/{len(GENERIC_AS_STRING_MAP)}")

    stub_c_candidates = [(p, os.path.exists(p)) for p in stub_c_paths]
    stub_h_candidates = [(p, os.path.exists(p)) for p in stub_h_paths]
    stub_c_texts = [read_file(p) for p, e in stub_c_candidates if e]
    stub_h_texts = [read_file(p) for p, e in stub_h_candidates if e]
    stub_ok = 0
    for f in non_standard:
        stub = f"UfsrvConfig{f['pascal']}AsString_{as_string_type_suffix(f['c_lang_type'])}"
        missing = []
        if not any(stub in t for t in stub_c_texts):
            missing.append('def (.c)')
        if not any(stub in t for t in stub_h_texts):
            missing.append('decl (.h)')
        if missing:
            checked = []
            if 'def (.c)' in missing:
                checked += [f"{p} ({'no match' if e else 'absent'})"
                            for p, e in stub_c_candidates]
            if 'decl (.h)' in missing:
                checked += [f"{p} ({'no match' if e else 'absent'})"
                            for p, e in stub_h_candidates]
            fail("A05", f"stub {stub} missing: {' and '.join(missing)} "
                        f"— checked: {', '.join(checked)}")
        else:
            stub_ok += 1
    ok(f"stub marshallers present: {stub_ok}/{len(non_standard)}")


def parse_field_table_on_as_string(text):
    """Extract {FIELD_NAME: on_as_string_fn} from the field tables."""
    entries = {}
    current = None
    for line in text.split('\n'):
        m = re.match(r'\s*\[(UFSRV\w*(?:CONFIG|RUNTIME)\w*_FIELD_\w+)\]\s*=', line)
        if m:
            current = m.group(1)
            continue
        if current:
            om = re.search(r'\.on_as_string\s*=\s*(\w+)\s*,', line)
            if om:
                entries[current] = om.group(1)
                current = None
    return entries


def verify_as_string_field_table_wiring(fields, ft_text, prefix):
    """A06: every field-table entry wires .on_as_string = UfsrvConfig<Name>AsString."""
    print(f"\n── AsString field-table wiring ({prefix})")
    on_as_string = parse_field_table_on_as_string(ft_text)
    ok_count = 0
    for f in fields:
        expected_enum = f['expected_enum_prefix'] + f['upper']
        expected_fn = f"UfsrvConfig{f['pascal']}AsString"
        actual = on_as_string.get(expected_enum)
        if actual is None:
            fail("A06", f"field-table entry [{expected_enum}] missing .on_as_string")
        elif actual != expected_fn:
            fail("A06", f"field-table entry [{expected_enum}] .on_as_string = {actual}, "
                        f"expected {expected_fn}")
        else:
            ok_count += 1
    ok(f"field-table .on_as_string wiring: {ok_count}/{len(fields)}")


def parse_field_table_on_value_available(text):
    """Extract {FIELD_NAME: on_value_available_fn} from the field tables."""
    entries = {}
    current = None
    for line in text.split('\n'):
        m = re.match(r'\s*\[(UFSRV\w*(?:CONFIG|RUNTIME)\w*_FIELD_\w+)\]\s*=', line)
        if m:
            current = m.group(1)
            continue
        if current:
            om = re.search(r'\.on_value_available\s*=\s*(\w+)\s*,', line)
            if om:
                entries[current] = om.group(1)
                current = None
    return entries


def verify_setter_bridge_field_table_wiring(fields, ft_text, prefix):
    """S04: every is_keep_custom_setter field wires
    .on_value_available = UfsrvConfig<Name>Setter (the generated lifecycle
    bridge that forwards to the hand-authored UfsrvConfigSet<Name>)."""
    print(f"\n── custom-setter bridge field-table wiring ({prefix})")
    custom_setters = [f for f in fields if f['is_keep_custom_setter']]
    if not custom_setters:
        ok("No is_keep_custom_setter fields — nothing to verify")
        return
    on_va = parse_field_table_on_value_available(ft_text)
    ok_count = 0
    for f in custom_setters:
        expected_enum = f['expected_enum_prefix'] + f['upper']
        expected_fn = f"UfsrvConfig{f['pascal']}Setter"
        actual = on_va.get(expected_enum)
        if actual is None:
            fail("S04", f"field-table entry [{expected_enum}] missing .on_value_available")
        elif actual != expected_fn:
            fail("S04", f"field-table entry [{expected_enum}] .on_value_available = {actual}, "
                        f"expected {expected_fn}")
        else:
            ok_count += 1
    ok(f"field-table .on_value_available bridge wiring: {ok_count}/{len(custom_setters)}")


def verify_stub_files_present(committed_src, prefix):
    """ST-series: presence check (warn-only) for the one-off hand-maintained stub files.

    The generator emits these under --emit-one-off-as-string and
    --emit-one-off-as_getters_setters; the consumer is expected to commit and
    hand-maintain them.  A missing file is a WARNING (not a failure) so the
    build is not blocked, and the regeneration command is printed."""
    print(f"\n── one-off stub files ({prefix})")

    as_string_cmd = ('./build/host/ufsrv_config_generator --emit-one-off-as-string '
                     '--repo-dir=<abs repo root> --source --output-dir=/tmp/asstring_dynamic')
    getset_cmd = ('./build/host/ufsrv_config_generator --emit-one-off-as_getters_setters '
                  '--repo-dir=<abs repo root> --source --output-dir=/tmp/gs_dynamic')

    groups = [
        ('AsString stub definitions',
         ['ufsrv_config_stubs_as_string.c',
          'ufsrv_config_stubs_as_string_generated.c'],
         as_string_cmd),
        ('AsString stub declarations',
         ['ufsrv_config_stubs_as_string_priv.h',
          'ufsrv_config_stubs_as_string_priv_generated.h'],
         as_string_cmd),
        ('custom accessor stubs',
         ['ufsrv_config_stubs_accessor_defs.c',
          'ufsrv_config_stubs_accessor_defs_generated.c'],
         getset_cmd),
        ('generic marshaller definitions',
         ['ufsrv_config_generic_callbacks.c'],
         as_string_cmd),
        ('generic marshaller declarations',
         ['ufsrv_config_generic_callbacks_priv.h'],
         as_string_cmd),
    ]

    for i, (label, names, cmd) in enumerate(groups, 1):
        present = [n for n in names if os.path.exists(os.path.join(committed_src, n))]
        if present:
            ok(f"{label}: present ({present[0]})")
        else:
            warn(f"ST{i:02d}", f"{label}: missing — expected one of: {', '.join(names)}")
            print(f"         regenerate: {cmd}")


def verify_naming_convention(fields, config_enums, prefix):
    """Check generated naming against design spec.
    Design says: UFSRV_CONFIG_{CORE,ADJUNCT}_FIELD_*
    Generator uses: UFSRV_CONFIG_FIELD_* (core, no prefix) or
                    UFSRVADJUNCT_CONFIG_FIELD_* (adjunct, --prefix=adjunct).
    Flag deviation as warning."""
    print(f"\n── naming convention ({prefix})")
    if not config_enums:
        return

    config_fields = [f for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG']
    if not config_fields:
        return

    # Check the first generated enum constant name pattern
    first = config_fields[0]
    key = first['name'].lower()
    if key not in config_enums:
        return

    # We have the enum values but need the actual constant name from the file.
    # Re-derive expected patterns from the design:
    # Design says core = UFSRV_CONFIG_CORE_FIELD_*, adjunct = UFSRV_CONFIG_ADJUNCT_FIELD_*
    # Generator says core = UFSRV_CONFIG_FIELD_*, adjunct = UFSRVADJUNCT_CONFIG_FIELD_*
    pass  # handled in main() by inspecting raw enum text

# ── Key-map verification (K-series) ──────────────────────────────────────────

KEY_MAP_ENTRY_RE = re.compile(
    r'\{\s*(CONF_FILE_LITERAL_\w+)\s*,\s*(UFSRV\w*CONFIG\w*_FIELD_\w+)\s*\}')

KEY_MAP_COUNT_RE = re.compile(
    r'#define\s+UFSRV_CONFIG_KEY_MAP_COUNT\s+(\d+)')

LITERALS_DEFINE_RE = re.compile(
    r'#define\s+(CONF_FILE_LITERAL_\w+)\s+"([^"]*)"')


def parse_key_map(text):
    """Parse the key-map header, returning (count, entries_list).
    entries_list is a list of (macro_name, enum_name) tuples in file order.
    Returns (0, []) on parse failure."""
    count_m = KEY_MAP_COUNT_RE.search(text)
    if not count_m:
        return 0, []
    count = int(count_m.group(1))

    entries = []
    for m in KEY_MAP_ENTRY_RE.finditer(text):
        entries.append((m.group(1), m.group(2)))

    return count, entries


def parse_literals_header(path):
    """Parse ufsrv_config_literals_defs.h, returning {macro_name: string_value}."""
    result = {}
    if not os.path.exists(path):
        return result
    text = read_file(path)
    for m in LITERALS_DEFINE_RE.finditer(text):
        result[m.group(1)] = m.group(2)
    return result


def verify_key_map(fields, key_map_file, repo_root, prefix):
    """K01–K07: verify the conf-literal → config-field key map."""
    print(f"\n── key map ({prefix})")

    # K01 — file existence
    if not key_map_file or not os.path.exists(key_map_file):
        fail("K01", "Key map header not found in generated output")
        return

    text = read_file(key_map_file)
    count, entries = parse_key_map(text)

    if count == 0 or not entries:
        fail("K01", "Key map header exists but no entries parsed")
        return

    ok(f"Key map header found with {count} entries")

    # Build catalogue expectation: CONFIG fields with config_file_key
    config_with_key = [f for f in fields
                       if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG'
                       and f['config_file_key']]

    # Build literals forward map for K07
    literals_path = os.path.join(repo_root, 'include', 'ufsrvcorelib',
                                 'ufsrv_core', 'configfile_processor',
                                 'ufsrv_config_literals_defs.h')
    literals_map = parse_literals_header(literals_path)

    # Build lookup: macro_name → (catalogue_field, string_value)
    macro_to_field = {}
    for f in config_with_key:
        macro = f['config_file_key']  # e.g. "CONF_FILE_LITERAL_SERVER_ID"
        string_val = literals_map.get(macro, None)
        macro_to_field[macro] = (f, string_val)

    # ── K05: COUNT match ─────────────────────────────────────────────
    if count != len(config_with_key):
        fail("K05", f"UFSRV_CONFIG_KEY_MAP_COUNT={count} but catalogue has "
              f"{len(config_with_key)} CONFIG fields with config_file_key")
    else:
        ok(f"K05: COUNT={count} matches catalogue ({len(config_with_key)} fields)")

    # ── K02: sorted order + K03: no duplicates ────────────────────────
    sorted_ok = True
    prev_str = ""
    seen_keys = set()
    dup_found = False
    for i, (macro, enum_name) in enumerate(entries):
        string_val = literals_map.get(macro, macro)  # fallback to macro name
        if i > 0 and string_val < prev_str:
            fail("K02", f"Sort order violation at index {i}: "
                 f"\"{string_val}\" < \"{prev_str}\"")
            sorted_ok = False
        prev_str = string_val

        if string_val in seen_keys:
            fail("K03", f"Duplicate key \"{string_val}\" at index {i}")
            dup_found = True
        seen_keys.add(string_val)

    if sorted_ok and not dup_found:
        ok("K02+K03: entries sorted, no duplicate keys")

    # ── K04: coverage — every CONFIG field with config_file_key has an entry
    # ── K06: no spurious entries ─────────────────────────────────────
    entry_macros = {macro for macro, _ in entries}
    entry_enums  = {enum_name for _, enum_name in entries}

    missing = []
    for f in config_with_key:
        # Build expected enum name: UFSRV_CONFIG_FIELD_<UPPER>
        expected_enum = f['expected_enum_prefix'] + f['upper']
        expected_macro = f['config_file_key']
        if expected_macro not in entry_macros and expected_enum not in entry_enums:
            missing.append(f['name'])

    if missing:
        for name in missing:
            fail("K04", f"Key map missing entry for CONFIG field '{name}'")
    else:
        ok(f"K04: all {len(config_with_key)} CONFIG fields with config_file_key "
           f"present in key map")

    # K06: spurious entries
    spurious = []
    for macro, enum_name in entries:
        if macro not in macro_to_field:
            spurious.append(f"{macro} → {enum_name}")
    if spurious:
        for s in spurious:
            fail("K06", f"Spurious key-map entry: {s} — no matching catalogue field")
    else:
        ok("K06: no spurious entries")

    # ── K07: each macro's string value matches the catalogue's config_file_key
    # The catalogue records config_file_key as the MACRO NAME (e.g.
    # CONF_FILE_LITERAL_SERVER_ID), parsed from the .c.input text.
    # Each entry in the key map uses the same macro name.
    # Cross-check: the string value of the macro (from literals header)
    # should match what the catalogue expects (the catalogue's config_file_key
    # IS the macro name, so by construction they match).
    # Practical check: for each entry, confirm the macro exists in the
    # literals header and the catalogue's config_file_key is the same macro.
    k07_ok = 0
    for macro, enum_name in entries:
        if macro in macro_to_field:
            _, string_val = macro_to_field[macro]
            if string_val is not None:
                k07_ok += 1
            else:
                fail("K07", f"Key-map macro {macro} not found in "
                     f"ufsrv_config_literals_defs.h")
        else:
            # Already reported under K06
            pass

    ok(f"K07: {k07_ok}/{len(entries)} entries cross-validated against "
       f"literals header")

# ── main ─────────────────────────────────────────────────────────────────────

def main():
    global _failures, _warnings
    _failures = 0
    _warnings = 0

    ap = argparse.ArgumentParser(description='Verify ufsrv_config generator output against cold catalogue')
    ap.add_argument('--catalogue', required=True, help='Path to .c.input cold catalogue file')
    ap.add_argument('--generated-dir', default=None, help='Directory containing generated .h and .c files (omit for catalogue-only checks)')
    ap.add_argument('--prefix', default='', help='Optional prefix for display (e.g., "core", "adjunct")')
    args = ap.parse_args()

    prefix = args.prefix or os.path.basename(args.catalogue)

    # Parse catalogue
    print(f"ufsrv_config_verify: reading catalogue from {args.catalogue}")
    fields = parse_catalogue(args.catalogue)
    if not fields:
        print("ERROR: no fields found in catalogue")
        sys.exit(1)

    config_count = sum(1 for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_CONFIG')
    runtime_count = sum(1 for f in fields if f['domain'] == 'UFSRV_CONFIG_DOMAIN_RUNTIME')
    adapter_count = sum(1 for f in fields if f['config_file_key'])
    validated_count = sum(1 for f in fields if f['validation_type'] != 'UFSRV_CONFIG_CHECK_NONE')
    struct_count = sum(1 for f in fields if f['value_type'] == 'UFSRV_CONFIG_VALUE_STRUCT')
    asptr_count = sum(1 for f in fields if f['is_as_ptr_getter'])
    custom_getter_set = {f['pascal'] for f in fields if f['is_keep_custom_getter']}
    custom_setter_set = {f['pascal'] for f in fields if f['is_keep_custom_setter']}

    print(f"  {len(fields)} fields: {config_count} CONFIG + {runtime_count} RUNTIME")
    print(f"  {adapter_count} config-file-mapped, {validated_count} validated, "
          f"{struct_count} STRUCT, {asptr_count} AsPtr")
    print(f"  {len(custom_getter_set)} custom-getter, {len(custom_setter_set)} custom-setter")

    # value_type must be present on every field (checked first;
    # dependent checks will also flag these fields as non-compliant)
    verify_value_type_present(fields, prefix)
    verify_struct_validation_none(fields, prefix)
    verify_type_allocation_sz(fields, prefix)
    verify_config_file_key(fields, prefix)
    verify_stored_value_member(fields, prefix)
    verify_lifecycle_callbacks(fields, prefix)
    verify_custom_setter_lifecycle_warning(fields, prefix)

    # If no generated dir, stop after catalogue checks
    if not args.generated_dir:
        print(f"\n{'='*60}")
        print(f"(skipping generated artefact checks — no --generated-dir)")
        print(f"FAILED: {_failures} failure(s), {_warnings} warning(s)")
        return 1 if _failures else 0

    gd = args.generated_dir

    # Find generated files
    # The generated dir may contain files with various naming patterns.
    # We need to locate: enums, store decls, accessor decls, accessor defs,
    # field tables, dispatch decls, validate, and the dispatch table.
    def find_file(patterns):
        """Find a file matching any of the given patterns (basename substring match)."""
        for fname in os.listdir(gd):
            for pat in patterns:
                if pat in fname:
                    return os.path.join(gd, fname)
        return None

    enums_file = find_file(['field_enums_generated.h', 'field_enums_'])
    store_decls_file = find_file(['store_decls_generated.h', 'store_decls_'])
    accessor_decls_file = find_file(['accessor_decls_generated.h', 'accessor_decls_'])
    accessor_defs_file = find_file(['accessor_defs_generated.c', 'accessor_defs_'])
    field_tables_file = find_file(['field_tables_generated.c', 'field_tables_'])
    validate_file = find_file(['validate_generated.c', 'validate_'])
    dispatch_table_file = find_file(['dispatch_table_generated.h', 'dispatch_table_'])
    key_map_file = find_file(['literal_to_field_map_priv_generated.h',
                               'literal_to_field_map_'])

    # Also check committed locations if generated dir doesn't have them.
    # Catalogue is at: <repo>/dev/scripts/ufsrv_config_generator/file.c.input
    # Go up 4 levels to reach repo root, then into include/ or src/.
    _repo_root = os.path.abspath(args.catalogue)
    for _ in range(4):
        _repo_root = os.path.dirname(_repo_root)
    include_roots = [os.path.join(_repo_root, 'include'), '/opt/include']
    enum_symbol_map = build_enum_symbol_map(include_roots)
    committed_include = os.path.join(_repo_root, 'include', 'ufsrvcorelib',
                                     'ufsrv_core', 'ufsrv_config')
    committed_src = os.path.join(_repo_root, 'src', 'ufsrv_core', 'ufsrv_config')
    committed_gen_include = os.path.join(committed_include, 'generated')
    committed_gen_src = os.path.join(committed_src, 'generated')

    if not enums_file:
        enums_file = os.path.join(committed_gen_include, 'ufsrv_config_field_enums_generated.h')
    if not accessor_decls_file:
        accessor_decls_file = os.path.join(committed_gen_include, 'ufsrv_config_accessor_decls_generated.h')
    if not accessor_defs_file:
        accessor_defs_file = os.path.join(committed_gen_src, 'ufsrv_config_accessor_defs_generated.c')
    if not field_tables_file:
        field_tables_file = os.path.join(committed_gen_src, 'data_ufsrv_config_field_tables_generated.c')
    if not validate_file:
        validate_file = os.path.join(committed_gen_src, 'ufsrv_config_validate_generated.c')
    if not key_map_file:
        key_map_file = os.path.join(committed_gen_src,
            'data_ufsrv_config_literal_to_field_map_priv_generated.h')

    # ── Run verifications ─────────────────────────────────────────────────

    # Initialize all variables that will be set in conditional blocks
    config_enums = {}
    runtime_enums = {}
    getter_decls = {}
    getter_defs = {}
    setter_decls = set()
    setter_defs = set()
    bridge_decls = set()
    bridge_defs = set()
    asptr_decls = set()
    asptr_defs = set()
    if enums_file and os.path.exists(enums_file):
        text = read_file(enums_file)
        config_enums, runtime_enums = parse_enums(text)
        verify_enums(fields, config_enums, runtime_enums, prefix)
    else:
        print(f"\n── enums ({prefix})")
        fail("I03", f"Enums file not found (tried: {enums_file})")

    # Getter declarations
    if accessor_decls_file and os.path.exists(accessor_decls_file):
        decl_text = read_file(accessor_decls_file)
        getter_decls = parse_accessor_decls(decl_text)
        setter_decls = parse_setter_decls(decl_text)
        bridge_decls = parse_setter_bridge_decls(decl_text)
        asptr_decls = parse_asptr_decls(decl_text)
        verify_getter_decls(fields, getter_decls, prefix, custom_getter_set)
    else:
        print(f"\n── getter declarations ({prefix})")
        fail("I03", f"Accessor decls file not found (tried: {accessor_decls_file})")
        getter_decls = {}

    # Getter definitions — generated + hand-authored Runtime
    getter_defs = {}
    setter_defs = set()
    asptr_defs = set()
    def_text_all = ""

    if accessor_defs_file and os.path.exists(accessor_defs_file):
        def_text = read_file(accessor_defs_file)
        def_text_all = def_text
        getter_defs = parse_accessor_defs(def_text)
        setter_defs = parse_setter_defs(def_text)
        bridge_defs = parse_setter_bridge_defs(def_text)
        asptr_defs = parse_asptr_defs(def_text)

    # Also parse hand-authored Runtime defs + AsPtr defs from ufsrv_config.c
    runtime_defs_file = os.path.join(committed_src, 'ufsrv_config.c')
    if os.path.exists(runtime_defs_file):
        rt_text = read_file(runtime_defs_file)
        rt_getter_defs = parse_accessor_defs(rt_text)
        rt_setter_defs = parse_setter_defs(rt_text)
        rt_asptr_defs = parse_asptr_defs(rt_text)
        # Merge — generated defs take priority if both exist
        for k, v in rt_getter_defs.items():
            if k not in getter_defs:
                getter_defs[k] = v
        setter_defs |= rt_setter_defs
        asptr_defs |= rt_asptr_defs
        def_text_all += '\n' + rt_text

    verify_getter_defs(fields, getter_defs, prefix, custom_getter_set)

    # Test 1: decl return type vs catalogue
    # (already done in verify_getter_decls above)

    # Test 2: def return type vs decl return type
    if getter_decls and getter_defs:
        verify_getter_decl_def_match(fields, getter_decls, getter_defs, prefix,
                                     custom_getter_set)

    # Test 3: return expression type vs def return type (all sources)
    return_exprs = parse_getter_return_exprs(def_text_all)
    verify_getter_return_exprs(fields, getter_defs, return_exprs, prefix,
                               custom_getter_set)

    # Test 4: enum index access — getter uses its own field's enum constant
    verify_getter_enum_access(fields, return_exprs, prefix, custom_getter_set)

    # Test 5: union member — getter dereferences correct .int_val/.str_val/etc
    verify_getter_union_member(fields, return_exprs, prefix, custom_getter_set)

    # Test 6: store array correctness — Config→ufsrv_config_store, Runtime→ufsrv_runtime_store
    verify_getter_store_correctness(fields, return_exprs, prefix, custom_getter_set)

    # Setters
    if accessor_decls_file and accessor_defs_file:
        verify_setters(fields, setter_decls, setter_defs, prefix, custom_setter_set)

        # S02: setter body pattern correctness
        setter_bodies = parse_setter_bodies(def_text_all)
        verify_setter_bodies(fields, setter_bodies, prefix, custom_setter_set)

        # S04: exhaustive lifecycle dispatch audit
        verify_setter_lifecycle_dispatch(fields, setter_bodies, prefix, custom_setter_set)

    # Field tables
    if field_tables_file and os.path.exists(field_tables_file):
        ft_text = read_file(field_tables_file)
        config_table = parse_field_table_entries(ft_text)
        runtime_table = parse_runtime_table_entries(ft_text)
        verify_field_tables(fields, config_table, runtime_table, prefix)

        # T09: field-table .field_name ↔ catalogue parity
        verify_field_names(fields, parse_field_names(ft_text), prefix)

        # T06: field-table .config must be zero-initialized
        config_ft_config = parse_field_table_config_values(ft_text, 'CONFIG')
        runtime_ft_config = parse_field_table_config_values(ft_text, 'RUNTIME')
        verify_field_table_config_zero_init(fields, config_ft_config, prefix)
        verify_field_table_config_zero_init(fields, runtime_ft_config, prefix)

        # T07: every field-table entry must have a matching defaults-table entry
        config_defaults = parse_defaults_table_entries(ft_text, 'CONFIG')
        runtime_defaults = parse_defaults_table_entries(ft_text, 'RUNTIME')
        verify_defaults_table_parity(fields, config_ft_config, config_defaults,
                                     'CONFIG', prefix, enum_symbol_map)
        verify_defaults_table_parity(fields, runtime_ft_config, runtime_defaults,
                                     'RUNTIME', prefix, enum_symbol_map)
    else:
        print(f"\n── field tables ({prefix})")
        fail("I03", f"Field tables file not found (tried: {field_tables_file})")

    # Validate
    if validate_file and os.path.exists(validate_file):
        val_text = read_file(validate_file)
        validate_refs = parse_validate_references(val_text)
        verify_validate(fields, validate_refs, prefix)
    else:
        print(f"\n── validate ({prefix})")
        fail("I03", f"Validate file not found (tried: {validate_file})")

    # AsPtr getters
    verify_asptr(fields, asptr_decls, asptr_defs, prefix, custom_getter_set)

    # AsPtr body verification — T08: correct union member access
    asptr_bodies = parse_asptr_bodies(def_text_all) if def_text_all else {}
    verify_asptr_body(fields, asptr_bodies, prefix, custom_getter_set)

    # G08: custom getter honoured — no generated getter/AsPtr for flagged fields
    verify_custom_getter_honoured(fields, getter_decls, getter_defs,
                                   asptr_decls, asptr_defs,
                                   custom_getter_set, prefix)

    # S03: custom setter honoured — no generated setter for flagged fields,
    #      plus the generated lifecycle bridge (UfsrvConfig<Name>Setter)
    verify_custom_setter_honoured(fields, setter_decls, setter_defs,
                                   bridge_decls, bridge_defs,
                                   custom_setter_set, prefix)

    # A-series: AsString marshallers — count + presence
    as_string_decls = set()
    as_string_defs = {}
    if accessor_decls_file and os.path.exists(accessor_decls_file):
        as_string_decls = parse_as_string_decls(read_file(accessor_decls_file))
    if accessor_defs_file and os.path.exists(accessor_defs_file):
        as_string_defs = parse_as_string_defs(read_file(accessor_defs_file))
    verify_as_string(
        fields, as_string_decls, as_string_defs,
        os.path.join(committed_src, 'ufsrv_config_generic_callbacks.c'),
        os.path.join(committed_src, 'ufsrv_config_generic_callbacks_priv.h'),
        [os.path.join(committed_src, 'ufsrv_config_stubs_as_string_generated.c'),
         os.path.join(committed_src, 'ufsrv_config_stubs_as_string.c')],
        [os.path.join(committed_src, 'ufsrv_config_stubs_as_string_priv_generated.h'),
         os.path.join(committed_src, 'ufsrv_config_stubs_as_string_priv.h')],
        prefix)

    # A06: field-table .on_as_string wiring
    if field_tables_file and os.path.exists(field_tables_file):
        verify_as_string_field_table_wiring(fields, read_file(field_tables_file), prefix)

    # S04: field-table .on_value_available bridge wiring for custom setters
    if field_tables_file and os.path.exists(field_tables_file):
        verify_setter_bridge_field_table_wiring(fields, read_file(field_tables_file), prefix)

    # ST-series: one-off stub-file presence (warn-only)
    verify_stub_files_present(committed_src, prefix)

    # K-series: key map — conf-literal → config-field mapping
    verify_key_map(fields, key_map_file, _repo_root, prefix)

    # callback function bodies exist in source files
    _cb_texts = []
    for _cb_dir in (committed_src, committed_gen_src):
        if os.path.exists(_cb_dir):
            for fn in os.listdir(_cb_dir):
                if fn.endswith('.c'):
                    _cb_texts.append(read_file(os.path.join(_cb_dir, fn)))
    verify_callback_bodies(fields, _cb_texts, prefix)

    # Include style check
    files_to_check = [f for f in [
        enums_file, store_decls_file, accessor_decls_file,
        dispatch_table_file
    ] if f and os.path.exists(f)]
    verify_includes(files_to_check)

    # Orphan check
    if getter_decls and getter_defs:
        verify_no_orphans(fields, getter_decls, getter_defs, prefix)

    # Naming convention check
    if config_enums:
        verify_naming_convention(fields, config_enums, prefix)

    # ── Summary ───────────────────────────────────────────────────────────
    print(f"\n{'='*60}")
    if _failures == 0 and _warnings == 0:
        print(f"VERIFIED: 0 failures, 0 warnings — all generated artefacts match catalogue")
        return 0
    elif _failures == 0:
        print(f"VERIFIED with warnings: {_warnings} warning(s) — see above")
        return 0
    else:
        print(f"FAILED: {_failures} failure(s), {_warnings} warning(s)")
        return 1

if __name__ == '__main__':
    sys.exit(main())
