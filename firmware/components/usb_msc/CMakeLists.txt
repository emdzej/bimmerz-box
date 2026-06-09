idf_component_register(
    SRCS "src/usb_msc.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus storage
    # Real implementation will add:
    #   PRIV_REQUIRES esp_tinyusb
)
