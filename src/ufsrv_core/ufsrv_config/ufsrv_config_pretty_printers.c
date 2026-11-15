/**
 * @file ufsrv_config_pretty_printers.c
 * @brief ufsrv_config_pretty_printers
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

/*
 * Standard pretty-printers for UfsrvConfigValue
 *
 * Contract:
 *   char *(*on_pretty_print)(int field_index,
 *                            UfsrvConfigValue *value_ptr,
 *                            UfsrvConfigFieldDescriptorForGenerator *gen,
 *                            enum PrettyPrintFormat fmt,
 *                            enum PrettyPrintScope scope,
 *                            BufferDescriptor *buffer_provided);
 *
 * buffer_provided == NULL
 *   → allocate one slab: [BufferDescriptor | payload bytes | '\0']
 *   → return pointer to payload (bd->data)
 *   → free with PrettyPrint_Release(returned_ptr)
 *
 * buffer_provided != NULL
 *   → write at most size_max-1 bytes into buffer_provided->data
 *   → set buffer_provided->size
 *   → return buffer_provided->data (or NULL on failure)
 *
 * All scalar values are rendered as strings.  Printer names follow
 * c_lang_type with "_pointer" suffix when the storage is a pointer.
 *
 *
 * {
    "name": "server_id",
    "value": "3",
    "source": "config_file",
    "domain": "config",
    "c_lang_type": "int",
    "config_field_default": "0",
    "field_index": 1
  }
 */


#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config_pretty_printers.h"
#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include "ufsrv_config_pretty_printers_priv.h"
#include "ufsrvcorelib/ufsrv_core/ufsrv_config/ufsrv_config.h"

/* -------------------------------------------------------------------------- */
/* Buffer helpers                                                             */
/* -------------------------------------------------------------------------- */

/** Free a buffer returned when buffer_provided was NULL. */
void
PrettyPrint_Release(char *data)
{
  if (!data) return;
  /* data points just past the BufferDescriptor in the slab */
  free((char*)data - sizeof(BufferDescriptor));
}

/**
 * Finalise output into either a caller buffer or a freshly allocated slab.
 * `payload` is a NUL-terminated string.  Returns pointer to usable text,
 * or NULL on failure.
 */
static char *
sPrettyPrintCommit(const char *payload, BufferDescriptor *buffer_provided)
{
  size_t n = payload ? strlen(payload) : 0;

  if (buffer_provided) {
    if (!buffer_provided->data || buffer_provided->size_max == 0) return NULL;
    size_t cap  = buffer_provided->size_max;
    size_t copy = (n + 1 <= cap) ? n : (cap ? cap - 1 : 0);
    if (cap > 0) {
      if (payload && copy > 0) memcpy(buffer_provided->data, payload, copy);
      buffer_provided->data[copy] = '\0';
      buffer_provided->size       = copy;
    }
    return buffer_provided->data;
  }

  /* Full slab: Descriptor + bytes + NUL */
  BufferDescriptor *bd = malloc(sizeof(*bd) + n + 1);
  if (!bd) return NULL;
  bd->data     = (char*)(bd + 1);
  bd->size     = n;
  bd->size_max = n + 1;
  if (payload) memcpy(bd->data, payload, n + 1);
  else bd->data[0] = '\0';
  return bd->data;
}

/* -------------------------------------------------------------------------- */
/* Name helpers                                                               */
/* -------------------------------------------------------------------------- */

/* Domain: adjust literals if your UfsrvConfigDomain enum differs */
#ifndef UFSRV_CONFIG_DOMAIN_CONFIG
#  define UFSRV_CONFIG_DOMAIN_CONFIG  0
#  define UFSRV_CONFIG_DOMAIN_RUNTIME 1
#endif

static const char *
sDomainName(int domain)
{
  switch (domain) {
  case UFSRV_CONFIG_DOMAIN_CONFIG: return "config";
  case UFSRV_CONFIG_DOMAIN_RUNTIME: return "runtime";
  default: return "unknown";
  }
}

