idf_component_register(
    SRCS "src/http_static.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus storage esp_http_server lwip
    EMBED_FILES "web/welcome.html"
)
