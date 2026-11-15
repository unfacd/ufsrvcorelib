-- valid_full.lua — config with both ufsrv and ufnet scopes, all required fields
ufsrv = {
    server_id             = 1,
    server_run_mode       = "normal",
    ufsrv_geogroup        = 3,
    main_listener_port    = 19701,
    ufsrv_workers_thread_pool   = 2,
    session_workers_thread_pool = 2,
    listener_workers_thread_pool = 1,
    user_timeouts = {
        unauthenticated_timeout = 60,
        connected_timeout       = 120,
        suspended_timeout       = 60,
        locationless_timeout    = 300,
    },
    buffer_sizes = {
        incoming_buffer_size    = 4096,
        incoming_buffer_maxsize = 1024000,
    },
    memory_specs_for_session = {
        sessions_per_allocation_group = 512,
        allocation_groups             = 5,
        allocation_threshold_trigger  = "20%",
    },
    fileloader = {
        is_enabled = true,
        registry_size = 8192,
        registry_hot_loaded_size = 128,
        black_listed_files = {
            "/etc/passwd",
            "/etc/shadow",
            "/proc/self/mem"
        }
    },
    sys_runtime = {
        run_as_user = "",
        chroot = "/"
    }
}

ufnet = {
    db_backend = {
        port     = 19800,
        address  = "db.ufsrv.unfacd.com",
        username = "ufsrv_user",
        password = "secret",
    },
}
