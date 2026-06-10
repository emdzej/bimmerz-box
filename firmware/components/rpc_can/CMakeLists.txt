idf_component_register(
    SRCS "src/rpc_can.c"
    INCLUDE_DIRS "include"
    REQUIRES log esp_http_server json mbedtls board
    PRIV_REQUIRES driver esp_driver_gpio esp_timer
)
