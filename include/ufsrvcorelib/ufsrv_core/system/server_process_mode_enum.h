//
// Created by ayman on 28/07/2026.
//

#ifndef UFSRVCORELIB_SERVER_PROCESS_MODE_H
#define UFSRVCORELIB_SERVER_PROCESS_MODE_H

enum ServerProcessMode {
  SERVER_PROCESS_MODE_DEFAULT,
  SERVER_PROCESS_MODE_DAEMON, ///< Legacy systemV fork based server
  SERVER_PROCESS_MODE_TERMINAL, ///< Foreground
  SERVER_PROCESS_MODE_SYSTEMD ///< Moder systemd based process management
 };

/*!
 * Return the config-literal string for a ServerProcessMode value.
 * Unknown values fall back to "default".
 */
static inline const char *
ServerProcessModeToLiteral(enum ServerProcessMode mode)
{
  switch (mode) {
    case SERVER_PROCESS_MODE_DAEMON:    return "daemon";
    case SERVER_PROCESS_MODE_TERMINAL:  return "terminal";
    case SERVER_PROCESS_MODE_SYSTEMD:   return "systemd";
    case SERVER_PROCESS_MODE_DEFAULT:
    default:                            return "default";
  }
}

#endif //UFSRVCORELIB_SERVER_PROCESS_MODE_H
