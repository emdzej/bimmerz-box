idf_component_register(
    SRCS "src/wifi_ap.c"
    INCLUDE_DIRS "include"
    REQUIRES log log_bus c6_host esp_event esp_netif esp_wifi
    PRIV_REQUIRES nvs_flash dns_server
)
