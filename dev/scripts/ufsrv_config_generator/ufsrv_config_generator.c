/**
 * @file ufsrv_config_generator.c
 * @brief Build‑time code generator — reads cold catalogues, emits
 *        ufsrv_config_generated.h (declarations) and
 *        ufsrv_config_getters_setters_generated.c (definitions).
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This is a HOST TOOL — it runs on the build machine, not the target.
 *
 * Usage:
 *   cc -I include/ufsrvcorelib \
 *      -I include/ufsrvcorelib/ufsrv_core/ufsrv_config \
 *      dev/scripts/ufsrv_config_generator/ufsrv_config_generator.c \
 *      -o build/host/ufsrv_config_generator
 *   ./build/host/ufsrv_config_generator --header > ufsrv_config_generated.h
 *   ./build/host/ufsrv_config_generator --source > ufsrv_config_getters_setters_generated.c
 *
 * Validation — exits non‑zero on:
 *   - Duplicate field names
 *   - CONFIG field with non‑NULL teardown
 *   - RUNTIME field with validation rule
 *   - Unknown value_type / empty field name
 */
// @version 64
// @sha     d3a5e2999eee548a6b4085cdfceb7e4b3147be96

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>        /* time, ctime */
#include <sys/stat.h>   /* mkdir */
#include <sys/types.h>  /* mkdir */
#include <errno.h>      /* errno */

#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_field_descriptor.h>
#include <ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h>

// Cold catalogue data

#include "catalogue_input.h"

// Generator state

#define MAX_FIELDS        256
#define INDENT2           "    "
#define INDENT4           "        "

typedef struct {
    const UfsrvConfigFieldDescriptor *desc;
    int  config_index;
    int  runtime_index;
} GenField;

static GenField gen_fields[MAX_FIELDS];
static int      gen_field_count;
static int      config_field_count;
static int      runtime_field_count;
static int      g_source_mode;   /* 0 = header, 1 = source */
static FILE    *g_out = NULL;    /* active output stream — set to stdout or a file */
static const char *g_output_dir = NULL;   /* non‑NULL ⇒ decomposed mode */
static const char *g_include_prefix = NULL; /* e.g. "ufsrvcorelib" */
static const char *g_prefix = NULL;       /* e.g. "adjunct" — infixed into all identifiers */
static const char *g_repo_dir = NULL;       /* repo root for finding source assets */
static const char *g_literals_header = NULL; /* explicit path to ufsrv_config_literals_defs.h */
static int  g_one_off = 0;               /* --emit-one-off-as-string: regenerate hand-maintained AsString files */
static int  g_one_off_gs = 0;            /* --emit-one-off-as_getters_setters: regenerate skeletal accessor-def stubs */
static int  g_show_version = 0;          /* --version: print version provenance and exit */

/* ── Literal reverse-map (CONF_FILE_LITERAL_* → string value) ────────── */

typedef struct {
    char macro_name[128];
    char string_value[256];
} LiteralMapEntry;

static LiteralMapEntry *g_literal_map      = NULL;
static int              g_literal_map_count = 0;

/* ── Prefix helpers ──────────────────────────────────────────────────── */

/* "ConfigAdjunct" or "Config" — used in enum/function CamelCase names */
static const char *
sPrefixCap(void)
{
    static char buf[64];
    if (!g_prefix) return "";
    snprintf(buf, sizeof(buf), "%c%s",
             (char)(g_prefix[0] - 32), g_prefix + 1);  /* "adjunct" → "Adjunct" */
    return buf;
}

/* "_adjunct" or "" — used in file names and snake_case identifiers */
static const char *
sPrefixFile(void)
{
    static char buf[64];
    if (!g_prefix) return "";
    snprintf(buf, sizeof(buf), "_%s", g_prefix);
    return buf;
}

/* "ADJUNCT" or "" — used in uppercase enum constants. Callers supply
 * the trailing underscore if needed. */
static const char *
sPrefixUpper(void)
{
    static char buf[64];
    if (!g_prefix) return "";
    int i;
    for (i = 0; g_prefix[i]; i++)
        buf[i] = (char)(g_prefix[i] - 32);
    buf[i] = '\0';
    return buf;
}

/* Emit a PascalCase lifecycle function name with optional prefix infix.
 * e.g. "PopulateDefaults" → "UfsrvConfigPopulateDefaults" or
 *      "PopulateDefaults" → "UfsrvConfigAdjunctPopulateDefaults" */
static void
sEmitLifecycleName(const char *suffix)
{
    fprintf(g_out, "UfsrvConfig%s%s", sPrefixCap(), suffix);
}

/* Read the first line of a file, trimming the trailing newline. */
static void
sReadLine(const char *path, char *out, size_t outsz)
{
    FILE *f = fopen(path, "r");
    if (!f || !fgets(out, (int)outsz, f)) {
        snprintf(out, outsz, "unknown");
    } else {
        size_t len = strlen(out);
        if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
    }
    if (f) fclose(f);
}

/* Read a single integer from a file; 0 if missing. */
static int
sReadVersionInt(const char *path)
{
    FILE *f = fopen(path, "r");
    int v = 0;
    if (f) { if (fscanf(f, "%d", &v) != 1) v = 0; fclose(f); }
    return v;
}

static int  g_version;           /* monotonic build number from VERSION */
static char g_gen_sha[64];       /* git hash of generator, from .version */
static char g_catalogue_sha[256]; /* git hash of catalogue(s) — room for core+adjunct */

/* Emit a generation timestamp and version info. */
static void
sEmitTimestamp(FILE *f)
{
    time_t now = time(NULL);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", ctime(&now));
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    fprintf(f, " * Version:    %d\n", g_version);
    fprintf(f, " * Generated:  %s\n", buf);
    fprintf(f, " * Generator:  %s\n", g_gen_sha);
    fprintf(f, " * Catalogue:  %s\n", g_catalogue_sha);
}

/* Emit a local include.  When --include-prefix is set, emits a
 * fully-qualified angle-bracket form; otherwise falls back to quoted.
 * When --prefix is set, the prefix is infixed into the filename stem
 * (e.g. "ufsrv_config_generated.h" → "ufsrv_config_adjunct_generated.h"). */
static void
sEmitLocalInclude(FILE *f, const char *subpath)
{
    char buf[512];
    const char *effective = subpath;

    if (g_prefix) {
        const char *stem = strstr(subpath, "_generated.");
        if (stem) {
            size_t plen = (size_t)(stem - subpath);
            snprintf(buf, sizeof(buf), "%.*s_%s%s",
                     (int)plen, subpath, g_prefix, stem);
        } else {
            const char *dot = strrchr(subpath, '.');
            if (dot) {
                size_t blen = (size_t)(dot - subpath);
                snprintf(buf, sizeof(buf), "%.*s_%s%s",
                         (int)blen, subpath, g_prefix, dot);
            } else {
                snprintf(buf, sizeof(buf), "%s_%s", subpath, g_prefix);
            }
        }
        effective = buf;
    }

    if (g_include_prefix)
        fprintf(f, "#include <%s/%s>\n", g_include_prefix, effective);
    else
        fprintf(f, "#include \"%s\"\n", effective);
}

// Forward declarations
static void sEmitEnumName(const char *field_name);
static const char *sUppercaseName(const char *field_name);
static void sEmitAccessorName(const char *field_name);
static void sEmitManifest(void);
static FILE *sOpenOutput(const char *filename);
static void sEmitDecomposedHeaderPreambleLight(FILE *f, const char *guard);
static void sEmitDecomposedFooter(FILE *f, const char *guard);
static void sBuildLiteralReverseMap(void);
static const char *sLookupMacroName(const char *string_value);
static void sGenerateDataLiteralToFieldMap(void);
static void sEmitConfLiteralToConfFieldMap(FILE *f);

// Validation

static void
sValidateNoDuplicates(void)
{
    int i, j;
    for (i = 0; i < gen_field_count; i++) {
        for (j = i + 1; j < gen_field_count; j++) {
            if (strcmp(gen_fields[i].desc->name,
                       gen_fields[j].desc->name) == 0) {
                fprintf(stderr, "ufsrv_config_generator: ERROR: duplicate field name \"%s\"\n",
                        gen_fields[i].desc->name);
                exit(1);
            }
        }
    }
}

static void
sValidateFields(void)
{
    int i;
    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        if (d->name == NULL || d->name[0] == '\0') {
            fprintf(stderr, "ufsrv_config_generator: ERROR: field %d has empty name\n", i);
            exit(1);
        }
        if (d->domain == UFSRV_CONFIG_DOMAIN_CONFIG && d->config_field_default.lifecycle.on_teardown != NULL) {
            fprintf(stderr, "ufsrv_config_generator: ERROR: CONFIG field \"%s\" has non‑NULL teardown\n",
                    d->name);
            exit(1);
        }
        if (d->domain == UFSRV_CONFIG_DOMAIN_RUNTIME &&
            d->validation.type != UFSRV_CONFIG_CHECK_NONE) {
            fprintf(stderr, "ufsrv_config_generator: ERROR: RUNTIME field \"%s\" has validation rule\n",
                    d->name);
            exit(1);
        }
        if (d->value_type < 0 ||
            (d->value_type > UFSRV_CONFIG_VALUE_ENUM &&
             d->value_type != UFSRV_CONFIG_VALUE_BOOL)) {
            fprintf(stderr, "ufsrv_config_generator: ERROR: field \"%s\" has unknown value_type %d\n",
                    d->name, d->value_type);
            exit(1);
        }
    }
}

// Field collection

static void
sScanCatalogue(const UfsrvConfigFieldDescriptor *fields, size_t count,
               const char *catalogue_name)
{
    size_t i;
    (void)catalogue_name;
    for (i = 0; i < count; i++) {
        const UfsrvConfigFieldDescriptor *d = &fields[i];
        if (d->name == NULL)
            continue;
        if (gen_field_count >= MAX_FIELDS) {
            fprintf(stderr, "ufsrv_config_generator: ERROR: too many fields (max %d)\n", MAX_FIELDS);
            exit(1);
        }
        GenField *gf = &gen_fields[gen_field_count++];
        gf->desc = d;
        if (d->domain == UFSRV_CONFIG_DOMAIN_CONFIG) {
            gf->config_index  = config_field_count++;
            gf->runtime_index = -1;
        } else {
            gf->config_index  = -1;
            gf->runtime_index = runtime_field_count++;
        }
    }
}

// Header boilerplate

