-- unknown_scope.lua — has a scope not in any schema (should silently ignore)
return {
    ufsrv = {
        ufsrv_geogroup     = 1,
        main_listener_port = 19701,
    },
    some_random_scope = {
        foo = "bar",
    },
}
