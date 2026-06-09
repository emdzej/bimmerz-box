idf_component_register(
    SRCS "src/transport_doip.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus obd_hal ediabasx_platform
    PRIV_REQUIRES lwip
)