static void sEmitHeader(void)
{
    fprintf(g_out,"/*\n");
    fprintf(g_out," * ufsrv_config_generated.h — GENERATED BY ufsrv_config_generator, DO NOT EDIT\n");
    fprintf(g_out," *\n");
    sEmitTimestamp(g_out);
    fprintf(g_out," *   Config fields:   %d\n", config_field_count);
    fprintf(g_out," *   Runtime fields:  %d\n", runtime_field_count);
    fprintf(g_out," */\n\n");
    fprintf(g_out,"#ifndef UFSRV_CORE_UFSRV_CONFIG_GENERATED_H\n");
    fprintf(g_out,"#define UFSRV_CORE_UFSRV_CONFIG_GENERATED_H\n\n");
    fprintf(g_out,"#include <sys/types.h>          /* pid_t */\n");
    fprintf(g_out,"#include <stdint.h>             /* uint8_t */\n");
    fprintf(g_out,"#include <uflib/standard_defs.h>\n");
    sEmitLocalInclude(g_out, "ufsrvcorelib_defs.h");
    sEmitLocalInclude(g_out, "ufsrv_core/ufsrv_config/ufsrv_config_types.h");
    sEmitLocalInclude(g_out, "ufsrv_core/ufsrv_config/ufsrv_config_value_type.h");
    sEmitLocalInclude(g_out, "ufsrv_core/ufsrv_config/ufsrv_config_field_descriptor.h");
    sEmitLocalInclude(g_out, "ufsrv_core/configfile_processor/configfile_processor_symbol_table_type.h");
    sEmitLocalInclude(g_out, "ufsrv_core/ufsrv_config/ufsrv_config_store_operating_state_enum.h");
    if (g_prefix) {
        fprintf(g_out,"\n/* Per-library prefix: %s */\n", g_prefix);
        fprintf(g_out,"#define ufsrv_config_field_table  ufsrv_config%s_field_table\n", sPrefixFile());
        fprintf(g_out,"#define ufsrv_runtime_field_table ufsrv_runtime%s_field_table\n", sPrefixFile());
        fprintf(g_out,"#define UFSRV_CONFIG_FIELD_PREFIX  %s_\n", sPrefixUpper());
        fprintf(g_out,"#define UFSRV_RUNTIME_FIELD_PREFIX %s_\n", sPrefixUpper());
        fprintf(g_out,"#define UFSRV_CONFIG_FIELD_COUNT   UFSRV%s_CONFIG_FIELD_COUNT\n", sPrefixUpper());
        fprintf(g_out,"#define UFSRV_RUNTIME_FIELD_COUNT  UFSRV%s_RUNTIME_FIELD_COUNT\n", sPrefixUpper());
    }
}

static void sEmitFooter(void)
{
    fprintf(g_out,"\n#endif /* UFSRV_CORE_UFSRV_CONFIG_GENERATED_H */\n");
}

static void sEmitSourcePreamble(void)
{
    fprintf(g_out,"/*\n");
    fprintf(g_out," * ufsrv_config_getters_setters_generated.c — GENERATED, DO NOT EDIT\n");
    fprintf(g_out," *\n");
    sEmitTimestamp(g_out);
    fprintf(g_out," *   Config fields:   %d\n", config_field_count);
    fprintf(g_out," *   Runtime fields:  %d\n", runtime_field_count);
    fprintf(g_out," */\n\n");
    fprintf(g_out,"#include <stdlib.h>  /* malloc, free */\n");
    fprintf(g_out,"#include <string.h>  /* memcpy, strncpy */\n");
    sEmitLocalInclude(g_out, "ufsrv_core/ufsrv_config/ufsrv_config.h");
    sEmitLocalInclude(g_out, "ufsrv_core/ufsrv_config/generated/ufsrv_config_field_enums_generated.h");
    sEmitLocalInclude(g_out, "ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h");
    if (g_prefix) {
        fprintf(g_out,"\n/* Per-library prefix: %s */\n", g_prefix);
        fprintf(g_out,"#define ufsrv_config_field_table  ufsrv_config%s_field_table\n", sPrefixFile());
        fprintf(g_out,"#define ufsrv_runtime_field_table ufsrv_runtime%s_field_table\n", sPrefixFile());
        fprintf(g_out,"#define ufsrv_config_field_defaults_table  ufsrv_config%s_field_defaults_table\n", sPrefixFile());
        fprintf(g_out,"#define ufsrv_runtime_field_defaults_table ufsrv_runtime%s_field_defaults_table\n", sPrefixFile());
        fprintf(g_out,"#define UFSRV_CONFIG_FIELD_PREFIX  %s_\n", sPrefixUpper());
        fprintf(g_out,"#define UFSRV_RUNTIME_FIELD_PREFIX %s_\n", sPrefixUpper());
        fprintf(g_out,"#define UFSRV_CONFIG_FIELD_COUNT   UFSRV%s_CONFIG_FIELD_COUNT\n", sPrefixUpper());
        fprintf(g_out,"#define UFSRV_RUNTIME_FIELD_COUNT  UFSRV%s_RUNTIME_FIELD_COUNT\n", sPrefixUpper());
    }
}

// Enums (header only)

static void sEmitConfigEnum(void)
{
    int i;
    fprintf(g_out,"\n// Config field indices\n\n");
    fprintf(g_out,"enum UfsrvConfig%sField {\n", sPrefixCap());
    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].config_index >= 0) {
            fprintf(g_out,INDENT2 "UFSRV%s_CONFIG_FIELD_", sPrefixUpper());
            sEmitEnumName(gen_fields[i].desc->name);
            fprintf(g_out," = %d,\n", gen_fields[i].config_index);
        }
    }
    fprintf(g_out,INDENT2 "UFSRV%s_CONFIG_FIELD_COUNT = %d\n", sPrefixUpper(), config_field_count);
    fprintf(g_out,"};\n");
}

static void sEmitRuntimeEnum(void)
{
    int i;
    fprintf(g_out,"\n// Runtime field indices\n\n");
    fprintf(g_out,"enum UfsrvRuntime%sField {\n", sPrefixCap());
    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].runtime_index >= 0) {
            fprintf(g_out,INDENT2 "UFSRV%s_RUNTIME_FIELD_", sPrefixUpper());
            sEmitEnumName(gen_fields[i].desc->name);
            fprintf(g_out," = %d,\n", gen_fields[i].runtime_index);
        }
    }
    fprintf(g_out,INDENT2 "UFSRV%s_RUNTIME_FIELD_COUNT = %d\n", sPrefixUpper(), runtime_field_count);
    fprintf(g_out,"};\n");
}

// Name helpers

static void
sEmitAccessorName(const char *field_name)
{
    int cap_next = 1;
    while (*field_name) {
        if (*field_name == '_') { cap_next = 1; field_name++; continue; }
        fputc(cap_next ? (char)(*field_name - 32) : *field_name, g_out);
        cap_next = 0;
        field_name++;
    }
}

static void
sEmitEnumName(const char *field_name)
{
    while (*field_name) {
        if (*field_name == '_') { fputc('_', g_out); field_name++; continue; }
        fputc((*field_name >= 'a' && *field_name <= 'z')
                ? (char)(*field_name - 32) : *field_name, g_out);
        field_name++;
    }
}

static const char *
sUppercaseName(const char *field_name)
{
    static char buf[128];
    int i = 0;
    while (*field_name && i < (int)sizeof(buf) - 1) {
        if (*field_name == '_') { buf[i++] = '_'; field_name++; continue; }
        buf[i++] = (*field_name >= 'a' && *field_name <= 'z')
                   ? (char)(*field_name - 32) : *field_name;
        field_name++;
    }
    buf[i] = '\0';
    return buf;
}

// ── AsString marshaller dispatch ──────────────────────────────────────────
// Standard scalar c_lang_types map to a generic Generic<X>AsString marshaller.
// The table is the single controlled place to extend the set of standard types.

typedef struct {
    const char *c_lang_type;   /* e.g. "int" */
    const char *as_string_fn;  /* e.g. "GenericIntAsString" */
} AsStringGenericType;

static const AsStringGenericType g_as_string_generic_types[] = {
    { "int",      "GenericIntAsString" },
    { "double",   "GenericDoubleAsString" },
    { "bool",     "GenericBoolAsString" },
    { "char *",   "GenericCharPtrAsString" },
    { "unsigned", "GenericUnsignedAsString" },
    { "unsigned char", "GenericUnsignedCharAsString" },
    { "size_t",   "GenericSizeTAsString" },
    { "time_t",   "GenericTimeTAsString" },
    { "pid_t",    "GenericPidTAsString" },
    { "struct timeval",   "GenericTimevalAsString" },
    { "struct timeval *", "GenericTimevalPtrAsString" },
};
static const size_t g_as_string_generic_type_count =
    sizeof(g_as_string_generic_types) / sizeof(g_as_string_generic_types[0]);

/* Generic AsString marshaller for a standard c_lang_type, or NULL when the
 * type is non-standard (hand-authored). */
static const char *
sGenericAsStringFn(const char *c_lang_type)
{
    size_t i;
    for (i = 0; i < g_as_string_generic_type_count; i++)
        if (strcmp(g_as_string_generic_types[i].c_lang_type, c_lang_type) == 0)
            return g_as_string_generic_types[i].as_string_fn;
    return NULL;
}

/* Sanitize c_lang_type into a C identifier suffix for the hand-authored
 * helper: "struct timeval" → "struct_timeval", "SSL_CTX *" → "SSL_CTX_pointer". */
static const char *
sAsStringTypeSuffix(const char *c_lang_type)
{
    static char buf[256];
    size_t i = 0;
    while (*c_lang_type && i < sizeof(buf) - 1) {
        if (*c_lang_type == ' ') {
            buf[i++] = '_';
        } else if (*c_lang_type == '*') {
            const char *s = "pointer";
            while (*s && i < sizeof(buf) - 1) buf[i++] = *s++;
        } else {
            buf[i++] = *c_lang_type;
        }
        c_lang_type++;
    }
    buf[i] = '\0';
    return buf;
}

/* PascalCase field name as a string (mirrors sEmitAccessorName). */
static const char *
sAccessorNameStr(const char *field_name)
{
    static char buf[128];
    int i = 0, cap_next = 1;
    while (*field_name && i < (int)sizeof(buf) - 1) {
        if (*field_name == '_') { cap_next = 1; field_name++; continue; }
        buf[i++] = (char)(cap_next ? (*field_name - 32) : *field_name);
        cap_next = 0;
        field_name++;
    }
    buf[i] = '\0';
    return buf;
}

// Config accessor declarations (header)

static void sEmitConfigAccessorDecls(void)
{
    int i;
    fprintf(g_out,"\n// Config accessors\n");
    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].config_index >= 0) {
            const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
            if (d->is_keep_custom_getter) {
                /* Custom getter — definition hand-authored by the consumer.
                 * Emit an extern declaration under the standard name so
                 * callers use the same accessor regardless of who defines it. */
                fprintf(g_out,"\nextern PUBLIC_API %s\nUfsrvConfig", d->c_lang_type);
                sEmitAccessorName(d->name);
                fprintf(g_out,"(void);\n");
            } else {
                fprintf(g_out,"\nPUBLIC_API %s\nUfsrvConfig", d->c_lang_type);
                sEmitAccessorName(d->name);
                fprintf(g_out,"(void);\n");
            }
            if (d->value_type != UFSRV_CONFIG_VALUE_CONST_PTR) {
                if (d->is_keep_custom_setter) {
                    /* Custom setter — definition hand-authored by the consumer.
                     * Emit an extern declaration under the standard name. */
                    fprintf(g_out,"extern PUBLIC_API void\nUfsrvConfigSet");
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"(UfsrvConfigItem *item, UfsrvConfigSource config_source);\n");
                    /* Lifecycle bridge — generated.  Adapts on_value_available's
                     * (int, const UfsrvConfigValue *, UfsrvConfigSource) to the
                     * setter's (UfsrvConfigItem *, UfsrvConfigSource) and
                     * forwards to the hand-authored UfsrvConfigSet<Name>. */
                    fprintf(g_out,"PUBLIC_API void\nUfsrvConfig");
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"Setter(int field_index, const UfsrvConfigValue *value_ptr, UfsrvConfigSource source);\n");
                } else {
                    fprintf(g_out,"PUBLIC_API void\nUfsrvConfigSet");
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"(UfsrvConfigItem *item, UfsrvConfigSource config_source);\n");
                }
            }
            if (d->is_as_ptr_getter) {
                if (d->is_keep_custom_getter) {
                    /* Custom getter — AsPtr definition hand-authored; emit an
                     * extern declaration under the standard name. */
                    fprintf(g_out,"extern PUBLIC_API %s *\nUfsrvConfig", d->c_lang_type);
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"AsPtr(void);\n");
                } else {
                    fprintf(g_out,"PUBLIC_API %s *\nUfsrvConfig", d->c_lang_type);
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"AsPtr(void);\n");
                }
            }
        }
    }
}

