//
// Created by ayman on 7/08/2026.
//

#ifndef UFSRVCORELIB_UFSRV_CONFIG_KEYMAP_ENTRY_TYPE_H
#define UFSRVCORELIB_UFSRV_CONFIG_KEYMAP_ENTRY_TYPE_H

typedef struct UfsrvConfigKeyMapEntry
{
  const char *config_literal; ///< CONF_FILE_LITERAL_* macro — Lua config-file key
  int         field_index;    ///< UfsrvConfigField enum value
} UfsrvConfigKeyMapEntry;

#endif //UFSRVCORELIB_UFSRV_CONFIG_KEYMAP_ENTRY_TYPE_H