/** Effective C type label: append "_pointer" when value is pointer-like. */
static const char *
sEffectiveCLangType(const UfsrvConfigFieldDescriptorForGenerator *gen, char *buf, size_t bufsz)
{
  const char *base   = (gen && gen->c_lang_type) ? gen->c_lang_type : "unknown";
  int         is_ptr = 0;
  if (gen) {
    switch ((UfsrvConfigValueType)gen->value_type) {
    case UFSRV_CONFIG_VALUE_PTR:
    case UFSRV_CONFIG_VALUE_CONST_PTR:
      is_ptr = 1;
      break;
    default:
      /* also treat c_lang_type ending in '*' as pointer */
      if (gen->c_lang_type && strchr(gen->c_lang_type, '*')) is_ptr = 1;
      break;
    }
  }
  if (is_ptr && !strstr(base, "_pointer") && !strchr(base, '*')) {
    snprintf(buf, bufsz, "%s_pointer", base);
    return buf;
  }
  return base;
}

/* -------------------------------------------------------------------------- */
/* Value → string                                                             */
/* -------------------------------------------------------------------------- */

static void
sFormatArray(const UfsrvConfigValue *v, char *out, size_t outsz)
{
  if (!v->stored_value.blob_val || v->collection_sz == 0) {
    snprintf(out, outsz, "[]");
    return;
  }
  size_t used   = 0;
  out[0]        = '\0';
  used          += (size_t)snprintf(out + used, outsz - used, "[");
  const char *p = (const char*)v->stored_value.blob_val;
  for (size_t i = 0; i < v->collection_sz && used + 1 < outsz; i++) {
    if (i > 0) used += (size_t)snprintf(out + used, outsz - used, ", ");
    used += (size_t)snprintf(out + used, outsz - used, "\"%s\"", p);
    p    += strlen(p) + 1;
  }
  snprintf(out + used, outsz - used, "]");
}

static void
sFormatScalarValue(const UfsrvConfigFieldDescriptorForGenerator *gen, const UfsrvConfigValue *v, char *out,
                   size_t                                        outsz)
{
  if (!v) {
    snprintf(out, outsz, "null");
    return;
  }
  int vt = gen ? gen->value_type : UFSRV_CONFIG_VALUE_TYPE_UNSPECIFIED;
  switch ((UfsrvConfigValueType)vt) {
  case UFSRV_CONFIG_VALUE_INT:
  case UFSRV_CONFIG_VALUE_ENUM:
    snprintf(out, outsz, "%lld", (long long)v->stored_value.int_val);
    break;
  case UFSRV_CONFIG_VALUE_BOOL:
    snprintf(out, outsz, "%s", v->stored_value.int_val ? "true" : "false");
    break;
  case UFSRV_CONFIG_VALUE_DOUBLE:
    snprintf(out, outsz, "%.17g", v->stored_value.double_value);
    break;
  case UFSRV_CONFIG_VALUE_STR:
  case UFSRV_CONFIG_VALUE_CONST_PTR:
    snprintf(out, outsz, "%s", v->stored_value.str_val ? v->stored_value.str_val : "");
    break;
  case UFSRV_CONFIG_VALUE_ARRAY:
    sFormatArray(v, out, outsz);
    break;
  case UFSRV_CONFIG_VALUE_PTR:
  case UFSRV_CONFIG_VALUE_STRUCT:
    if (v->stored_value.ptr_val) snprintf(out, outsz, "%p", v->stored_value.ptr_val);
    else snprintf(out, outsz, "null");
    break;
  default:
    snprintf(out, outsz, "null");
    break;
  }
}

/* Default field value — best-effort string from config_field_default.value */
static void
sFormatDefaultValue(const UfsrvConfigFieldDescriptorForGenerator *gen, char *out, size_t outsz)
{
  if (!gen) {
    snprintf(out, outsz, "");
    return;
  }
  /* UfsrvConfigFieldEntry layout assumed compatible with UfsrvConfigValue
   * storage for the same value_type; adjust if your generator differs. */
  sFormatScalarValue(gen, (const UfsrvConfigValue*)&gen->config_field_default, out, outsz);
}

/* -------------------------------------------------------------------------- */
/* Format builders (digest / full × json / ini / yaml / ufsrv)                */
/* -------------------------------------------------------------------------- */