// Config accessor definitions (source)

static void sEmitConfigGetterDef(const GenField *gf)
{
    const UfsrvConfigFieldDescriptor *d = gf->desc;

    if (d->is_keep_custom_getter)
        return;   /* user-supplied custom getter — not emitted */

    const char *type_name = d->c_lang_type;

    fprintf(g_out,"%s\nUfsrvConfig", type_name);
    sEmitAccessorName(d->name);
    fprintf(g_out,"(void)\n{\n");

    switch (d->value_type) {
    case UFSRV_CONFIG_VALUE_INT:
    case UFSRV_CONFIG_VALUE_ENUM:
    case UFSRV_CONFIG_VALUE_BOOL:
        fprintf(g_out,INDENT2 "return (%s)UfsrvConfigConfFileValuesInstance(\n"
               INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.int_val;\n",
               type_name, sPrefixUpper(), sUppercaseName(d->name));
        break;
    case UFSRV_CONFIG_VALUE_STR:
        fprintf(g_out,INDENT2 "return UfsrvConfigConfFileValuesInstance(\n"
               INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.str_val;\n",
               sPrefixUpper(), sUppercaseName(d->name));
        break;
    case UFSRV_CONFIG_VALUE_STRUCT:
        fprintf(g_out,INDENT2 "return *(const %s *)UfsrvConfigConfFileValuesInstance(\n"
               INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.blob_val;\n",
               type_name, sPrefixUpper(), sUppercaseName(d->name));
        break;
    case UFSRV_CONFIG_VALUE_PTR:
        fprintf(g_out,INDENT2 "return (%s)UfsrvConfigConfFileValuesInstance(\n"
               INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.ptr_val;\n",
               type_name, sPrefixUpper(), sUppercaseName(d->name));
        break;
    case UFSRV_CONFIG_VALUE_CONST_PTR:
        fprintf(g_out,INDENT2 "return (%s)UfsrvConfigConfFileValuesInstance(\n"
               INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.ptr_val;\n",
               type_name, sPrefixUpper(), sUppercaseName(d->name));
        break;
    default:
        fprintf(stderr, "ufsrv_config_generator: ERROR: unsupported value_type %d for \"%s\"\n",
                d->value_type, d->name);
        exit(1);
    }
    fprintf(g_out,"}\n");

    /* AsPtr getter — returns pointer to the store slot so callers can
     * read or mutate the value in place (before WORM boundary). */
    if (d->is_as_ptr_getter) {
        fprintf(g_out,"\n%s *\nUfsrvConfig", type_name);
        sEmitAccessorName(d->name);
        fprintf(g_out,"AsPtr(void)\n{\n");
        switch (d->value_type) {
        case UFSRV_CONFIG_VALUE_INT:
        case UFSRV_CONFIG_VALUE_ENUM:
        case UFSRV_CONFIG_VALUE_BOOL:
            fprintf(g_out,INDENT2 "return &UfsrvConfigConfFileValuesInstance(\n"
                   INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.int_val;\n",
                   sPrefixUpper(), sUppercaseName(d->name));
            break;
        case UFSRV_CONFIG_VALUE_STR:
            fprintf(g_out,INDENT2 "return &UfsrvConfigConfFileValuesInstance(\n"
                   INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.str_val;\n",
                   sPrefixUpper(), sUppercaseName(d->name));
            break;
        case UFSRV_CONFIG_VALUE_STRUCT:
            fprintf(g_out,INDENT2 "return (%s *)UfsrvConfigConfFileValuesInstance(\n"
                   INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.blob_val;\n",
                   type_name, sPrefixUpper(), sUppercaseName(d->name));
            break;
        case UFSRV_CONFIG_VALUE_PTR:
        case UFSRV_CONFIG_VALUE_CONST_PTR:
            fprintf(g_out,INDENT2 "return (%s *)&UfsrvConfigConfFileValuesInstance(\n"
                   INDENT4 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.ptr_val;\n",
                   type_name, sPrefixUpper(), sUppercaseName(d->name));
            break;
        default:
            fprintf(stderr, "ufsrv_config_generator: ERROR: "
                    "unsupported value_type %d for \"%s\" AsPtr\n",
                    d->value_type, d->name);
            exit(1);
        }
        fprintf(g_out,"}\n");
    }
}

static void sEmitConfigSetterDef(const GenField *gf)
{
    const UfsrvConfigFieldDescriptor *d = gf->desc;

    if (d->is_keep_custom_setter)
        return;   /* user-supplied custom setter — not emitted */

    if (d->value_type == UFSRV_CONFIG_VALUE_CONST_PTR)
        return;   /* read-only — no setter emitted */

    fprintf(g_out,"void\nUfsrvConfigSet");
    sEmitAccessorName(d->name);
    fprintf(g_out,"(UfsrvConfigItem *item, UfsrvConfigSource config_source)\n{\n");
    fprintf(g_out,INDENT2 "UfsrvConfigFieldEntry *_entry = UfsrvConfigConfFileStoreInstance(\n"
                        INDENT4 "UFSRV%s_CONFIG_FIELD_%s);\n",
            sPrefixUpper(), sUppercaseName(d->name));
    fprintf(g_out,INDENT2 "if (_entry->lifecycle.on_value_available)\n");
    fprintf(g_out,INDENT4 "_entry->lifecycle.on_value_available(\n"
                        INDENT4 INDENT2 "UFSRV%s_CONFIG_FIELD_%s, &item->value, config_source);\n",
            sPrefixUpper(), sUppercaseName(d->name));
    fprintf(g_out,"}\n");
}

static void sEmitCustomSetterBridgeDef(const GenField *gf)
{
    const UfsrvConfigFieldDescriptor *d = gf->desc;

    if (!d->is_keep_custom_setter)
        return;   /* only custom-setter fields need a lifecycle bridge */

    /* Lifecycle bridge — adapts on_value_available's
     * (int, const UfsrvConfigValue *, UfsrvConfigSource) signature to the
     * hand-authored setter's (UfsrvConfigItem *, UfsrvConfigSource), then
     * forwards.  Emitted only for is_keep_custom_setter fields so the generic
     * dispatch can still reach the custom setter. */
    fprintf(g_out,"void\nUfsrvConfig");
    sEmitAccessorName(d->name);
    fprintf(g_out,"Setter(int field_index, const UfsrvConfigValue *value_ptr, UfsrvConfigSource source)\n{\n");
    fprintf(g_out,INDENT2 "(void)field_index;\n");
    fprintf(g_out,INDENT2 "UfsrvConfigItem item = { .layer = source, .value = *value_ptr };\n");
    fprintf(g_out,INDENT2 "UfsrvConfigSet");
    sEmitAccessorName(d->name);
    fprintf(g_out,"(&item, source);\n");
    fprintf(g_out,"}\n");
}

static void sEmitRuntimeGetterDef(const GenField *gf)
{
    const UfsrvConfigFieldDescriptor *d = gf->desc;

    if (d->is_keep_custom_getter)
        return;   /* user-supplied custom getter — not emitted */

    fprintf(g_out,"%s\nUfsrvConfig", d->c_lang_type);
    sEmitAccessorName(d->name);
    fprintf(g_out,"(void)\n{\n");

    switch (d->value_type) {
    case UFSRV_CONFIG_VALUE_INT:
    case UFSRV_CONFIG_VALUE_ENUM:
    case UFSRV_CONFIG_VALUE_BOOL:
        fprintf(g_out,INDENT2 "return (%s)UfsrvConfigRuntimeValuesInstance(\n"
               INDENT4 "UFSRV%s_RUNTIME_FIELD_%s)->stored_value.int_val;\n",
               d->c_lang_type, sPrefixUpper(), sUppercaseName(d->name));
        break;
    case UFSRV_CONFIG_VALUE_STR:
        fprintf(g_out,INDENT2 "return UfsrvConfigRuntimeValuesInstance(\n"
               INDENT4 "UFSRV%s_RUNTIME_FIELD_%s)->stored_value.str_val;\n",
               sPrefixUpper(), sUppercaseName(d->name));
        break;
    case UFSRV_CONFIG_VALUE_STRUCT:
        fprintf(g_out,INDENT2 "return *(const %s *)UfsrvConfigRuntimeValuesInstance(\n"
               INDENT4 "UFSRV%s_RUNTIME_FIELD_%s)->stored_value.blob_val;\n",
               d->c_lang_type, sPrefixUpper(), sUppercaseName(d->name));
        break;
    case UFSRV_CONFIG_VALUE_PTR:
    case UFSRV_CONFIG_VALUE_CONST_PTR:
        fprintf(g_out,INDENT2 "return (%s)UfsrvConfigRuntimeValuesInstance(\n"
               INDENT4 "UFSRV%s_RUNTIME_FIELD_%s)->stored_value.ptr_val;\n",
               d->c_lang_type, sPrefixUpper(), sUppercaseName(d->name));
        break;
    default:
        fprintf(stderr, "ufsrv_config_generator: ERROR: unsupported value_type %d for \"%s\" runtime getter\n",
                d->value_type, d->name);
        exit(1);
    }
    fprintf(g_out,"}\n");

    /* AsPtr getter */
    if (d->is_as_ptr_getter) {
        fprintf(g_out,"\n%s *\nUfsrvConfig", d->c_lang_type);
        sEmitAccessorName(d->name);
        fprintf(g_out,"AsPtr(void)\n{\n");
        switch (d->value_type) {
        case UFSRV_CONFIG_VALUE_INT:
        case UFSRV_CONFIG_VALUE_ENUM:
        case UFSRV_CONFIG_VALUE_BOOL:
            fprintf(g_out,INDENT2 "return &UfsrvConfigRuntimeValuesInstance(\n"
                   INDENT4 "UFSRV%s_RUNTIME_FIELD_%s)->stored_value.int_val;\n",
                   sPrefixUpper(), sUppercaseName(d->name));
            break;
        case UFSRV_CONFIG_VALUE_STR:
            fprintf(g_out,INDENT2 "return &UfsrvConfigRuntimeValuesInstance(\n"
                   INDENT4 "UFSRV%s_RUNTIME_FIELD_%s)->stored_value.str_val;\n",
                   sPrefixUpper(), sUppercaseName(d->name));
            break;
        case UFSRV_CONFIG_VALUE_STRUCT:
            fprintf(g_out,INDENT2 "return (%s *)UfsrvConfigRuntimeValuesInstance(\n"
                   INDENT4 "UFSRV%s_RUNTIME_FIELD_%s)->stored_value.blob_val;\n",
                   d->c_lang_type, sPrefixUpper(), sUppercaseName(d->name));
            break;
        case UFSRV_CONFIG_VALUE_PTR:
        case UFSRV_CONFIG_VALUE_CONST_PTR:
            fprintf(g_out,INDENT2 "return (%s *)&UfsrvConfigRuntimeValuesInstance(\n"
                   INDENT4 "UFSRV%s_RUNTIME_FIELD_%s)->stored_value.ptr_val;\n",
                   d->c_lang_type, sPrefixUpper(), sUppercaseName(d->name));
            break;
        default:
            fprintf(stderr, "ufsrv_config_generator: ERROR: "
                    "unsupported value_type %d for \"%s\" AsPtr\n",
                    d->value_type, d->name);
            exit(1);
        }
        fprintf(g_out,"}\n");
    }
}

