-- config.lua — ufsrvwebsock configuration (namespaced, three-tier)
--
-- Tier 1 — ufsrv scope: inherited by every ufsrv-class server
-- Tier 2 — ufnet scope: servers in the unfacd messaging network
-- Tier 3 — ufsrvwebsock scope: WebSocket-server-specific
--
-- Converted from the legacy flat-global format (etc/ufsrv-1.lua).
-- Validated against core_schema_ufsrv.lua + core_schema_ufnet.lua +
-- adjunct_schema_ufsrvwebsock.lua via core_schema_validator.lua.
--
-- ══════════════════════════════════════════════════════════════════════════
-- Tier 1 — ufsrv scope (every ufsrv-class server inherits)
-- ══════════════════════════════════════════════════════════════════════════

ufsrv = {

    -- ── Server identity ──────────────────────────────────────────────
    server_id               = 0,
    server_run_mode         = "shadow",         -- "normal" | "shadow"
    server_cpu_affinity     = "system_override", -- "system_override" | "managed" | "user_override"
    intra_ufsrv_classname   = "ufsrv",
    ufsrv_geogroup          = 3,

    -- ── Network listeners ────────────────────────────────────────────
    command_console_port         = 19700,
    command_console_bind_address = "127.0.0.1",

    -- ── Thread pools ─────────────────────────────────────────────────
    session_workers_thread_pool   = 2,
    ufsrv_workers_thread_pool     = 2,
    listener_workers_thread_pool  = 1,

    -- ── SSL — console (admin) ────────────────────────────────────────
    ssl = {
        certificate = "ufsrv_certificate_key.pem",
        key         = "ufsrv_certificate_key.pem",
        location    = ""
    },
    ssl_command_console = { required = 1, client_certificate = 1 },

    -- ── SSL — client connections ─────────────────────────────────────
    ssl_user = {
        certificate = "ufsrv_certificate_key.pem",
        key         = "ufsrv_certificate_key.pem",
        location    = "",
        enabled     = 1
    },

    -- ── StatsD instrumentation backend ───────────────────────────────
    stats_backend = { address = "127.0.0.1", port = 8125 },

    -- ── User session timeouts (seconds) ──────────────────────────────
    user_timeouts = {
        unauthenticated_timeout = 60,
        connected_timeout       = 120,
        suspended_timeout       = 0,
        locationless_timeout    = 0
    },

    -- ── I/O buffer sizes (bytes) ──────────────────────────────────────
    buffer_sizes = {
        incoming_buffer_size    = 10240,
        incoming_buffer_maxsize = 1024000,
        outgoing_buffer_size    = 1024,
        holding_buffer_size     = 1024
    },

    -- ── Session memory-pool specs ────────────────────────────────────
    memory_specs_for_session = {
        sessions_per_allocation_group  = 1024,
        allocation_groups              = 10,
        allocation_threshold_trigger   = "10%"
    },

    -- ── File loader (concurrent registry + background I/O) ───────────
    fileloader = {
        enabled              = 0,  -- disabled by default; set to 1 to enable
        registry_size        = 4096,
        registry_hot_loaded_size  = 64,
        black_listed_files   = {}
    },

    -- ── Runtime (privilege drop) ─────────────────────────────────────
    sys_runtime = {
        run_as_user = "",
        chroot      = "/"
    }
}

-- ══════════════════════════════════════════════════════════════════════════
-- Tier 2 — ufnet scope (unfacd messaging network participants)
-- ══════════════════════════════════════════════════════════════════════════

ufnet = {

    -- ── GeoIP backend ────────────────────────────────────────────────
    geoip_service = { address = "127.0.0.1", port = 19801 },

    -- ── Message queue (inter/intra-instance messaging) ───────────────
    msgqueue = {
        is_enabled = false,
        address    = "ufsrvmsgqueue.unfacd.com",
        port       = 6380
    },

    -- ── Database backend (MariaDB) ───────────────────────────────────
    db_backend = {
        port     = 19800,
        address  = "db.ufsrv.unfacd.com",
        username = "ufsrv_user",
        password = "c*"
    },

    -- ── Persistence backend (Redis) ──────────────────────────────────
    persistence_backend = {
        address = "ufsrvpersistance.unfacd.com",
        port    = 19705,
        mode    = "tcp",
        timeout = 500000
    },

    -- ── User-message cache backend (Redis) ───────────────────────────
    cache_backend_usrmsg = {
        address = "usrmsg.cachebackend.unfacd.io",
        port    = 22001,
        mode    = "tcp",
        timeout = 500000
    },

    -- ── Fence cache backend (Redis) ──────────────────────────────────
    cache_backend_fence = {
        address = "fence.cachebackend.unfacd.io",
        port    = 32001,
        mode    = "tcp",
        timeout = 500000
    }
}

-- ══════════════════════════════════════════════════════════════════════════
-- Tier 3 — ufsrvwebsock scope (WebSocket-server-specific)
-- ══════════════════════════════════════════════════════════════════════════

ufsrvwebsock = {
    max_frame_size              = 65536,
    max_message_size            = 1048576,
    heartbeat_interval_secs     = 30,
    close_timeout_secs          = 10,
    max_connections_per_session = 1
}
