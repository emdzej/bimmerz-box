idf_component_register(
    SRCS "src/obd_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus board driver
)