static void sEmitRuntimeSetterDef(const GenField *gf)
{
    const UfsrvConfigFieldDescriptor *d = gf->desc;

    if (d->is_keep_custom_setter)
        return;   /* user-supplied custom setter — not emitted */

    if (d->value_type == UFSRV_CONFIG_VALUE_CONST_PTR)
        return;

    fprintf(g_out,"void\nUfsrvConfigSet");
    sEmitAccessorName(d->name);
    fprintf(g_out,"(UfsrvConfigItem *item, UfsrvConfigSource config_source)\n{\n");
    fprintf(g_out,INDENT2 "UfsrvConfigFieldEntry *_entry = UfsrvConfigRuntimeStoreInstance(\n"
                        INDENT4 "UFSRV%s_RUNTIME_FIELD_%s);\n",
            sPrefixUpper(), sUppercaseName(d->name));
    fprintf(g_out,INDENT2 "if (_entry->lifecycle.on_value_available)\n");
    fprintf(g_out,INDENT4 "_entry->lifecycle.on_value_available(\n"
                        INDENT4 INDENT2 "UFSRV%s_RUNTIME_FIELD_%s, &item->value, config_source);\n",
            sPrefixUpper(), sUppercaseName(d->name));
    fprintf(g_out,"}\n");
}

static void sEmitRuntimeAccessorDefs(void)
{
    int i;
    fprintf(g_out,"\n// Runtime accessors\n");
    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].runtime_index >= 0) {
            const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
            fprintf(g_out,"\n");
            if (!d->is_keep_custom_getter)
                sEmitRuntimeGetterDef(&gen_fields[i]);
            fprintf(g_out,"\n");
            if (d->is_keep_custom_setter)
                sEmitCustomSetterBridgeDef(&gen_fields[i]);
            else
                sEmitRuntimeSetterDef(&gen_fields[i]);
        }
    }
}

static void sEmitConfigAccessorDefs(void)
{
    int i;
    fprintf(g_out,"\n// Config accessors\n");
    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].config_index >= 0) {
            const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
            fprintf(g_out,"\n");
            if (!d->is_keep_custom_getter)
                sEmitConfigGetterDef(&gen_fields[i]);
            fprintf(g_out,"\n");
            if (d->is_keep_custom_setter)
                sEmitCustomSetterBridgeDef(&gen_fields[i]);
            else
                sEmitConfigSetterDef(&gen_fields[i]);
        }
    }
}

// Runtime accessor declarations (header)

static void sEmitRuntimeAccessorDecls(void)
{
    int i;
    fprintf(g_out,"\n// Runtime accessors — user refines stubs with concrete types\n");
    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].runtime_index >= 0) {
            const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
            if (d->is_keep_custom_getter) {
                /* Custom getter — definition hand-authored by the consumer.
                 * Emit an extern declaration under the standard name so
                 * callers use the same accessor regardless of who defines it. */
                fprintf(g_out,"\nextern PUBLIC_API %s\nUfsrvConfig", d->c_lang_type);
                sEmitAccessorName(d->name);
                fprintf(g_out,"(void);\n");
            } else {
                fprintf(g_out,"\nPUBLIC_API %s\nUfsrvConfig", d->c_lang_type);
                sEmitAccessorName(d->name);
                fprintf(g_out,"(void);\n");
            }
            if (d->value_type != UFSRV_CONFIG_VALUE_CONST_PTR) {
                if (d->is_keep_custom_setter) {
                    /* Custom setter — definition hand-authored by the consumer.
                     * Emit an extern declaration under the standard name. */
                    fprintf(g_out,"extern PUBLIC_API void\nUfsrvConfigSet");
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"(UfsrvConfigItem *item, UfsrvConfigSource config_source);\n");
                    /* Lifecycle bridge — generated.  Adapts on_value_available's
                     * (int, const UfsrvConfigValue *, UfsrvConfigSource) to the
                     * setter's (UfsrvConfigItem *, UfsrvConfigSource) and
                     * forwards to the hand-authored UfsrvConfigSet<Name>. */
                    fprintf(g_out,"PUBLIC_API void\nUfsrvConfig");
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"Setter(int field_index, const UfsrvConfigValue *value_ptr, UfsrvConfigSource source);\n");
                } else {
                    fprintf(g_out,"PUBLIC_API void\nUfsrvConfigSet");
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"(UfsrvConfigItem *item, UfsrvConfigSource config_source);\n");
                }
            }
            if (d->is_as_ptr_getter) {
                if (d->is_keep_custom_getter) {
                    /* Custom getter — AsPtr definition hand-authored; emit an
                     * extern declaration under the standard name. */
                    fprintf(g_out,"extern PUBLIC_API %s *\nUfsrvConfig", d->c_lang_type);
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"AsPtr(void);\n");
                } else {
                    fprintf(g_out,"PUBLIC_API %s *\nUfsrvConfig", d->c_lang_type);
                    sEmitAccessorName(d->name);
                    fprintf(g_out,"AsPtr(void);  // definition hand-authored in ufsrv_config.c\n");
                }
            }
        }
    }
}

// Validation — definition (source)

static void sEmitValidationDef(void)
{
    int i;
    fprintf(g_out,"\n// Validation\n\n");
    fprintf(g_out,"UfsrvConfigResult\n"); sEmitLifecycleName("Validate"); fprintf(g_out,"(void)\n{\n");

    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        if (d->domain != UFSRV_CONFIG_DOMAIN_CONFIG)
            continue;
        switch (d->validation.type) {
        case UFSRV_CONFIG_CHECK_NOT_EMPTY:
            fprintf(g_out,INDENT2 "// %s: not‑empty\n", d->name);
            fprintf(g_out,INDENT2 "{\n");
            fprintf(g_out,INDENT4 "const char *_v = UfsrvConfigConfFileValuesInstance(\n"
                   INDENT4 INDENT2 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.str_val;\n",
                   sPrefixUpper(), sUppercaseName(d->name));
            fprintf(g_out,INDENT4 "if (_v == NULL || _v[0] == '\\0') {\n");
            fprintf(g_out,INDENT4 "UfsrvConfigResult r = { UFSRV_CONFIG_ERR_EMPTY,\n");
            fprintf(g_out,INDENT4 "                        \"%s: must not be empty\" };\n", d->name);
            fprintf(g_out,INDENT4 "return r;\n");
            fprintf(g_out,INDENT4 "}\n");
            fprintf(g_out,INDENT2 "}\n");
            break;
        case UFSRV_CONFIG_CHECK_RANGE:
        case UFSRV_CONFIG_CHECK_ENUM_RANGE:
            fprintf(g_out,INDENT2 "// %s: range [%d, %d]\n", d->name,
                   d->validation.check.range.min, d->validation.check.range.max);
            fprintf(g_out,INDENT2 "{\n");
            fprintf(g_out,INDENT4 "int64_t _v = UfsrvConfigConfFileValuesInstance(\n"
                   INDENT4 INDENT2 "UFSRV%s_CONFIG_FIELD_%s)->stored_value.int_val;\n",
                   sPrefixUpper(), sUppercaseName(d->name));
            fprintf(g_out,INDENT4 "if (_v < %d || _v > %d) {\n",
                   d->validation.check.range.min, d->validation.check.range.max);
            fprintf(g_out,INDENT4 "UfsrvConfigResult r = { UFSRV_CONFIG_ERR_RANGE,\n");
            fprintf(g_out,INDENT4 "                        \"%s: out of range [%d, %d]\" };\n",
                   d->name, d->validation.check.range.min, d->validation.check.range.max);
            fprintf(g_out,INDENT4 "return r;\n");
            fprintf(g_out,INDENT4 "}\n");
            fprintf(g_out,INDENT2 "}\n");
            break;
        case UFSRV_CONFIG_CHECK_PATH_EXISTS:
        case UFSRV_CONFIG_CHECK_IS_DIRECTORY:
        case UFSRV_CONFIG_CHECK_IS_FILE:
            fprintf(g_out,INDENT2 "// TODO: %s — path validation (check type %d)\n",
                   d->name, d->validation.type);
            break;
        case UFSRV_CONFIG_CHECK_NONE:
            break;
        default:
            fprintf(stderr, "ufsrv_config_generator: WARNING: unknown check type %d for \"%s\"\n",
                    d->validation.type, d->name);
            break;
        }
    }
    fprintf(g_out,INDENT2 "UfsrvConfigResult ok = { UFSRV_CONFIG_OK, NULL };\n");
    fprintf(g_out,INDENT2 "return ok;\n");
    fprintf(g_out,"}\n");
}


// Lifecycle callback name helpers — read directly from the cold catalogue.
// The catalogue is the single source of truth; no fallback logic.
// NULL or empty string → emitted as NULL (no callback wired).

static const char *sLifecycleName(const char *name)
{
    return (name && name[0]) ? name : "NULL";
}

/* Map a catalogue lifecycle callback name to the correct variant for the
 * given domain.  CONFIG callbacks access ufsrv_config_conf_file_store_self();
 * RUNTIME callbacks access ufsrv_config_runtime_store_self().
 * Custom (non-generic) callbacks pass through unchanged. */
static const char *sLifecycleNameForDomain(const char *name, int domain)
{
    if (!name || !name[0]) return "NULL";
    if (domain != UFSRV_CONFIG_DOMAIN_RUNTIME) return name;

    /* Generic callbacks → Runtime-variant */
    if (strcmp(name, "GenericIntInflator")      == 0) return "GenericIntInflatorRuntime";
    if (strcmp(name, "GenericBoolInflator")     == 0) return "GenericBoolInflatorRuntime";
    if (strcmp(name, "GenericStrInflator")      == 0) return "GenericStrInflatorRuntime";
    if (strcmp(name, "GenericStructInflator")   == 0) return "GenericStructInflatorRuntime";
    if (strcmp(name, "GenericPtrInflator")      == 0) return "GenericPtrInflatorRuntime";
    if (strcmp(name, "GenericIntSetter")        == 0) return "GenericIntSetterRuntime";
    if (strcmp(name, "GenericBoolSetter")       == 0) return "GenericBoolSetterRuntime";
    if (strcmp(name, "GenericStrSetter")        == 0) return "GenericStrSetterRuntime";
    if (strcmp(name, "GenericStructSetter")     == 0) return "GenericStructSetterRuntime";
    if (strcmp(name, "GenericPtrSetter")        == 0) return "GenericPtrSetterRuntime";
    if (strcmp(name, "GenericStrTeardown")      == 0) return "GenericStrTeardownRuntime";
    if (strcmp(name, "GenericStructTeardown")   == 0) return "GenericStructTeardownRuntime";
    /* GenericReloadedStub and custom callbacks need no domain differentiation */
    return name;
}

// ═════════════════════════════════════════════════════════════════════════════
// Field tables — one per domain, indexed by respective enum
// ═════════════════════════════════════════════════════════════════════════════

static void sEmitConfigFieldTable(void)
{
    int i;
    fprintf(g_out,"\n// ── Config field table — indexed by enum UfsrvConfig%sField\n\n", sPrefixCap());
    fprintf(g_out,"static UfsrvConfigFieldEntry\n");
    fprintf(g_out,"ufsrv_config_field_table[UFSRV%s_CONFIG_FIELD_COUNT] = {\n", sPrefixUpper());

    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].config_index < 0) continue;
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        const char *inflator = sLifecycleName(d->lifecycle.on_inflated);
        const char *setter   = sLifecycleName(d->lifecycle.on_value_available);
        const char *reloaded = sLifecycleName(d->lifecycle.on_value_reloaded);

        fprintf(g_out,INDENT2 "[UFSRV%s_CONFIG_FIELD_%s] = {\n", sPrefixUpper(), sUppercaseName(d->name));
        fprintf(g_out,INDENT4 ".field_name = \"%s\",\n", d->name);
        fprintf(g_out,INDENT4 ".config = { 0 },\n");
        fprintf(g_out,INDENT4 ".lifecycle = {\n");
        fprintf(g_out,INDENT4 INDENT2 ".on_inflated        = %s,\n", inflator);
        fprintf(g_out,INDENT4 INDENT2 ".on_value_available = ");
        if (d->is_keep_custom_setter) {
            fprintf(g_out, "UfsrvConfig");
            sEmitAccessorName(d->name);
            fprintf(g_out, "Setter,\n");
        } else {
            fprintf(g_out, "%s,\n", setter);
        }
        fprintf(g_out,INDENT4 INDENT2 ".on_value_reloaded  = %s,\n", reloaded);
        fprintf(g_out,INDENT4 INDENT2 ".on_as_string       = UfsrvConfig");
        sEmitAccessorName(d->name);
        fprintf(g_out,"AsString,\n");
        fprintf(g_out,INDENT4 "},\n");
        fprintf(g_out,INDENT2 "},\n");
    }

    fprintf(g_out,"};\n");
}

