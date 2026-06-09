idf_component_register(
    SRCS "src/transport_can.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus ediabasx_platform
    PRIV_REQUIRES driver
)
