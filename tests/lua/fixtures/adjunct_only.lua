-- adjunct_only.lua — only ufsrvapi scope, no ufsrv scope
return {
    ufsrvapi = {
        this_server_origin  = "https://api.example.com",
        ufsrv_media_upload_uri = "https://media.example.com/",
        cache_authorisation = {
            size            = 2048,
            eviction_policy = "age",
        },
    },
}