static void sEmitRuntimeFieldTable(void)
{
    int i;
    fprintf(g_out,"\n// ── Runtime field table — indexed by enum UfsrvRuntime%sField\n\n", sPrefixCap());
    fprintf(g_out,"static UfsrvConfigFieldEntry\n");
    fprintf(g_out,"ufsrv_runtime_field_table[UFSRV%s_RUNTIME_FIELD_COUNT] = {\n", sPrefixUpper());

    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].runtime_index < 0) continue;
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;

        const char *inflator  = sLifecycleNameForDomain(d->lifecycle.on_inflated, UFSRV_CONFIG_DOMAIN_RUNTIME);
        const char *setter    = sLifecycleNameForDomain(d->lifecycle.on_value_available, UFSRV_CONFIG_DOMAIN_RUNTIME);
        const char *reloaded  = sLifecycleNameForDomain(d->lifecycle.on_value_reloaded, UFSRV_CONFIG_DOMAIN_RUNTIME);
        const char *teardown  = sLifecycleNameForDomain(d->lifecycle.on_teardown, UFSRV_CONFIG_DOMAIN_RUNTIME);

        fprintf(g_out,INDENT2 "[UFSRV%s_RUNTIME_FIELD_%s] = {\n", sPrefixUpper(), sUppercaseName(d->name));
        fprintf(g_out,INDENT4 ".field_name = \"%s\",\n", d->name);
        fprintf(g_out,INDENT4 ".config = { 0 },\n");
        fprintf(g_out,INDENT4 ".lifecycle = {\n");
        fprintf(g_out,INDENT4 INDENT2 ".on_inflated        = %s,\n", inflator);
        fprintf(g_out,INDENT4 INDENT2 ".on_value_available = ");
        if (d->is_keep_custom_setter) {
            fprintf(g_out, "UfsrvConfig");
            sEmitAccessorName(d->name);
            fprintf(g_out, "Setter,\n");
        } else {
            fprintf(g_out, "%s,\n", setter);
        }
        fprintf(g_out,INDENT4 INDENT2 ".on_value_reloaded  = %s,\n", reloaded);
        fprintf(g_out,INDENT4 INDENT2 ".on_teardown        = %s,\n", teardown);
        fprintf(g_out,INDENT4 INDENT2 ".on_as_string       = UfsrvConfig");
        sEmitAccessorName(d->name);
        fprintf(g_out,"AsString,\n");
        fprintf(g_out,INDENT4 "},\n");
        fprintf(g_out,INDENT2 "},\n");
    }

    fprintf(g_out,"};\n");
}

// ═════════════════════════════════════════════════════════════════════════════
// Compiled-defaults tables — UfsrvConfigValue arrays parallel to field tables
// ═════════════════════════════════════════════════════════════════════════════

static void sEmitConfigFieldDefaultsTable(void)
{
    int i;
    fprintf(g_out,"\n// ── Config field compiled defaults — indexed by enum UfsrvConfig%sField\n\n", sPrefixCap());
    fprintf(g_out,"static const UfsrvConfigValue\n");
    fprintf(g_out,"ufsrv_config_field_defaults_table[UFSRV%s_CONFIG_FIELD_COUNT] = {\n", sPrefixUpper());

    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].config_index < 0) continue;
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;

        fprintf(g_out, INDENT2 "[UFSRV%s_CONFIG_FIELD_%s] = { ", sPrefixUpper(), sUppercaseName(d->name));

        switch (d->value_type) {
        case UFSRV_CONFIG_VALUE_INT:
        case UFSRV_CONFIG_VALUE_ENUM:
        case UFSRV_CONFIG_VALUE_BOOL:
            fprintf(g_out, ".stored_value = { .int_val = %lld }, .type_allocation_sz = 0 },\n",
                   (long long) d->config_field_default.config.stored_value.int_val);
            break;
        case UFSRV_CONFIG_VALUE_STR:
            fprintf(g_out, ".stored_value = { .str_val = \"%s\" }, .type_allocation_sz = %zu },\n",
                   d->config_field_default.config.stored_value.str_val
                     ? d->config_field_default.config.stored_value.str_val : "",
                   d->config_field_default.config.type_allocation_sz);
            break;
        case UFSRV_CONFIG_VALUE_STRUCT:
            fprintf(g_out, ".stored_value = { .int_val = 0 }, .type_allocation_sz = %zu },\n",
                   d->config_field_default.config.type_allocation_sz);
            break;
        case UFSRV_CONFIG_VALUE_PTR:
        case UFSRV_CONFIG_VALUE_CONST_PTR:
        default:
            fprintf(g_out, ".stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },\n");
            break;
        }
    }

    fprintf(g_out,"};\n");
}

static void sEmitRuntimeFieldDefaultsTable(void)
{
    int i;
    fprintf(g_out,"\n// ── Runtime field compiled defaults — indexed by enum UfsrvRuntime%sField\n\n", sPrefixCap());
    fprintf(g_out,"static const UfsrvConfigValue\n");
    fprintf(g_out,"ufsrv_runtime_field_defaults_table[UFSRV%s_RUNTIME_FIELD_COUNT] = {\n", sPrefixUpper());

    for (i = 0; i < gen_field_count; i++) {
        if (gen_fields[i].runtime_index < 0) continue;
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;

        fprintf(g_out, INDENT2 "[UFSRV%s_RUNTIME_FIELD_%s] = { ", sPrefixUpper(), sUppercaseName(d->name));

        switch (d->value_type) {
        case UFSRV_CONFIG_VALUE_INT:
        case UFSRV_CONFIG_VALUE_ENUM:
        case UFSRV_CONFIG_VALUE_BOOL:
            fprintf(g_out, ".stored_value = { .int_val = %lld }, .type_allocation_sz = 0 },\n",
                   (long long) d->config_field_default.config.stored_value.int_val);
            break;
        case UFSRV_CONFIG_VALUE_STR:
            fprintf(g_out, ".stored_value = { .str_val = \"%s\" }, .type_allocation_sz = %zu },\n",
                   d->config_field_default.config.stored_value.str_val
                     ? d->config_field_default.config.stored_value.str_val : "",
                   d->config_field_default.config.type_allocation_sz);
            break;
        case UFSRV_CONFIG_VALUE_STRUCT:
            fprintf(g_out, ".stored_value = { .int_val = 0 }, .type_allocation_sz = %zu },\n",
                   d->config_field_default.config.type_allocation_sz);
            break;
        case UFSRV_CONFIG_VALUE_PTR:
        case UFSRV_CONFIG_VALUE_CONST_PTR:
        default:
            fprintf(g_out, ".stored_value = { .int_val = 0 }, .type_allocation_sz = 0 },\n");
            break;
        }
    }

    fprintf(g_out,"};\n");
}

// Dispatch table (header only, #if 0)

static int sCompareDispatch(const void *a, const void *b)
{
    const GenField *fa = (const GenField *)a;
    const GenField *fb = (const GenField *)b;
    return strcmp(fa->desc->name, fb->desc->name);
}

static void sEmitDispatchTable(void)
{
    int i;
    fprintf(g_out,"\n// Dispatch table — preserved, not compiled\n\n");
    fprintf(g_out,"#if 0\n");
    fprintf(g_out,"// Sorted alphabetically for bsearch.\n");
    fprintf(g_out,"static const struct {\n");
    fprintf(g_out,INDENT2 "const char *name;\n");
    fprintf(g_out,INDENT2 "int         enum_index;\n");
    fprintf(g_out,"} ufsrv_config_dispatch[] = {\n");
    qsort(gen_fields, gen_field_count, sizeof(GenField), sCompareDispatch);
    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        const char *stem = (d->domain == UFSRV_CONFIG_DOMAIN_CONFIG)
                           ? "_CONFIG_FIELD_" : "_RUNTIME_FIELD_";
        fprintf(g_out,INDENT2 "{ \"%s\", UFSRV%s%s%s },\n", d->name,
               sPrefixUpper(), stem, sUppercaseName(d->name));
    }
    fprintf(g_out,INDENT2 "{ NULL, -1 }\n");
    fprintf(g_out,"};\n");
    fprintf(g_out,"#endif\n");
}

// ═════════════════════════════════════════════════════════════════════════════
// Decomposed output — one file per logical artefact
// ═════════════════════════════════════════════════════════════════════════════

/* Open a file in g_output_dir for writing.  Creates the directory if
 * it does not exist.  When g_prefix is set, the prefix is infixed into
 * the filename (e.g. "ufsrv_config_generated.h" → "ufsrv_config_adjunct_generated.h").
 * Exits on failure. */
static FILE *
sOpenOutput(const char *filename)
{
    char path[1024];
    int n;

    /* Ensure output directory exists */
    if (mkdir(g_output_dir, 0755) != 0 && errno != EEXIST) {
        perror("ufsrv_config_generator: mkdir");
        exit(1);
    }

    /* Infix prefix before the "_generated" stem (e.g. "_generated.h" or
     * "_generated.c"), falling back to before the last dot. */
    if (g_prefix) {
        const char *stem = strstr(filename, "_generated.");
        if (stem) {
            char base[512];
            size_t baselen = (size_t)(stem - filename);
            if (baselen >= sizeof(base)) baselen = sizeof(base) - 1;
            memcpy(base, filename, baselen);
            base[baselen] = '\0';
            n = snprintf(path, sizeof(path), "%s/%s_%s%s",
                         g_output_dir, base, g_prefix, stem);
        } else {
            const char *dot = strrchr(filename, '.');
            if (dot) {
                char base[512];
                size_t blen = (size_t)(dot - filename);
                if (blen >= sizeof(base)) blen = sizeof(base) - 1;
                memcpy(base, filename, blen);
                base[blen] = '\0';
                n = snprintf(path, sizeof(path), "%s/%s_%s%s",
                             g_output_dir, base, g_prefix, dot);
            } else {
                n = snprintf(path, sizeof(path), "%s/%s_%s",
                             g_output_dir, filename, g_prefix);
            }
        }
    } else {
        n = snprintf(path, sizeof(path), "%s/%s", g_output_dir, filename);
    }
    if (n < 0 || (size_t)n >= sizeof(path)) {
        fprintf(stderr, "ufsrv_config_generator: output path too long: %s/%s\n",
                g_output_dir, filename);
        exit(1);
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("ufsrv_config_generator: fopen");
        exit(1);
    }
    return f;
}

/* Emit the standard header boilerplate that every generated header needs. */
static void
sEmitDecomposedHeaderPreamble(FILE *f, const char *guard)
{
    fprintf(f, "/*\n");
    fprintf(f, " * %s — GENERATED BY ufsrv_config_generator, DO NOT EDIT\n",
            guard);
    fprintf(f, " *\n");
    sEmitTimestamp(f);
    fprintf(f, " *   Config fields:   %d\n", config_field_count);
    fprintf(f, " *   Runtime fields:  %d\n", runtime_field_count);
    fprintf(f, " */\n\n");
    fprintf(f, "#ifndef %s\n", guard);
    fprintf(f, "#define %s\n\n", guard);
    fprintf(f, "#include <sys/types.h>          /* pid_t */\n");
    fprintf(f, "#include <stdint.h>             /* uint8_t */\n");
    fprintf(f, "#include <uflib/standard_defs.h>\n");
    sEmitLocalInclude(f, "ufsrvcorelib_defs.h");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/ufsrv_config_types.h");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/ufsrv_config_value_type.h");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/ufsrv_config_field_descriptor.h");
    sEmitLocalInclude(f, "ufsrv_core/configfile_processor/configfile_processor_symbol_table_type.h");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/ufsrv_config_store_operating_state_enum.h");
}

