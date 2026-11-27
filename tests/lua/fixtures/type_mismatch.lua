-- type_mismatch.lua — command_console_port is a string, should be integer
return {
    ufsrv = {
        ufsrv_geogroup     = 1,
        command_console_port = "not-a-number",
    },
}