static char *
sBuildDigestJson(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                 BufferDescriptor *buffer_provided)
{
  char        valbuf[1024];
  char        defbuf[512];
  char        ctype_buf[512];
  const char *ctype = sEffectiveCLangType(gen, ctype_buf, sizeof ctype_buf);

  sFormatScalarValue(gen, value_ptr, valbuf, sizeof valbuf);
  sFormatDefaultValue(gen, defbuf, sizeof defbuf);

  char body[1024 * 3];
  snprintf(body, sizeof body,
           "{" "\"name\":\"%s\"," "\"value\":\"%s\"," "\"type\":\"%s\"," "\"source\":\"%s\"," "\"domain\":\"%s\","
           "\"c_lang_type\":\"%s\"," "\"config_field_default\":\"%s\"," "\"field_index\":%d" "}",
           (gen && gen->name) ? gen->name : "", valbuf, UfsrvConfigTypeLiteral(gen ? gen->value_type : 0),
           UfsrvConfigSourceNameLiteral(value_ptr ? value_ptr->source : UFSRV_CONFIG_SOURCE_UNASSIGNED),
           sDomainName(gen ? gen->domain : 0), ctype, defbuf, field_index);

  return sPrettyPrintCommit(body, buffer_provided);
}

static char *
sBuildFullJson(int               field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
               BufferDescriptor *buffer_provided)
{
  char        valbuf[1024];
  char        defbuf[512];
  char        ctype_buf[128];
  const char *ctype = sEffectiveCLangType(gen, ctype_buf, sizeof ctype_buf);

  sFormatScalarValue(gen, value_ptr, valbuf, sizeof valbuf);
  sFormatDefaultValue(gen, defbuf, sizeof defbuf);

  char body[1024 * 4];
  snprintf(body, sizeof body,
           "{" "\"name\":\"%s\"," "\"value\":\"%s\"," "\"type\":\"%s\"," "\"source\":\"%s\"," "\"domain\":\"%s\","
           "\"c_lang_type\":\"%s\"," "\"config_field_default\":\"%s\"," "\"config_file_key\":\"%s\","
           "\"field_index\":%d," "\"enum_index\":%d," "\"is_as_ptr_getter\":%s," "\"is_keep_custom_getter\":%s,"
           "\"is_keep_custom_setter\":%s," "\"lifecycle\":{" "\"on_inflated\":\"%s\"," "\"on_value_available\":\"%s\","
           "\"on_value_reloaded\":\"%s\"," "\"on_teardown\":\"%s\"," "\"on_pretty_print\":\"%s\"" "}" "}",
           (gen && gen->name) ? gen->name : "", valbuf, UfsrvConfigTypeLiteral(gen ? gen->value_type : 0),
           UfsrvConfigSourceNameLiteral(value_ptr ? value_ptr->source : UFSRV_CONFIG_SOURCE_UNASSIGNED),
           sDomainName(gen ? gen->domain : 0), ctype, defbuf, (gen && gen->config_file_key) ? gen->config_file_key : "",
           field_index, gen ? gen->enum_index : 0, (gen && gen->is_as_ptr_getter) ? "true" : "false",
           (gen && gen->is_keep_custom_getter) ? "true" : "false",
           (gen && gen->is_keep_custom_setter) ? "true" : "false",
           (gen && gen->lifecycle.on_inflated) ? gen->lifecycle.on_inflated : "",
           (gen && gen->lifecycle.on_value_available) ? gen->lifecycle.on_value_available : "",
           (gen && gen->lifecycle.on_value_reloaded) ? gen->lifecycle.on_value_reloaded : "",
           (gen && gen->lifecycle.on_teardown) ? gen->lifecycle.on_teardown : "",
           (gen && gen->lifecycle.on_pretty_print) ? gen->lifecycle.on_pretty_print : "");

  return sPrettyPrintCommit(body, buffer_provided);
}

static char *
sBuildDigestIni(int               field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                BufferDescriptor *buffer_provided)
{
  char        valbuf[1024], defbuf[512], ctype_buf[128];
  const char *ctype = sEffectiveCLangType(gen, ctype_buf, sizeof ctype_buf);
  sFormatScalarValue(gen, value_ptr, valbuf, sizeof valbuf);
  sFormatDefaultValue(gen, defbuf, sizeof defbuf);

  char body[2048];
  snprintf(body, sizeof body,
           "name=%s\n" "value=%s\n" "type=%s\n" "source=%s\n" "domain=%s\n" "c_lang_type=%s\n"
           "config_field_default=%s\n" "field_index=%d\n", (gen && gen->name) ? gen->name : "", valbuf,
           UfsrvConfigTypeLiteral(gen ? gen->value_type : 0),
           UfsrvConfigSourceNameLiteral(value_ptr ? value_ptr->source : UFSRV_CONFIG_SOURCE_UNASSIGNED),
           sDomainName(gen ? gen->domain : 0), ctype, defbuf, field_index);

  return sPrettyPrintCommit(body, buffer_provided);
}

