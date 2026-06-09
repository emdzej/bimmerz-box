idf_component_register(
    SRCS "src/c6_host.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus espressif__esp_hosted
)
