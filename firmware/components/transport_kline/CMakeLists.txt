idf_component_register(
    SRCS "src/transport_kline.c" "src/kline_proto.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus obd_hal ediabasx_platform board ediabasx-embedded
    PRIV_REQUIRES driver esp_driver_uart esp_timer
)
