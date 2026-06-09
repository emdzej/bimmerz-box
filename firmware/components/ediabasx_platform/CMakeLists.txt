idf_component_register(
    SRCS "src/ediabasx_platform.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus storage ediabasx-embedded
    PRIV_REQUIRES transport_kline
)