static char *
sBuildDigestYaml(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                 BufferDescriptor *buffer_provided)
{
  char        valbuf[1024], defbuf[512], ctype_buf[128];
  const char *ctype = sEffectiveCLangType(gen, ctype_buf, sizeof ctype_buf);
  sFormatScalarValue(gen, value_ptr, valbuf, sizeof valbuf);
  sFormatDefaultValue(gen, defbuf, sizeof defbuf);

  char body[2048];
  snprintf(body, sizeof body,
           "name: %s\n" "value: %s\n" "type: %s\n" "source: %s\n" "domain: %s\n" "c_lang_type: %s\n"
           "config_field_default: %s\n" "field_index: %d\n", (gen && gen->name) ? gen->name : "", valbuf,
           UfsrvConfigTypeLiteral(gen ? gen->value_type : 0),
           UfsrvConfigSourceNameLiteral(value_ptr ? value_ptr->source : UFSRV_CONFIG_SOURCE_UNASSIGNED),
           sDomainName(gen ? gen->domain : 0), ctype, defbuf, field_index);

  return sPrettyPrintCommit(body, buffer_provided);
}

/** ufsrv config style: key = value  (digest) */
static char *
sBuildDigestUfsrvConfig(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                        BufferDescriptor *buffer_provided)
{
  (void)field_index;
  char valbuf[1024];
  sFormatScalarValue(gen, value_ptr, valbuf, sizeof valbuf);

  const char *key = (gen && gen->config_file_key) ? gen->config_file_key : (gen && gen->name) ? gen->name : "unknown";

  char body[1200];
  snprintf(body, sizeof body, "%s = %s\n", key, valbuf);
  return sPrettyPrintCommit(body, buffer_provided);
}

static char *
sDispatchPretty(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided)
{
  if (fmt == PRETTY_PRINT_DEFAULT) fmt = PRETTY_PRINT_JSON; /* or PRIV_CONFIG_DEFAULT_UFSRV_CONFIG_PRETTY_FORMATTER */

  if (scope == PRETTY_PRINT_SCOPE_DEFAULT) scope = PRETTY_PRINT_SCOPE_DIGEST;

  switch (fmt) {
  case PRETTY_PRINT_JSON:
    return (scope == PRETTY_PRINT_SCOPE_FULL) ?
             sBuildFullJson(field_index, value_ptr, gen, buffer_provided) :
             sBuildDigestJson(field_index, value_ptr, gen, buffer_provided);

  case PRETTY_PRINT_INI:
    return sBuildDigestIni(field_index, value_ptr, gen, buffer_provided);

  case PRETTY_PRINT_YAML:
    return sBuildDigestYaml(field_index, value_ptr, gen, buffer_provided);

  case PRETTY_PRINT_UFSRV_CONFIG:
    return sBuildDigestUfsrvConfig(field_index, value_ptr, gen, buffer_provided);

  default:
    return sBuildDigestJson(field_index, value_ptr, gen, buffer_provided);
  }
}

/* -------------------------------------------------------------------------- */
/* Public standard printers                                                   */
/*                                                                            */
/* Naming: PrettyPrint_<c_lang_type>() with _pointer suffix for pointer       */
/* storage.  Each is a thin typed entry point over sDispatchPretty.           */
/* -------------------------------------------------------------------------- */

char *
PrettyPrint_int(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided)
{
  return sDispatchPretty(field_index, value_ptr, gen, fmt, scope, buffer_provided);
}

char *
PrettyPrint_bool(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                 enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided)
{
  return sDispatchPretty(field_index, value_ptr, gen, fmt, scope, buffer_provided);
}

char *
PrettyPrint_double(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                   enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided)
{
  return sDispatchPretty(field_index, value_ptr, gen, fmt, scope, buffer_provided);
}

