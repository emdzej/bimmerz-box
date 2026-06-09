idf_component_register(
    SRCS "src/storage.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus board
    PRIV_REQUIRES fatfs sdmmc vfs esp_driver_sdmmc
)
