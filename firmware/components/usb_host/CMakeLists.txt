idf_component_register(
    SRCS "src/usb_host.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus usb espressif__usb_host_cdc_acm espressif__usb_host_ftdi_vcp
)
