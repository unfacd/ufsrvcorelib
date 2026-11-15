/**
* @file ufsrv_config_store_operating_state_enum.h
 * @brief Enumerated states, describing the operating evolution of the various ufsrv config stores.
 *
 * Copyright (C) 2015-2026 unfacd works
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * UfsrvConfigValue is the canonical value representation used everywhere —
 * cold catalogue defaults, hot store slots, and values passed to lifecycle
 * callbacks.  The @p stored_value union carries the value itself; @p
 * type_allocation_sz carries the byte count for BLOB memcpy or the buffer
 * size for STR fields.
 */
#ifndef UFSRVCORELIB_UFSRV_CONFIG_STORE_OPERATING_STATE_ENUM_H
#define UFSRVCORELIB_UFSRV_CONFIG_STORE_OPERATING_STATE_ENUM_H

typedef enum UfsrvConfigStoreOperatingState {
 UFSRV_CONFIG_STORE_OPERATING_STATE_BOOTED = 0, ///> the config store is part of the host program static
 UFSRV_CONFIG_STORE_OPERATING_STATE_INFLATED,   ///> the various items have their memory allocated via on_inflated lifecycle method
 UFSRV_CONFIG_STORE_OPERATING_DEFAULTS_LOADED,
 UFSRV_CONFIG_STORE_OPERATING_STATE_VALUES_MARSHALLED, ///> lifecycle method on_value_available invoked
 UFSRV_CONFIG_STORE_OPERATING_STATE_RELOADED,
 UFSRV_CONFIG_STORE_OPERATING_STATE_VALUES_TORN_DOWN
} UfsrvConfigStoreOperatingState;

/*!
 * Return the config-literal string for a UfsrvConfigStoreOperatingState value.
 * Unknown values fall back to "booted".
 */
static inline const char *
UfsrvConfigStoreOperatingStateToLiteral(UfsrvConfigStoreOperatingState state)
{
  switch (state) {
    case UFSRV_CONFIG_STORE_OPERATING_STATE_INFLATED:           return "inflated";
    case UFSRV_CONFIG_STORE_OPERATING_DEFAULTS_LOADED:          return "defaults_loaded";
    case UFSRV_CONFIG_STORE_OPERATING_STATE_VALUES_MARSHALLED:  return "values_marshalled";
    case UFSRV_CONFIG_STORE_OPERATING_STATE_RELOADED:           return "reloaded";
    case UFSRV_CONFIG_STORE_OPERATING_STATE_VALUES_TORN_DOWN:   return "values_torn_down";
    case UFSRV_CONFIG_STORE_OPERATING_STATE_BOOTED:
    default:                                                    return "booted";
  }
}

#endif //UFSRVCORELIB_UFSRV_CONFIG_STORE_OPERATING_STATE_ENUM_H
