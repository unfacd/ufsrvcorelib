//
// Created by ayman on 28/07/2026.
//

#ifndef UFSRVCORELIB_SERVER_RUNMODE_ENUM_H
#define UFSRVCORELIB_SERVER_RUNMODE_ENUM_H

typedef enum ServerRunMode {
  RUNMODE_NORMAL, RUNMODE_SHADOW, RUNMODE_INVALID
} ServerRunMode;

/*!
 * Return the config-literal string for a ServerRunMode value.
 * Unknown values fall back to "invalid".
 */
static inline const char *
ServerRunModeToLiteral(ServerRunMode mode)
{
  switch (mode) {
    case RUNMODE_NORMAL:  return "normal";
    case RUNMODE_SHADOW:  return "shadow";
    case RUNMODE_INVALID:
    default:              return "invalid";
  }
}

#endif //UFSRVCORELIB_SERVER_RUNMODE_ENUM_H