char *
PrettyPrint_char_pointer(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                         enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided)
{
  return sDispatchPretty(field_index, value_ptr, gen, fmt, scope, buffer_provided);
}

char *
PrettyPrint_array(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                  enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided)
{
  return sDispatchPretty(field_index, value_ptr, gen, fmt, scope, buffer_provided);
}

/**
 * struct timeval — value stored as STRUCT/PTR; blob interpreted as
 * struct timeval when type_allocation_sz >= sizeof(struct timeval).
 */
char *
PrettyPrint_struct_timeval(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                           enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided)
{
  /* Render timeval into a temporary string value override via local copy */
  UfsrvConfigValue tmp;
  char             str[64];

  if (value_ptr && value_ptr->stored_value.blob_val && value_ptr->type_allocation_sz >= sizeof(struct timeval)) {
    const struct timeval *tv = (const struct timeval*)value_ptr->stored_value.blob_val;
    snprintf(str, sizeof str, "%ld.%06ld", (long)tv->tv_sec, (long)tv->tv_usec);
  }
  else if (value_ptr && value_ptr->stored_value.ptr_val) {
    const struct timeval *tv = (const struct timeval*)value_ptr->stored_value.ptr_val;
    snprintf(str, sizeof str, "%ld.%06ld", (long)tv->tv_sec, (long)tv->tv_usec);
  }
  else {
    snprintf(str, sizeof str, "null");
  }

  tmp = value_ptr ? *value_ptr : (UfsrvConfigValue)
  {
    0
  };
  tmp.stored_value.str_val = str;
  /* Force string formatting path */
  UfsrvConfigFieldDescriptorForGenerator gtmp;
  if (gen) {
    gtmp             = *gen;
    gtmp.value_type  = UFSRV_CONFIG_VALUE_STR;
    gtmp.c_lang_type = "struct timeval";
    gen              = &gtmp;
  }

  return sDispatchPretty(field_index, &tmp, gen, fmt, scope, buffer_provided);
}

/**
 * Generic enum printer — integer store; name comes from gen->name.
 * For named constants, replace with a generated PrettyPrint_enum_<Type>().
 */
char *
PrettyPrint_enum(int field_index, UfsrvConfigValue *value_ptr, UfsrvConfigFieldDescriptorForGenerator *gen,
                 enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *buffer_provided)
{
  return sDispatchPretty(field_index, value_ptr, gen, fmt, scope, buffer_provided);
}

/* Example specialised enum (generator emits one per enum type) */
char *
PrettyPrint_enum_UfsrvConfigSource(int                                     field_index, UfsrvConfigValue *value_ptr,
                                   UfsrvConfigFieldDescriptorForGenerator *gen, enum PrettyPrintFormat fmt,
                                   enum PrettyPrintScope                   scope, BufferDescriptor *buffer_provided)
{
  UfsrvConfigValue tmp = value_ptr ? *value_ptr : (UfsrvConfigValue)
  {
    0
  };
  const char *label = UfsrvConfigSourceNameLiteral((UfsrvConfigSource)tmp.stored_value.int_val);
  char        buf[64];
  snprintf(buf, sizeof buf, "%s", label);
  tmp.stored_value.str_val = buf;

  UfsrvConfigFieldDescriptorForGenerator gtmp;
  if (gen) {
    gtmp             = *gen;
    gtmp.value_type  = UFSRV_CONFIG_VALUE_STR;
    gtmp.c_lang_type = "enum UfsrvConfigSource";
    gen              = &gtmp;
  }
  return sDispatchPretty(field_index, &tmp, gen, fmt, scope, buffer_provided);
}

/* -------------------------------------------------------------------------- */
/* Usage                                                                      */
/* -------------------------------------------------------------------------- */
#if 0
/* Dynamic allocation */
char *s = PrettyPrint_int(idx, &val, &desc, PRETTY_PRINT_JSON, PRETTY_PRINT_SCOPE_DIGEST, NULL);puts(s);
PrettyPrint_Release(s);

/* Caller-supplied buffer */
char storage[512];BufferDescriptor bd = {.data = storage, .size = 0, .size_max = sizeof storage};char *p =
  PrettyPrint_bool(idx, &val, &desc, PRETTY_PRINT_JSON, PRETTY_PRINT_SCOPE_DIGEST, &bd);
