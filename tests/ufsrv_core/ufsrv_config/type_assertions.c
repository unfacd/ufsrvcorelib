/**
 * @file type_assertions.c
 * @brief Compile-time assertions for ufsrv_config type headers.
 *
 * Copyright (C) 2015-2026 unfacd works
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is a COMPILE-TIME test.  If it compiles, all assertions pass.
 * If a static_assert fires, the type definition is wrong.
 *
 * This file is NOT linked into any test binary — compilation is the test.
 */

#include <assert.h>
#include <stddef.h>   /* offsetof */

#include "ufsrv_config_value_type.h"
#include "ufsrv_config_field_descriptor.h"

// Enum value ordering — zero-based, must match generator assumptions

static_assert(UFSRV_CONFIG_VALUE_INT       == 0,
    "UFSRV_CONFIG_VALUE_INT must be 0 (zero-value default for calloc'd storage)");
static_assert(UFSRV_CONFIG_VALUE_STR       == 1,
    "UFSRV_CONFIG_VALUE_STR must be 1");
static_assert(UFSRV_CONFIG_VALUE_PTR       == 2,
    "UFSRV_CONFIG_VALUE_PTR must be 2");
static_assert(UFSRV_CONFIG_VALUE_STRUCT    == 3,
    "UFSRV_CONFIG_VALUE_STRUCT must be 3");
static_assert(UFSRV_CONFIG_VALUE_CONST_PTR == 4,
    "UFSRV_CONFIG_VALUE_CONST_PTR must be 4");
static_assert(UFSRV_CONFIG_VALUE_ENUM      == 5,
    "UFSRV_CONFIG_VALUE_ENUM must be 5");

static_assert(UFSRV_CONFIG_DOMAIN_CONFIG  == 0,
    "UFSRV_CONFIG_DOMAIN_CONFIG must be 0 (CONFIG domains appear first in enum)");
static_assert(UFSRV_CONFIG_DOMAIN_RUNTIME == 1,
    "UFSRV_CONFIG_DOMAIN_RUNTIME must be 1");

static_assert(UFSRV_CONFIG_LAYER_DEFAULTS == 0,
    "UFSRV_CONFIG_LAYER_DEFAULTS must be 0 (first population pass)");
static_assert(UFSRV_CONFIG_LAYER_FILE     == 1,
    "UFSRV_CONFIG_LAYER_FILE must be 1 (second population pass)");
static_assert(UFSRV_CONFIG_LAYER_CLI      == 2,
    "UFSRV_CONFIG_LAYER_CLI must be 2 (third and final population pass)");

// Validation check types — zero-based

static_assert(UFSRV_CONFIG_CHECK_NONE         == 0,
    "UFSRV_CONFIG_CHECK_NONE must be 0 (no validation)");
static_assert(UFSRV_CONFIG_CHECK_RANGE        == 1,
    "UFSRV_CONFIG_CHECK_RANGE must be 1");
static_assert(UFSRV_CONFIG_CHECK_NOT_EMPTY    == 2,
    "UFSRV_CONFIG_CHECK_NOT_EMPTY must be 2");
static_assert(UFSRV_CONFIG_CHECK_PATH_EXISTS  == 3,
    "UFSRV_CONFIG_CHECK_PATH_EXISTS must be 3");
static_assert(UFSRV_CONFIG_CHECK_IS_DIRECTORY == 4,
    "UFSRV_CONFIG_CHECK_IS_DIRECTORY must be 4");
static_assert(UFSRV_CONFIG_CHECK_IS_FILE      == 5,
    "UFSRV_CONFIG_CHECK_IS_FILE must be 5");

// Config result states — zero-based

static_assert(UFSRV_CONFIG_OK          == 0,
    "UFSRV_CONFIG_OK must be 0 (success)");

// sizeof checks — ensure no ABI surprises

static_assert(sizeof(UfsrvConfigItem) <= 40,
    "UfsrvConfigItem must not exceed 32 bytes (register-pressure constraint)");

static_assert(sizeof(UfsrvConfigValue) <= 40,
    "UfsrvConfigValue must not exceed 40 bytes");

static_assert(sizeof(UfsrvConfigFieldDescriptor) <= 96,
    "UfsrvConfigFieldDescriptor must not exceed 80 bytes (cache-line friendly)");

static_assert(sizeof(UfsrvConfigValidation) <= 24,
    "UfsrvConfigValidation must not exceed 24 bytes");

// UfsrvConfigFieldDescriptor member offsets

/* name pointer must be first — NULL sentinel detection in generator */
static_assert(offsetof(UfsrvConfigFieldDescriptor, name) == 0,
    "UfsrvConfigFieldDescriptor.name must be at offset 0 (first member)");

/* value_type immediately follows name */
static_assert(offsetof(UfsrvConfigFieldDescriptor, value_type) >
              offsetof(UfsrvConfigFieldDescriptor, name),
    "value_type must follow name");

/* domain immediately follows value_type */
static_assert(offsetof(UfsrvConfigFieldDescriptor, domain) >
              offsetof(UfsrvConfigFieldDescriptor, value_type),
    "domain must follow value_type");

// Integral type sizes (portability)

static_assert(sizeof(int) >= 4,
    "int must be at least 32 bits for config value storage");
