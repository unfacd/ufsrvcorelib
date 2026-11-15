-- out_of_bounds.lua — main_listener_port below minimum (0 < min=1)
return {
    ufsrv = {
        ufsrv_geogroup     = 1,
        main_listener_port = 0,
    },
}
