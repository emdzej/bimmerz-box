idf_component_register(
    SRCS "src/rpc_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES log esp_http_server json mbedtls transport_kline board
    PRIV_REQUIRES driver esp_driver_uart
)