static void
sEmitDecomposedHeaderPreambleLight(FILE *f, const char *guard)
{
    fprintf(f, "/*\n");
    fprintf(f, " * %s — GENERATED BY ufsrv_config_generator, DO NOT EDIT\n",
            guard);
    fprintf(f, " *\n");
    sEmitTimestamp(f);
    fprintf(f, " */\n\n");
    fprintf(f, "#ifndef %s\n", guard);
    fprintf(f, "#define %s\n\n", guard);
}

static void sEmitDecomposedFooter(FILE *f, const char *guard)
{
    fprintf(f, "\n#endif /* %s */\n", guard);
}

/* ── Header files ─────────────────────────────────────────────────────── */

static void sGenerateHeaderFieldEnums(void)
{
    FILE *f = sOpenOutput("ufsrv_config_field_enums_generated.h");
    sEmitDecomposedHeaderPreambleLight(f,
        "UFSRV_CORE_UFSRV_CONFIG_FIELD_ENUMS_GENERATED_H");
    g_out = f;
    sEmitConfigEnum();
    sEmitRuntimeEnum();
    g_out = f;
    sEmitDecomposedFooter(f, "UFSRV_CORE_UFSRV_CONFIG_FIELD_ENUMS_GENERATED_H");
    fclose(f);
}

// AsString marshaller declarations (header)

static void sEmitAsStringDecls(void)
{
    int i;
    fprintf(g_out,"\n// AsString marshallers — declarations\n");
    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        fprintf(g_out,"PUBLIC_API BufferDescriptor *\nUfsrvConfig");
        sEmitAccessorName(d->name);
        fprintf(g_out,"AsString(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided);\n");
    }
}

// AsString marshaller definitions (source) — trailing section

static void sEmitAsStringDefs(void)
{
    int i;
    fprintf(g_out,"\n// ── AsString marshallers — accessor per field ─────────────────────────\n");
    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        const char *generic = sGenericAsStringFn(d->c_lang_type);
        fprintf(g_out,"\nBufferDescriptor *\nUfsrvConfig");
        sEmitAccessorName(d->name);
        fprintf(g_out,"AsString(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)\n{\n");
        if (generic) {
            fprintf(g_out,INDENT2 "return %s(field_index, value_ptr, buffer_provided);\n", generic);
        } else {
            fprintf(g_out,INDENT2 "return UfsrvConfig");
            sEmitAccessorName(d->name);
            fprintf(g_out,"AsString_%s(field_index, value_ptr, buffer_provided);\n",
                    sAsStringTypeSuffix(d->c_lang_type));
        }
        fprintf(g_out,"}\n");
    }
}

// ── One-off AsString generation ──────────────────────────────────────────
// Emitted only under --emit-one-off-as-string: the hand-maintained generic marshallers
// (generic_callbacks.c / _priv.h) and the per-field non-standard stubs
// (stubs_as_string_generated.c / _priv_generated.h).  The consumer takes over
// maintenance of these files afterwards.

/* Build the path to a file under src/ufsrv_core/ufsrv_config/. */
static const char *
sRepoConfigPath(const char *name)
{
    static char path[1024];
    int n = snprintf(path, sizeof(path), "%s/src/ufsrv_core/ufsrv_config/%s",
                     g_repo_dir ? g_repo_dir : ".", name);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        fprintf(stderr, "ufsrv_config_generator: path too long for %s\n", name);
        exit(1);
    }
    return path;
}

/* Open a file under src/ufsrv_core/ufsrv_config/. */
static FILE *
sOpenRepoConfig(const char *name, const char *mode)
{
    const char *path = sRepoConfigPath(name);
    FILE *f = fopen(path, mode);
    if (!f) { perror(path); exit(1); }
    return f;
}

/* Append `content` to `name` unless the file already contains `marker`. */
static void
sAppendIfAbsent(const char *name, const char *marker, const char *content)
{
    const char *path = sRepoConfigPath(name);
    FILE *f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = malloc((size_t)sz + 1);
        if (buf) {
            size_t n = fread(buf, 1, (size_t)sz, f);
            buf[n] = '\0';
            int found = (strstr(buf, marker) != NULL);
            free(buf);
            fclose(f);
            if (found) return;   /* already generated */
        } else {
            fclose(f);
        }
    }
    f = fopen(path, "a");
    if (!f) { perror(path); exit(1); }
    fputs(content, f);
    fclose(f);
}

/* Insert `content` before the final #endif of `name` (unless already present). */
static void
sInsertBeforeEndif(const char *name, const char *marker, const char *content)
{
    const char *path = sRepoConfigPath(name);
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); exit(1); }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);

    if (strstr(buf, marker)) { free(buf); return; }

    char *endif = NULL, *p = buf;
    while ((p = strstr(p, "#endif")) != NULL) { endif = p; p += 6; }
    if (!endif) { free(buf); return; }

    f = fopen(path, "w");
    if (!f) { perror(path); free(buf); exit(1); }
    fwrite(buf, 1, (size_t)(endif - buf), f);
    fputs(content, f);
    fputs(endif, f);
    fclose(f);
    free(buf);
}

/* Build the generic AsString declarations (decls=1) or definitions (decls=0). */
static void
sBuildGenericAsStringContent(char *buf, size_t bufsz, int decls)
{
    size_t off = 0;
    int i;
    if (decls)
        off += (size_t)snprintf(buf + off, bufsz - off,
                    "\n/* ── AsString marshallers — standard scalar c_lang_types */\n");
    for (i = 0; i < (int)g_as_string_generic_type_count; i++) {
        const char *fn = g_as_string_generic_types[i].as_string_fn;
        if (decls)
            off += (size_t)snprintf(buf + off, bufsz - off,
                        "PUBLIC_API BufferDescriptor * %s(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided);\n", fn);
        else
            off += (size_t)snprintf(buf + off, bufsz - off,
                        "\nPUBLIC_API BufferDescriptor *\n%s(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)\n{\n"
                        "  (void)field_index;\n  (void)value_ptr;\n  (void)buffer_provided;\n  return NULL;\n}\n", fn);
    }
}

static void
sGenerateAsStringOneOff(void)
{
    int i;
    char buf[8192];

    /* Generic marshallers — declarations into the hand-maintained priv header. */
    sBuildGenericAsStringContent(buf, sizeof(buf), 1);
    sInsertBeforeEndif("ufsrv_config_generic_callbacks_priv.h", "GenericIntAsString", buf);

    /* Generic marshallers — definitions into the hand-maintained .c. */
    sBuildGenericAsStringContent(buf, sizeof(buf), 0);
    sAppendIfAbsent("ufsrv_config_generic_callbacks.c", "GenericIntAsString", buf);

    /* Stub marshallers — declarations into a new priv header. */
    FILE *sd = sOpenRepoConfig("ufsrv_config_stubs_as_string_priv_generated.h", "w");
    fprintf(sd, "/*\n * ufsrv_config_stubs_as_string_priv_generated.h — GENERATED (one-off),\n"
                " * DO NOT EDIT.  Rename (drop _generated) once the stubs are hand-maintained.\n */\n");
    fprintf(sd, "#ifndef UFSRV_CONFIG_STUBS_AS_STRING_PRIV_GENERATED_H\n"
                "#define UFSRV_CONFIG_STUBS_AS_STRING_PRIV_GENERATED_H\n\n");
    fprintf(sd, "#include <ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_value_type.h>\n"
                "#include <uflib/main_types.h>\n\n");
    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        if (sGenericAsStringFn(d->c_lang_type)) continue;
        fprintf(sd, "extern BufferDescriptor * UfsrvConfig%sAsString_%s(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided);\n",
                sAccessorNameStr(d->name), sAsStringTypeSuffix(d->c_lang_type));
    }
    fprintf(sd, "\n#endif /* UFSRV_CONFIG_STUBS_AS_STRING_PRIV_GENERATED_H */\n");
    fclose(sd);

    /* Stub marshallers — definitions into a new .c. */
    FILE *sc = sOpenRepoConfig("ufsrv_config_stubs_as_string_generated.c", "w");
    fprintf(sc, "/*\n * ufsrv_config_stubs_as_string_generated.c — GENERATED (one-off), DO NOT\n"
                " * EDIT.  Rename (drop _generated) once the stubs are hand-maintained.\n */\n");
    fprintf(sc, "#include \"ufsrv_config_stubs_as_string_priv_generated.h\"\n\n");
    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        if (sGenericAsStringFn(d->c_lang_type)) continue;
        fprintf(sc, "BufferDescriptor *\nUfsrvConfig%sAsString_%s(int field_index, UfsrvConfigValue *value_ptr, BufferDescriptor *buffer_provided)\n{\n",
                sAccessorNameStr(d->name), sAsStringTypeSuffix(d->c_lang_type));
        fprintf(sc, "  /* TODO: hand-author string formatting for %s. */\n", d->c_lang_type);
        fprintf(sc, "  (void)field_index;\n  (void)value_ptr;\n  (void)buffer_provided;\n  return NULL;\n}\n\n");
    }
    fclose(sc);

    fprintf(stderr, "ufsrv_config_generator: wrote one-off AsString files (generic callbacks + stubs)\n");
}

// ── One-off accessor-def stub generation ────────────────────────────────
// Emitted only under --emit-one-off-as_getters_setters: skeletal definitions
// for the accessors the generator declared `extern` (i.e. catalogue entries
// with is_keep_custom_getter / is_keep_custom_setter set).  The consumer
// hand-authors the real bodies afterwards.

/* Zero/safe return value for a custom-getter stub body. */
static const char *
sGetterStubReturn(const UfsrvConfigFieldDescriptor *d)
{
    switch (d->value_type) {
    case UFSRV_CONFIG_VALUE_STR:
    case UFSRV_CONFIG_VALUE_PTR:
    case UFSRV_CONFIG_VALUE_CONST_PTR:
    case UFSRV_CONFIG_VALUE_ARRAY:
        return "NULL";
    case UFSRV_CONFIG_VALUE_STRUCT: {
        static char buf[256];
        snprintf(buf, sizeof(buf), "(%s){0}", d->c_lang_type);
        return buf;
    }
    default:   /* INT, ENUM, BOOL, DOUBLE */
        return "0";
    }
}

