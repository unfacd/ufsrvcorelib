-- adjunct_schema_ufsrvwebsock.lua — ufsrvwebsock adjunct namespace schema
--
-- Supplied by the ufsrvwebsock application.  Defines the `ufsrvwebsock` scope.
-- The validator merges this with the core schema (ufsrv namespace) before
-- validating the ufsrvwebsock config file.
--
-- IMPORTANT: when you make changes to attribute names, make sure they match literals defined in:
-- include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h
--
-- Copyright (C) 2015-2026 unfacd works
-- SPDX-License-Identifier: AGPL-3.0-or-later

local schema = {

    -- ══════════════════════════════════════════════════════════════════
    -- ufsrvwebsock scope — WebSocket-server-specific
    -- ══════════════════════════════════════════════════════════════════

    ufsrvwebsock = {
        -- ── WebSocket protocol ───────────────────────────────────────
        websocket = {
               max_frame_size              = { type = "integer", required = false, default = 65536,   min = 256 },
               max_message_size            = { type = "integer", required = false, default = 1048576, min = 256 },
               heartbeat_interval_secs     = { type = "integer", required = false, default = 30,      min = 5 },
               close_timeout_secs          = { type = "integer", required = false, default = 10,      min = 1 },
               max_connections_per_session = { type = "integer", required = false, default = 1,       min = 1 },
       }
    }
}

return schema