/* p == storage; do not PrettyPrint_Release(p) */
#endif

#define PRETTY_PRINT_JSON_MAX_ATTRIBUTE_VALUE_SZ  512 //bytes

PUBLIC_API int
JsonFormatConfigStore(UfsrvConfigFieldEntry *table, size_t table_sz, enum PrettyPrintScope scope, BufferDescriptor *buffer_descriptor_provided)
{
  char value_mem[table_sz][PRETTY_PRINT_JSON_MAX_ATTRIBUTE_VALUE_SZ]; memset(value_mem, '\0', table_sz * PRETTY_PRINT_JSON_MAX_ATTRIBUTE_VALUE_SZ);
  BufferDescriptor value_slots[table_sz];

  for (int i = 0; i < table_sz; i++) {
    value_slots[i].data     = value_mem[i];
    value_slots[i].size     = 0;
    value_slots[i].size_max = sizeof value_mem[i];
  }

  int rc = UfsrvConfigFieldTablePrettyPrint(table, table_sz, PRETTY_PRINT_JSON, scope, value_slots, buffer_descriptor_provided);

  return rc;

}

static int
sUfsrvConfigBufferAppendJsonEscaped(BufferDescriptor *bd, const char *s)
{
    if (!s)
        s = "";
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        int rc;
        switch (*p) {
        case '"':  rc = BufferDescriptorAppendFormatted(bd, "\\\""); break;
        case '\\': rc = BufferDescriptorAppendFormatted(bd, "\\\\"); break;
        case '\b': rc = BufferDescriptorAppendFormatted(bd, "\\b");  break;
        case '\f': rc = BufferDescriptorAppendFormatted(bd, "\\f");  break;
        case '\n': rc = BufferDescriptorAppendFormatted(bd, "\\n");  break;
        case '\r': rc = BufferDescriptorAppendFormatted(bd, "\\r");  break;
        case '\t': rc = BufferDescriptorAppendFormatted(bd, "\\t");  break;
        default:
            if (*p < 0x20)
                rc = BufferDescriptorAppendFormatted(bd, "\\u%04x", *p);
            else
                rc = BufferDescriptorAppendFormatted(bd, "%c", *p);
            break;
        }
        if (rc != 0)
            return -1;
    }
    return 0;
}

/**
 * Contract
 * --------
 * value_slots: BufferDescriptor value_slots[table_sz], zero-initialised
 *              (data == NULL, size == 0, size_max == 0).
 *
 * on_as_string(field_index, &entry->config, &value_slots[i]):
 *   - receives a zeroed BufferDescriptor *
 *   - allocates value_slots[i].data via the same grow/append rules as
 *     UfsrvConfigBufferReserve / BufferDescriptorAppendFormatted
 *   - on success returns the same pointer (== &value_slots[i])
 *   - on failure returns NULL (slot left empty / partially filled)
 *
 * After the JSON body is built, the caller (or this function) frees
 * each value_slots[i].data with free().
 *
 * @param table         field table
 * @param table_sz      entry count
 * @param fmt           PRETTY_PRINT_JSON (others treated as JSON here)
 * @param scope         DIGEST or FULL
 * @param value_slots   required: BufferDescriptor[table_sz], all zeroed
 *                      on entry; each on_as_string fills slots[i]
 * @param out           required: JSON body; zeroed or already inited
 *
 * @return 0 on success, -1 on failure.
 *         On success: out->data holds JSON (caller BufferDescriptorRelease(out)).
 *         Always: value_slots[i].data freed before return (success or fail).
 *
 *  @code
 *  enum { N = 46 };

   // Optional per-field value slots (fixed)
   static char value_mem[N][512];
   static BufferDescriptor value_slots[N];

   for (int i = 0; i < N; i++) {
     value_slots[i].data     = value_mem[i];
     value_slots[i].size     = 0;
     value_slots[i].size_max = sizeof value_mem[i];
   }

   // Growable JSON body — one BufferDescriptor
   BufferDescriptor out = {0};

   int rc = UfsrvConfigFieldTablePrettyPrint(
       ufsrv_config_field_table,
       N,
       PRETTY_PRINT_JSON,
       PRETTY_PRINT_SCOPE_DIGEST,
       value_slots,   // or NULL
       &out);

   if (rc == 0) {
     fputs(out.data, stdout);
     BufferDescriptorRelease(&out);
   }
  * @endcode
  *
  * Sample json output
  *
  * @code
   {
     "serverid": {
       "field_index": "1",
       "value": "0",
       "source": "unassigned"
     },
     "xx": {
       "value": "19701",
       "source": "config_file"
     },
     "xx": {
       "value": "127.0.0.1",
       "source": "defaults"
     }
   }
  * @endcode
 */