static void
sGenerateGettersSettersOneOff(void)
{
    int i;
    int custom_count = 0;

    /* Nothing to emit when the catalogue declares no custom getter/setter. */
    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;
        if (d->is_keep_custom_getter ||
            (d->is_keep_custom_setter && d->value_type != UFSRV_CONFIG_VALUE_CONST_PTR))
            custom_count++;
    }
    if (custom_count == 0) {
        fprintf(stderr, "ufsrv_config_generator: no is_keep_custom_getter/is_keep_custom_setter "
                        "fields in catalogue — not generating accessor-def stub\n");
        return;
    }

    FILE *f = sOpenRepoConfig("ufsrv_config_stubs_accessor_defs_generated.c", "w");
    fprintf(f, "/*\n * ufsrv_config_stubs_accessor_defs_generated.c — GENERATED (one-off), DO NOT\n"
               " * EDIT.  Rename (drop _generated) once the stubs are hand-maintained.\n */\n");
    fprintf(f, "#include <ufsrvcorelib/ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_decls_generated.h>\n\n");

    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;

        /* Custom getter — definition hand-authored by the consumer. */
        if (d->is_keep_custom_getter) {
            fprintf(f, "%s\nUfsrvConfig%s(void)\n{\n", d->c_lang_type, sAccessorNameStr(d->name));
            fprintf(f, "  /* TODO: hand-author custom getter for %s. */\n", d->name);
            fprintf(f, "  return %s;\n", sGetterStubReturn(d));
            fprintf(f, "}\n\n");
        }

        /* Custom setter — definition hand-authored by the consumer. */
        if (d->is_keep_custom_setter && d->value_type != UFSRV_CONFIG_VALUE_CONST_PTR) {
            fprintf(f, "void\nUfsrvConfigSet%s(UfsrvConfigItem *item, UfsrvConfigSource config_source)\n{\n",
                    sAccessorNameStr(d->name));
            fprintf(f, "  /* TODO: hand-author custom setter for %s. */\n", d->name);
            fprintf(f, "  (void)item;\n  (void)config_source;\n");
            fprintf(f, "}\n\n");
        }

        /* Custom AsPtr getter — definition hand-authored by the consumer. */
        if (d->is_as_ptr_getter && d->is_keep_custom_getter) {
            fprintf(f, "%s *\nUfsrvConfig%sAsPtr(void)\n{\n", d->c_lang_type, sAccessorNameStr(d->name));
            fprintf(f, "  /* TODO: hand-author custom AsPtr getter for %s. */\n", d->name);
            fprintf(f, "  return NULL;\n");
            fprintf(f, "}\n\n");
        }
    }

    fclose(f);
    fprintf(stderr, "ufsrv_config_generator: wrote one-off accessor-def stubs\n");
}

static void sGenerateHeaderAccessorDecls(void)
{
    FILE *f = sOpenOutput("ufsrv_config_accessor_decls_generated.h");
    sEmitDecomposedHeaderPreambleLight(f,
        "UFSRV_CORE_UFSRV_CONFIG_ACCESSOR_DECLS_GENERATED_H");
    /* Emit only the includes this file actually needs — the full
     * preamble drags in field_descriptor.h and symbol_table_type.h
     * which are unnecessary for accessor declarations. */
    fprintf(f, "#include <sys/types.h>          /* pid_t */\n");
    fprintf(f, "#include <stdint.h>             /* uint8_t */\n");
    fprintf(f, "#include <uflib/standard_defs.h>\n");
    sEmitLocalInclude(f, "ufsrvcorelib_defs.h");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/ufsrv_config_types.h");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/ufsrv_config_value_type.h");
    fprintf(f, "#include <uflib/main_types.h>    /* BufferDescriptor */\n");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/ufsrv_config_store_operating_state_enum.h");
    g_out = f;
    sEmitConfigAccessorDecls();
    sEmitRuntimeAccessorDecls();
    sEmitAsStringDecls();
    g_out = f;
    sEmitDecomposedFooter(f, "UFSRV_CORE_UFSRV_CONFIG_ACCESSOR_DECLS_GENERATED_H");
    fclose(f);
}

static void sGenerateHeaderDispatchTable(void)
{
    FILE *f = sOpenOutput("data_ufsrv_config_dispatch_table_priv_generated.h");
    sEmitDecomposedHeaderPreambleLight(f,
        "DATA_UFSRV_CONFIG_DISPATCH_TABLE_PRIV_GENERATED_H");
    g_out = f;
    sEmitDispatchTable();
    g_out = f;
    sEmitDecomposedFooter(f, "DATA_UFSRV_CONFIG_DISPATCH_TABLE_PRIV_GENERATED_H");
    fclose(f);
}

static void sGenerateHeaderDecomposed(void)
{
    sGenerateHeaderFieldEnums();
    sGenerateHeaderAccessorDecls();
    sGenerateHeaderDispatchTable();
    if (g_prefix)
        sEmitManifest();
    fprintf(stderr, "ufsrv_config_generator: wrote 3 header files to %s\n",
            g_output_dir);
}

static void sGenerateSourceFieldTables(void)
{
    FILE *f = sOpenOutput("data_ufsrv_config_field_tables_generated.c");
    g_out = f;
    sEmitSourcePreamble();
    /* Pull in the generic callback declarations so field table
     * initialisers can reference them.  The callbacks have external
     * linkage (compiled as a standalone TU) — this priv header
     * provides the declarations needed at compile time.
     * Quoted include resolves relative to the includer's directory
     * (both files are co-located in src/ufsrv_core/ufsrv_config/). */
    fprintf(f, "#include \"ufsrv_config_generic_callbacks_priv.h\"\n\n");
    sEmitConfigFieldTable();
    sEmitRuntimeFieldTable();
    sEmitConfigFieldDefaultsTable();
    sEmitRuntimeFieldDefaultsTable();
    fclose(f);
}

static void sGenerateSourceAccessorDefs(void)
{
    FILE *f = sOpenOutput("ufsrv_config_accessor_defs_generated.c");
    g_out = f;
    sEmitSourcePreamble();
    /* Generic + stub AsString marshallers are referenced by the AsString
     * accessor bodies emitted below. */
    fprintf(f, "#include \"ufsrv_config_generic_callbacks_priv.h\"\n");
    fprintf(f, "#include \"ufsrv_config_stubs_as_string_priv.h\"\n\n");
    sEmitConfigAccessorDefs();
    sEmitRuntimeAccessorDefs();
    sEmitAsStringDefs();
    fclose(f);
}

static void sGenerateSourceValidate(void)
{
    FILE *f = sOpenOutput("ufsrv_config_validate_generated.c");
    g_out = f;
    sEmitSourcePreamble();
    sEmitValidationDef();
    fclose(f);
}

static void sGenerateSourceMasterInclude(void)
{
    FILE *f = sOpenOutput("ufsrv_config_getters_setters_generated.c");
    fprintf(f, "/*\n");
    fprintf(f, " * ufsrv_config_getters_setters_generated.c — GENERATED, DO NOT EDIT\n");
    fprintf(f, " *\n");
    sEmitTimestamp(f);
    fprintf(f, " * Master include — pulls in all decomposed generated source files.\n");
    fprintf(f, " */\n\n");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/generated/data_ufsrv_config_field_tables_generated.c");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/generated/ufsrv_config_accessor_defs_generated.c");
    sEmitLocalInclude(f, "ufsrv_core/ufsrv_config/generated/ufsrv_config_validate_generated.c");
    fclose(f);
}

static void sGenerateSourceDecomposed(void)
{
    sGenerateSourceFieldTables();
    sGenerateSourceAccessorDefs();
    sGenerateSourceValidate();
    sGenerateDataLiteralToFieldMap();
    sGenerateSourceMasterInclude();
    fprintf(stderr, "ufsrv_config_generator: wrote 5 source files to %s\n",
            g_output_dir);
}


/* Emit a manifest header with enum counts.  Consumed by the leaf-server
 * composer to compute unified indices without reading full generated output. */
static void sEmitManifest(void)
{
    FILE *f;
    char path[1024];
    snprintf(path, sizeof(path), "%s/ufsrv_config%s_manifest.h",
             g_output_dir, sPrefixFile());
    f = fopen(path, "w");
    if (!f) { perror("ufsrv_config_generator: fopen manifest"); exit(1); }
    fprintf(f, "/* ufsrv_config%s_manifest.h — GENERATED, DO NOT EDIT */\n", sPrefixFile());
    sEmitTimestamp(f);
    fprintf(f, "#define UFSRV%s_CONFIG_FIELD_COUNT  %d\n", sPrefixUpper(), config_field_count);
    fprintf(f, "#define UFSRV%s_RUNTIME_FIELD_COUNT %d\n", sPrefixUpper(), runtime_field_count);
    fclose(f);
}

/* ── Literal reverse-map: parse CONF_FILE_LITERAL_* defines ─────────── */

static void
sBuildLiteralReverseMap(void)
{
    char path[1024];
    int n;
    if (g_literals_header) {
        n = snprintf(path, sizeof(path), "%s", g_literals_header);
    } else {
        n = snprintf(path, sizeof(path),
                     "%s/include/ufsrvcorelib/ufsrv_core/configfile_processor/"
                     "ufsrv_config_literals_defs.h",
                     g_repo_dir ? g_repo_dir : ".");
    }
    if (n < 0 || (size_t)n >= sizeof(path)) {
        fprintf(stderr, "ufsrv_config_generator: ERROR: "
                "path too long for literals header\n");
        exit(1);
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ufsrv_config_generator: ERROR: "
                "cannot open ufsrv_config_literals_defs.h at %s\n", path);
        exit(1);
    }

    char line[512];
    int  capacity = 0;

    while (fgets(line, (int)sizeof(line), f)) {
        char macro[128];
        char value[256];

        /* Match: #define CONF_FILE_LITERAL_XXX "yyy" */
        if (sscanf(line, " #define %127s \"%255[^\"]\"", macro, value) == 2) {
            /* Only accept CONF_FILE_LITERAL_ macros */
            if (strncmp(macro, "CONF_FILE_LITERAL_", 18) != 0)
                continue;

            if (g_literal_map_count >= capacity) {
                capacity = (capacity == 0) ? 64 : capacity * 2;
                g_literal_map = (LiteralMapEntry *)realloc(
                    g_literal_map, (size_t)capacity * sizeof(LiteralMapEntry));
                if (!g_literal_map) {
                    fprintf(stderr, "ufsrv_config_generator: ERROR: "
                            "out of memory building literal map\n");
                    exit(1);
                }
            }

            snprintf(g_literal_map[g_literal_map_count].macro_name,
                     sizeof(g_literal_map[0].macro_name), "%s", macro);
            snprintf(g_literal_map[g_literal_map_count].string_value,
                     sizeof(g_literal_map[0].string_value), "%s", value);
            g_literal_map_count++;
        }
    }

    fclose(f);

    if (g_literal_map_count == 0) {
        fprintf(stderr, "ufsrv_config_generator: ERROR: "
                "no CONF_FILE_LITERAL_* entries found in literals header\n");
        exit(1);
    }

    fprintf(stderr, "ufsrv_config_generator: built literal reverse-map — "
            "%d entries\n", g_literal_map_count);
}

static const char *
sLookupMacroName(const char *string_value)
{
    int i;
    for (i = 0; i < g_literal_map_count; i++) {
        if (strcmp(g_literal_map[i].string_value, string_value) == 0)
            return g_literal_map[i].macro_name;
    }
    fprintf(stderr, "ufsrv_config_generator: ERROR: "
            "no CONF_FILE_LITERAL_* macro found for string \"%s\"\n",
            string_value);
    exit(1);
}

/* ── Key-map emission ───────────────────────────────────────────────── */

typedef struct {
    const char *macro_name;       /* "CONF_FILE_LITERAL_XXX" */
    const char *enum_name;        /* "UFSRV_CONFIG_FIELD_XXX" */
    const char *sort_key;         /* string value for strcmp sorting */
    int         config_index;
} KeyMapEntry;

static int
sEmitConfigKeyMapEntryCompare(const void *a, const void *b)
{
    const KeyMapEntry *ka = (const KeyMapEntry *)a;
    const KeyMapEntry *kb = (const KeyMapEntry *)b;
    return strcmp(ka->sort_key, kb->sort_key);
}

