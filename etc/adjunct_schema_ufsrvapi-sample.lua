-- adjunct_schema_ufsrvapi.lua — ufsrvapi adjunct namespace schema
--
-- Supplied by the ufsrvapi application.  Defines the `ufsrvapi` scope.
-- The validator merges this with the core schema (ufsrv namespace) before
-- validating the ufsrvapi config file.
--
-- IMPORTANT: when you make changes to attribute names, make sure they match literals defined in:
-- include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h
--
-- Copyright (C) 2015-2026 unfacd works
-- SPDX-License-Identifier: AGPL-3.0-or-later

local schema = {

    -- ══════════════════════════════════════════════════════════════════
    -- ufsrvapi scope — API-server-specific
    -- ══════════════════════════════════════════════════════════════════

    ufsrvapi = {

-- ── HTTP / CORS ───────────────────────────────────────────────
        this_server_origin = { type = "string", required = false, default = "" },
        allowed_origins    = { type = "array",  required = false, subtype = "string" },


        -- ── Media upload endpoint ────────────────────────────────────
        ufsrv_media_upload_uri = { type = "string", required = false, default = "" },

        -- ── Cache authorisation ──────────────────────────────────────
        cache_authorisation = {
            size            = { type = "integer", required = false, default = 1024, min = 64 },
            eviction_policy = { type = "string",  required = false, default = "least" }
        }
    }
}

return schema