PUBLIC_API int
UfsrvConfigFieldTablePrettyPrint(UfsrvConfigFieldEntry *table, size_t table_sz, enum PrettyPrintFormat fmt, enum PrettyPrintScope scope, BufferDescriptor *value_slots, BufferDescriptor *out)
{
    size_t i;

    if (!out || !value_slots) return -1;

    if (!out->data) BufferDescriptorInit(out, 256);
    if (!out->data) return -1;

    out->size = 0;
    out->data[0] = '\0';

    if (fmt == PRETTY_PRINT_DEFAULT)          fmt = PRETTY_PRINT_JSON;
    if (scope == PRETTY_PRINT_SCOPE_DEFAULT)  scope = PRETTY_PRINT_SCOPE_DIGEST;
    (void)fmt;

    if (!table || table_sz == 0) return BufferDescriptorAppendFormatted(out, "{}") == 0 ? 0 : -1;

    if (BufferDescriptorAppendFormatted(out, "{\n") != 0) goto fail;

    for (i = 0; i < table_sz; i++) {
        UfsrvConfigFieldEntry *e = &table[i];
        BufferDescriptor *slot = &value_slots[i];
        const char *val = "";

        slot->data     = NULL;
        slot->size     = 0;
        slot->size_max = 0;

        if (e->lifecycle.on_as_string) {
            BufferDescriptor *bd = e->lifecycle.on_as_string((int)i, &e->config, slot);

            if (bd == slot && slot->data) val = slot->data;
            else {
                // failure or partial — drop any allocation
                BufferDescriptorRelease(slot);
                val = "";
            }
        }

        if (i > 0) {
            if (BufferDescriptorAppendFormatted(out, ",\n") != 0)  goto fail_release_current;
        }

        if (BufferDescriptorAppendFormatted(out, "  \"%s\": {\n", e->field_name) != 0)   goto fail_release_current;
        if (BufferDescriptorAppendFormatted(out, "    \"field_index\": \"%d\",\n", i) != 0)   goto fail_release_current;
        if (BufferDescriptorAppendFormatted(out, "    \"value\": \"") != 0)   goto fail_release_current;
        if (sUfsrvConfigBufferAppendJsonEscaped(out, val) != 0)               goto fail_release_current;
        if (BufferDescriptorAppendFormatted(out, "\",\n") != 0)               goto fail_release_current;

        if (BufferDescriptorAppendFormatted(out, "    \"source\": \"") != 0)                                goto fail_release_current;
        if (sUfsrvConfigBufferAppendJsonEscaped(out, UfsrvConfigSourceNameLiteral(e->config.source)) != 0)    goto fail_release_current;
        if (BufferDescriptorAppendFormatted(out, "\"") != 0)                                                goto fail_release_current;

        if (scope == PRETTY_PRINT_SCOPE_FULL) {
            if (BufferDescriptorAppendFormatted(out,
                    ",\n"
                    "    \"type_allocation_sz\": %zu,\n"
                    "    \"collection_sz\": %zu,\n"
                    "    \"has_on_as_string\": %s",
                    e->config.type_allocation_sz,
                    e->config.collection_sz,
                    e->lifecycle.on_as_string ? "true" : "false") != 0)
                goto fail_release_current;
        }

        if (BufferDescriptorAppendFormatted(out, "\n  }") != 0) goto fail_release_current;

        //value text no longer needed
        BufferDescriptorRelease(slot);
        continue;

fail_release_current:
        BufferDescriptorRelease(slot);
        goto fail;
    }

    if (BufferDescriptorAppendFormatted(out, "\n}\n") != 0) goto fail;

    return 0;

fail:
    // release any slots still holding data (e.g. early exit mid-table)
    for (i = 0; i < table_sz; i++) {
      BufferDescriptorRelease(&value_slots[i]);
    }
    BufferDescriptorRelease(out);
    return -1;
}