static void
sEmitConfLiteralToConfFieldMap(FILE *f)
{
    int i, map_count = 0;
    KeyMapEntry *entries = NULL;

    /* Collect CONFIG-domain fields that carry a config_file_key */
    for (i = 0; i < gen_field_count; i++) {
        const UfsrvConfigFieldDescriptor *d = gen_fields[i].desc;

        if (d->domain != UFSRV_CONFIG_DOMAIN_CONFIG)
            continue;
        if (!d->config_file_key || !d->config_file_key[0])
            continue;

        entries = (KeyMapEntry *)realloc(entries,
            (size_t)(map_count + 1) * sizeof(KeyMapEntry));
        if (!entries) {
            fprintf(stderr, "ufsrv_config_generator: ERROR: "
                    "out of memory building key map\n");
            exit(1);
        }

        entries[map_count].macro_name   = sLookupMacroName(d->config_file_key);
        entries[map_count].enum_name    = NULL;  /* filled below */
        entries[map_count].sort_key     = d->config_file_key;
        entries[map_count].config_index = gen_fields[i].config_index;
        map_count++;
    }

    /* Build enum constant name strings for emission */
    for (i = 0; i < map_count; i++) {
        char buf[256];
        /* Find the field descriptor for this config_index to get its name */
        int j;
        const char *field_name = NULL;
        for (j = 0; j < gen_field_count; j++) {
            if (gen_fields[j].config_index == entries[i].config_index) {
                field_name = gen_fields[j].desc->name;
                break;
            }
        }
        snprintf(buf, sizeof(buf), "UFSRV%s_CONFIG_FIELD_%s",
                 sPrefixUpper(), sUppercaseName(field_name));
        entries[i].enum_name = strdup(buf);
        if (!entries[i].enum_name) {
            fprintf(stderr, "ufsrv_config_generator: ERROR: "
                    "out of memory building key map enum names\n");
            exit(1);
        }
    }

    /* Sort by the config-file-key string value (strcmp order) */
    qsort(entries, (size_t)map_count, sizeof(KeyMapEntry),
          sEmitConfigKeyMapEntryCompare);

    /* Emit the COUNT and the array */
    fprintf(f, "\n#define UFSRV_CONFIG_KEY_MAP_COUNT  %d\n\n", map_count);
    fprintf(f, "static const UfsrvConfigKeyMapEntry\n");
    fprintf(f, "ufsrv_config_key_map[UFSRV_CONFIG_KEY_MAP_COUNT] = {\n");

    for (i = 0; i < map_count; i++) {
        fprintf(f, "    { %s, %s }", entries[i].macro_name, entries[i].enum_name);
        if (i < map_count - 1)
            fprintf(f, ",");
        fprintf(f, "\n");
        free((void *)entries[i].enum_name);
    }

    fprintf(f, "};\n");
    free(entries);

    fprintf(stderr, "ufsrv_config_generator: key map — %d entries (strcmp-sorted)\n",
            map_count);
}

static void
sGenerateDataLiteralToFieldMap(void)
{
    FILE *f = sOpenOutput("data_ufsrv_config_literal_to_field_map_priv_generated.h");

    fprintf(f, "/*\n");
    fprintf(f, " * data_ufsrv_config_literal_to_field_map_priv_generated.h\n");
    fprintf(f, " * — GENERATED BY ufsrv_config_generator, DO NOT EDIT\n");
    fprintf(f, " *\n");
    sEmitTimestamp(f);
    fprintf(f, " *   Key-map entries:  (see UFSRV_CONFIG_KEY_MAP_COUNT below)\n");
    fprintf(f, " */\n\n");
    fprintf(f, "#ifndef UFSRV_CORE_UFSRV_CONFIG_DATA_LITERAL_TO_FIELD_MAP_PRIV_GENERATED_H\n");
    fprintf(f, "#define UFSRV_CORE_UFSRV_CONFIG_DATA_LITERAL_TO_FIELD_MAP_PRIV_GENERATED_H\n\n");

    sEmitConfLiteralToConfFieldMap(f);

    fprintf(f, "\n#endif /* UFSRV_CORE_UFSRV_CONFIG_DATA_LITERAL_TO_FIELD_MAP_PRIV_GENERATED_H */\n");
    fclose(f);
}

/* Print full usage to stdout.  Help is the requested output, not an
 * error path, so it goes to stdout (stderr is reserved for diagnostics
 * and the generated artefacts use stdout in legacy mode). */
static void
sPrintHelp(void)
{
    printf("ufsrv_config_generator — build-time code generator for the ufsrv_config module\n");
    printf("\n");
    printf("Usage:\n");
    printf("  ufsrv_config_generator [options]\n");
    printf("\n");
    printf("Output mode (mutually exclusive; --header is the default):\n");
    printf("  --header                 Emit declarations (enums, accessor decls, dispatch table)\n");
    printf("  --source                 Emit definitions (accessor defs, field tables, validate)\n");
    printf("\n");
    printf("General options:\n");
    printf("  --output-dir=<path>      Decomposed mode: write one file per logical artefact\n");
    printf("                           under <path> instead of a monolithic stdout dump\n");
    printf("  --include-prefix=<pfx>   Emit local includes as fully-qualified angle-bracket\n");
    printf("                           form, e.g. \"#include <ufsrvcorelib/...>\"\n");
    printf("  --prefix=<pfx>           Infix <pfx> into all generated identifiers and filename\n");
    printf("                           stems, e.g. --prefix=adjunct\n");
    printf("  --literals-header=<path> Explicit path to ufsrv_config_literals_defs.h (used to\n");
    printf("                           build the CONF_FILE_LITERAL_* reverse-map)\n");
    printf("  --repo-dir=<path>        Repo root for resolving source assets (VERSION files,\n");
    printf("                           one-off AsString output paths)\n");
    printf("  --emit-one-off-as-string Regenerate the hand-maintained AsString files\n");
    printf("                           (generic marshallers + per-field stubs)\n");
    printf("  --emit-one-off-as_getters_setters\n");
    printf("                           Regenerate skeletal accessor-def stubs for the\n");
    printf("                           custom getters/setters (is_keep_custom_getter/\n");
    printf("                           is_keep_custom_setter)\n");
    printf("  --version, -V            Show version and provenance info and exit\n");
    printf("  --help, -h               Show this help and exit\n");
}

/* Read version provenance from the committed VERSION / .version files into
 * g_version / g_gen_sha / g_catalogue_sha.  Paths resolve through g_repo_dir —
 * the generator's CWD is the build directory, not the repo root (see D01 in
 * UFSRV_CONFIG_GENERATOR_ARTEFACTS_CUSTOM_VERSION_MANAGEMENT_DESIGN_COMPLETED.md).
 * Returns 0 on success, 1 on path overflow.  Missing files are tolerated by
 * the readers (fallback 0 / "unknown") — provenance is metadata and must not
 * break the build. */
static int
sReadVersionProvenance(void)
{
    char vpath[1024];
    int n;

    n = snprintf(vpath, sizeof(vpath),
                 "%s/dev/scripts/ufsrv_config_generator/VERSION",
                 g_repo_dir ? g_repo_dir : ".");
    if (n < 0 || (size_t)n >= sizeof(vpath)) {
        fprintf(stderr, "ufsrv_config_generator: ERROR: path too long for VERSION\n");
        return 1;
    }
    g_version = sReadVersionInt(vpath);

    n = snprintf(vpath, sizeof(vpath),
                 "%s/dev/scripts/ufsrv_config_generator/ufsrv_config_generator.c.version",
                 g_repo_dir ? g_repo_dir : ".");
    if (n < 0 || (size_t)n >= sizeof(vpath)) {
        fprintf(stderr, "ufsrv_config_generator: ERROR: path too long for generator.c.version\n");
        return 1;
    }
    sReadLine(vpath, g_gen_sha, sizeof(g_gen_sha));

    n = snprintf(vpath, sizeof(vpath),
                 "%s/dev/scripts/ufsrv_config_generator/ufsrv_config_generator_fields.c.input.version",
                 g_repo_dir ? g_repo_dir : ".");
    if (n < 0 || (size_t)n >= sizeof(vpath)) {
        fprintf(stderr, "ufsrv_config_generator: ERROR: path too long for catalogue.c.input.version\n");
        return 1;
    }
    sReadLine(vpath, g_catalogue_sha, sizeof(g_catalogue_sha));

    return 0;
}

// Main

int
main(int argc, char **argv)
{
    int i;

    g_out = stdout;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            sPrintHelp();
            return 0;
        } else if (strcmp(argv[i], "--source") == 0)
            g_source_mode = 1;
        else if (strcmp(argv[i], "--header") == 0)
            g_source_mode = 0;
        else if (strncmp(argv[i], "--output-dir=", 13) == 0) {
            g_output_dir = argv[i] + 13;
        } else if (strncmp(argv[i], "--include-prefix=", 17) == 0) {
            g_include_prefix = argv[i] + 17;
        } else if (strncmp(argv[i], "--prefix=", 9) == 0) {
            g_prefix = argv[i] + 9;
        } else if (strncmp(argv[i], "--literals-header=", 18) == 0) {
            g_literals_header = argv[i] + 18;
        } else if (strncmp(argv[i], "--repo-dir=", 11) == 0) {
            g_repo_dir = argv[i] + 11;
        } else if (strcmp(argv[i], "--emit-one-off-as-string") == 0) {
            g_one_off = 1;
        } else if (strcmp(argv[i], "--emit-one-off-as_getters_setters") == 0) {
            g_one_off_gs = 1;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            g_show_version = 1;
        } else {
            fprintf(stderr, "ufsrv_config_generator: unknown option: %s\n", argv[i]);
            fprintf(stderr, "usage: ufsrv_config_generator [--header|--source] [--output-dir=<path>]\n");
            fprintf(stderr, "       run with --help for full usage\n");
            return 1;
        }
    }

    if (g_show_version) {
        if (sReadVersionProvenance() != 0)
            return 1;
        printf("ufsrv_config_generator version %d\n", g_version);
        printf("  generator: %s\n", g_gen_sha);
        printf("  catalogue: %s\n", g_catalogue_sha);
        return 0;
    }

    sBuildLiteralReverseMap();
    sScanCatalogue(core_fields, core_field_count, "core");
    sValidateNoDuplicates();
    sValidateFields();

    if (sReadVersionProvenance() != 0)
        return 1;

    fprintf(stderr, "ufsrv_config_generator: %d config + %d runtime fields"
            " from %d descriptors\n",
            config_field_count, runtime_field_count, gen_field_count);

    if (g_output_dir) {
        /* Decomposed mode — one file per logical artefact */
        if (g_source_mode) {
            sGenerateSourceDecomposed();
        } else {
            sGenerateHeaderDecomposed();
        }
    } else if (g_source_mode) {
        /* Legacy stdout mode — monolithic source */
        sEmitSourcePreamble();
        sEmitConfigFieldTable();
        sEmitRuntimeFieldTable();
        sEmitConfigFieldDefaultsTable();
        sEmitRuntimeFieldDefaultsTable();
        sEmitConfigAccessorDefs();
        sEmitValidationDef();
        fprintf(stderr, "ufsrv_config_generator: wrote source"
                " (%d config accessors, 2 field tables, validate)\n",
                config_field_count);
    } else {
        /* Legacy stdout mode — monolithic header */
        sEmitHeader();
        sEmitConfigEnum();
        sEmitRuntimeEnum();
        sEmitConfigAccessorDecls();
        sEmitRuntimeAccessorDecls();
        sEmitDispatchTable();
        sEmitFooter();
        fprintf(stderr, "ufsrv_config_generator: wrote header "
                "(%d config + %d runtime enums, %d declarations, dispatch table)\n",
                config_field_count, runtime_field_count,
                config_field_count * 2 + runtime_field_count * 2 + 1);
    }

    if (g_one_off)
        sGenerateAsStringOneOff();
    if (g_one_off_gs)
        sGenerateGettersSettersOneOff();

    return 0;
}
