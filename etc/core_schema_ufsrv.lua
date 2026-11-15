-- core_schema_ufsrv.lua — core ufsrv server-domain schema
--
-- Ships with ufsrvcorelib.  Every ufsrv-class server (websock, api, mcp,
-- sfu, stun, …) inherits this schema.  It defines the `ufsrv` scope —
-- the minimal configuration every ufsrv server needs regardless of
-- whether it participates in the ufnet messaging network.
--
-- Servers that DO participate in ufnet also inherit:
--   core_schema_ufnet.lua  (msgqueue, backends, CORS, …)
--
-- Server-class-specific config lives in adjunct schemas supplied by
-- each application (e.g. adjunct_schema_ufsrvwebsock.lua).
--
-- The validator merges all applicable schemas before validation.
--
-- IMPORTANT: when you make changes to attribute names, make sure they match literals defined in:
-- include/ufsrvcorelib/ufsrv_core/configfile_processor/ufsrv_config_literals_defs.h
--
-- Copyright (C) 2015-2026 unfacd works
-- SPDX-License-Identifier: AGPL-3.0-or-later


local schema = {

    -- ══════════════════════════════════════════════════════════════════
    -- ufsrv scope — shared across all ufsrv-class servers
    -- ══════════════════════════════════════════════════════════════════

    ufsrv = {
        required = true,

        -- ── Server identity ──────────────────────────────────────────
        server_instance_id    = { type = "integer", required = false, default = 0 },
        server_run_mode       = { type = "string",  required = false, default = "normal" },
        server_cpu_affinity   = { type = "string",  required = false, default = "system_override" },

        -- ── Geographic routing ────────────────────────────────────────
        ufsrv_geogroup = { type = "integer", required = true, min = 0 },

        -- ── Network listeners ──────────────────────────────────────────
        main_listener_port           = { type = "integer", required = true,  min = 1, max = 65535 },
        main_listener_address        = { type = "string",  required = false, default = "127.0.0.1" },
        main_listener_protocol_id    = { type = "integer", required = false, default = 1 },
        command_console_port         = { type = "integer", required = false, default = 19700, min = 1, max = 65535 },
        command_console_address = { type = "string",  required = false, default = "127.0.0.1" },

        -- ── Thread pools ───────────────────────────────────────────────
        session_workers_thread_pool  = { type = "integer", required = false, default = 2, min = 1 },
        ufsrv_workers_thread_pool    = { type = "integer", required = false, default = 2, min = 1 },
        listener_workers_thread_pool = { type = "integer", required = false, default = 1, min = 1 },

        -- ── SSL — console (admin) ──────────────────────────────────────
        ssl_command_console = {
            certificate_file = { type = "string", required = false, default = "ufsrv_console_certificate.pem" },
            key_file         = { type = "string", required = false, default = "ufsrv_console.key" },
            is_enabled       = { type = "boolean", required = false, default = false },
            is_client_certificate_required = { type = "boolean", required = false, default = false},
            allow_list_god    = { type = "array",  required = false, default = {} },
            allow_list_browser= { type = "array",  required = false, default = {} },
            deny_list      = { type = "array",  required = false, default = {} },
        },
        -- ── SSL — client connections ───────────────────────────────────
        ssl_command_console_client = {
            certificate_file = { type = "string",  required = false, default = "ufsrv_console_client_certificate_key.pem" },
            key_file         = { type = "string",  required = false, default = "ufsrv_console_client_key.key" },
            is_enabled       = { type = "boolean", required = false, default = false },
        },

        -- ── StatsD instrumentation backend ─────────────────────────────
        stats_backend = {
            address = { type = "string",  required = false, default = "127.0.0.1" },
            port    = { type = "integer", required = false, default = 8125, min = 1, max = 65535 }
        },

        -- ── User session timeouts (seconds) ────────────────────────────
        user_timeouts = {
            unauthenticated_timeout = { type = "integer", required = false, default = 60,  min = 0 },
            connected_timeout       = { type = "integer", required = false, default = 120, min = 0 },
            suspended_timeout       = { type = "integer", required = false, default = 60,  min = 0 },
            locationless_timeout    = { type = "integer", required = false, default = 300, min = 0 }
        },

        -- ── I/O buffer sizes (bytes) ────────────────────────────────────
        buffer_sizes = {
            incoming_buffer_size    = { type = "integer", required = false, default = 1024,   min = 256 },
            incoming_buffer_maxsize = { type = "integer", required = false, default = 1024000, min = 1024 },
            outgoing_buffer_size    = { type = "integer", required = false, default = 1024,   min = 256 },
            holding_buffer_size     = { type = "integer", required = false, default = 1024,   min = 256 }
        },

        -- ── Session memory-pool specs ──────────────────────────────────
        memory_specs_for_session = {
            sessions_per_allocation_group = { type = "integer", required = false, default = 1024, min = 64 },
            allocation_groups             = { type = "integer", required = false, default = 10,   min = 1 },
            allocation_threshold_trigger  = { type = "string",  required = false, default = "10%" }
        },

        -- ── File loader (concurrent registry + background I/O) ──────────
        fileloader = {
            is_enabled   = { type = "boolean", required = false, default = false },
            registry_size        = { type = "integer", required = false, default = 4096, min = 1, max = 1048576 },
            registry_hot_loaded_size  = { type = "integer", required = false, default = 64, min = 1, max = 1048576 },
            black_listed_files_registry   = { type = "array",  required = false, default = {} },
            files_registry                = { type = "array",  required = false, default = {} }
        },

        ratelimit = {
            is_enabled           = { type = "boolean", required = false, default = false },
            is_bypass_private_ip = { type = "boolean", required = false, default = false }
        },

        -- ── Runtime (privilege drop) ───────────────────────────────────
        sys_runtime = {
            required = true,
            run_as_user = { type = "string", required = false, default = "" },
            chroot      = { type = "string", required = false, default = "/" }
        },

        -- ── Logger ───────────────────────────────────
        logger = {
            driver = { type = "string", required = false, default = "file" }, -- "syslog| stderr| stdout| file"
            format = { type = "string", required = false, default = "%d(%F %T) %-8V [%c] %f:%L %m%n" }, -- 2026-10-29 15:55:59 INFO     [prefix] file_name.c:67
            prefix = { type = "string", required = false, default = "ufsrv" }

        },

        -- ── OAuth access-token issuance ───────────────────────────────
        oauth = {
            access_token_format = { type = "string", required = false, default = "jwt" }, -- "opaque" | "jwt"
            jwt_issuer          = { type = "string", required = false, default = "" },
            jwt_signing_key     = { type = "string", required = false, default = "" },
            jwt_active_kid      = { type = "string", required = false, default = "" }
        },

        -- ── Outbound e-mail provider (Mailgun-compatible HTTP API) ─────
        -- api_key is stored as an enc:v1:<nonce>:<ciphertext>:<tag> envelope,
        -- decrypted at the point of use with the application secret key.
        email = {
            api_key = { type = "string", required = false, default = "" },
            api_url = { type = "string", required = false, default = "" }
        }
    },

    -- ══════════════════════════════════════════════════════════════════
    -- ufnet scope — unfacd messaging-network configuration
    -- ══════════════════════════════════════════════════════════════════

    ufnet = {

        -- ── GeoIP service backend ──────────────────────────────────────────────
        geoip_service = {
            address = { type = "string",  required = false, default = "127.0.0.1" },
            port    = { type = "integer", required = false, default = 19801, min = 1, max = 65535 }
        },

        -- ── Message queue (inter/intra-instance messaging) ────────────
        msgqueue = {
            is_enabled   = { type = "boolean", required = false, default = false },
            address      = { type = "string",  required = false, default = "ufsrvmsgqueue.unfacd.com" },
            port         = { type = "integer", required = false, default = 6380, min = 1, max = 65535 }
        },

        -- ── Database backend (MariaDB) ────────────────────────────────
        db_backend = {
            port     = { type = "integer", required = true,  min = 1, max = 65535 },
            address  = { type = "string",  required = true },
            username = { type = "string",  required = true },
            password = { type = "string",  required = true }
        },

        -- ── Persistence backend (Redis) ───────────────────────────────
        persistence_backend = {
            address = { type = "string",  required = false, default = "127.0.0.1" },
            port    = { type = "integer", required = false, default = 19705, min = 1, max = 65535 },
            mode    = { type = "string",  required = false, default = "tcp" },
            timeout = { type = "integer", required = false, default = 500000, min = 0 }
        },

        -- ── User-message cache backend (Redis) ────────────────────────
        cache_backend_usrmsg = {
            address = { type = "string",  required = false, default = "127.0.0.1" },
            port    = { type = "integer", required = false, default = 22001, min = 1, max = 65535 },
            mode    = { type = "string",  required = false, default = "tcp" },
            timeout = { type = "integer", required = false, default = 500000, min = 0 }
        },

        -- ── Fence cache backend (Redis) ───────────────────────────────
        cache_backend_fence = {
            address = { type = "string",  required = false, default = "127.0.0.1" },
            port    = { type = "integer", required = false, default = 32001, min = 1, max = 65535 },
            mode    = { type = "string",  required = false, default = "tcp" },
            timeout = { type = "integer", required = false, default = 500000, min = 0 }
        },

        -- ── Instrumentation backend override (StatsD) ─────────────────
        instrumentation_backend = {
            address = { type = "string",  required = false, default = "127.0.0.1" },
            port    = { type = "integer", required = false, default = 8125, min = 1, max = 65535 }
        },
    }
}

return schema
