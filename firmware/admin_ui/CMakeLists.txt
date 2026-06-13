idf_component_register(
    SRCS "src/admin_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus http_static esp_http_server json nvs_flash esp_app_format esp_timer
    EMBED_FILES "web/settings.html" "web/fflate.min.js"
)
