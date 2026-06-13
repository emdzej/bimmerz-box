idf_component_register(
    SRCS "src/ota_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus storage c6_host http_static esp_http_server
             app_update esp_app_format esp_timer json
)
