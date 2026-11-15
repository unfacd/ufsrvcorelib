# Example command catalogue for the console-script generator.
#
# Each server (ufsrvwebsock, ufsrvapi, …) keeps its own catalogue listing the
# command paths it exposes and the C callback prefixes that implement them.
#
#   COMMANDS = [ (full_path, methods, callback_base), ... ]
#
# The generator appends "OnGet"/"OnDel"/"OnPut" to `callback_base` for each
# method. The callback itself is defined in the server's own source.

COMMANDS = [
    ("/internals",                        ("GET",), "UfsrvConsoleInternals"),
    ("/internals/config",                 ("GET",), "UfsrvConsoleInternalsConfig"),
    ("/internals/config/runtime",         ("GET",), "UfsrvConsoleInternalsConfigRuntime"),
    ("/internals/config/config_file",     ("GET",), "UfsrvConsoleInternalsConfigConfigFile"),
    ("/internals/threads",                ("GET",), "UfsrvConsoleInternalsThreads"),
    ("/internals/hashtables",             ("GET",), "UfsrvConsoleInternalsHashtables"),
    ("/internals/hashtables/sessions",    ("GET",), "UfsrvConsoleInternalsHashtablesSessions"),
    ("/internals/recyclers",              ("GET",), "UfsrvConsoleInternalsRecyclers"),
    ("/internals/recyclers/sessions",     ("GET",), "UfsrvConsoleInternalsRecyclersSessions"),
]
