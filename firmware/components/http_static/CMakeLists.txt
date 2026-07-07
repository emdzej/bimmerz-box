idf_component_register(
    SRCS "src/http_static.c"
    INCLUDE_DIRS "include"
    # `rpc_uart` / `rpc_can` are pulled in so http_static's close_fn
    # can dispatch to their session-release hooks on socket drop.
    # Priv-requires — the rpc_* components already require
    # http_static (they register URI handlers with our server), so a
    # public REQUIRES here would create a dependency cycle. Private
    # REQUIRES lets us reach the headers from the .c without exposing
    # them to consumers of http_static.
    REQUIRES log log_bus storage esp_http_server lwip
    PRIV_REQUIRES rpc_uart rpc_can
    EMBED_FILES "web/welcome.html"
)
