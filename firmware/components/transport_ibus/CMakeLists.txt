idf_component_register(
    SRCS "src/transport_ibus.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus ediabasx_platform
    PRIV_REQUIRES driver
)
