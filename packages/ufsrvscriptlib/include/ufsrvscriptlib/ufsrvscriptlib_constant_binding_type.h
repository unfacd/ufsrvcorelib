/**
 * @file ufsrvscriptlib_constant_binding_type.h
 * @brief Script-visible global constant for the ufsrvscriptlib engine.
 *
 * Type-only header: a read-only value registered into the script's global
 * environment at bootstrap. Unlike a UfsrvScriptFunctionBinding (a callable function),
 * a constant is plain data — a string, integer, or boolean — reached by a
 * dotted name ("ufsrvcli.version" becomes the global table `ufsrvcli` with a
 * `version` field). Includes only what the type needs (<lua.h> for lua_Integer).
 */
#ifndef UFSRVSCRIPTLIB_CONSTANT_BINDING_TYPE_H
#define UFSRVSCRIPTLIB_CONSTANT_BINDING_TYPE_H

#include <lua.h>

/*! Which value field of a UfsrvScriptConstantBinding holds the value. */
typedef enum UfsrvScriptConstantType {
  UFSRVSCRIPTLIB_CONSTANT_TYPE_NONE,      ///< sentinel (NULL name); no value
  UFSRVSCRIPTLIB_CONSTANT_TYPE_STRING,    ///< string_value
  UFSRVSCRIPTLIB_CONSTANT_TYPE_INTEGER,   ///< integer_value
  UFSRVSCRIPTLIB_CONSTANT_TYPE_BOOLEAN,   ///< boolean_value
} UfsrvScriptConstantType;

/*! A read-only global exposed to scripts, keyed by a dotted name. */
typedef struct UfsrvScriptConstantBinding {
  const char              *name;           ///< dotted path, e.g. "ufsrvcli.version"
  UfsrvScriptConstantType  type;           ///< which value field is valid
  const char              *string_value;   ///< valid when type is STRING
  lua_Integer              integer_value;  ///< valid when type is INTEGER
  int                      boolean_value;  ///< valid when type is BOOLEAN (0/1)
} UfsrvScriptConstantBinding;

#endif /* UFSRVSCRIPTLIB_CONSTANT_BINDING_TYPE_H */
