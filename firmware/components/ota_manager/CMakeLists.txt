idf_component_register(
    SRCS "src/ota_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus storage c6_host
    # Real implementation will add:
    #   PRIV_REQUIRES app_update esp_http_server
)
