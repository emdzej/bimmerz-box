idf_component_register(
    SRCS "src/jsonrpc.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus ediabasx_platform http_static esp_http_server json transport_kline
)
