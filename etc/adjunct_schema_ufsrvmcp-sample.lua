-- adjunct_schema_ufsrvmcp.lua — ufsrvmcp adjunct namespace schema
--
-- Supplied by the ufsrvmcp application.  Defines the `ufsrvmcp` scope.
-- The validator merges this with the core schema (ufsrv namespace) before
-- validating the ufsrvmcp config file.
--
-- IMPORTANT: when you make changes to attribute names, make sure they match literals defined in:
-- include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h
--
-- Copyright (C) 2015-2026 unfacd works
-- SPDX-License-Identifier: AGPL-3.0-or-later

local schema = {

    -- ══════════════════════════════════════════════════════════════════
    -- ufsrvmcp scope — MCP-server-specific
    -- ══════════════════════════════════════════════════════════════════

    ufsrvmcp = {

    }
}

return schema
