-- type_mismatch.lua — main_listener_port is a string, should be integer
return {
    ufsrv = {
        ufsrv_geogroup     = 1,
        main_listener_port = "not-a-number",
    },
}
