// Bimmerz Box — boot orchestration.
//
// See docs/firmware.md §3 for the staged boot sequence and the LED
// pattern each stage produces. This file only sequences the stages;
// the actual work lives in components/.

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "boards/board.h"

#include "admin_ui.h"
#include "c6_host.h"
#include "ediabasx_platform.h"
#include "http_static.h"
#include "jsonrpc.h"
#include "log_bus.h"
#include "obd_hal.h"
#include "ota_manager.h"
#include "rpc_can.h"
#include "rpc_uart.h"
#include "storage.h"
#include "usb_host.h"
#include "wifi_ap.h"

static const char *TAG = "app";

static void init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void) {
    ESP_LOGI(TAG, "Bimmerz Box boot");

    // Stage 2: log bus + NVS
    ESP_ERROR_CHECK(log_bus_init());
    init_nvs();

    // Stage 3: SD card
    ESP_ERROR_CHECK(storage_mount());

    // Stage 5: OBD HAL — start in IDLE so no transceiver is ever
    // active before firmware has chosen a mode.
    ESP_ERROR_CHECK(obd_hal_init());
    ESP_ERROR_CHECK(obd_set_mode(OBD_MODE_IDLE));

    // Stage 6: C6 link via ESP-Hosted
    ESP_ERROR_CHECK(c6_host_init());

    // Stage 7: Wi-Fi AP
    ESP_ERROR_CHECK(wifi_ap_init());

    // Stage 8: ediabasx VM
    ESP_ERROR_CHECK(ediabasx_platform_init());

    // Stage 9: HTTP + WS surface. Register specific routes (jsonrpc /rpc,
    // admin /admin/+/api/*) BEFORE installing http_static's wildcard /*
    // fallback — esp_http_server's wildcard matcher returns the first
    // registered match.
    ESP_ERROR_CHECK(http_static_start());
    ESP_ERROR_CHECK(jsonrpc_start());
    ESP_ERROR_CHECK(rpc_uart_start(http_static_handle()));
    ESP_ERROR_CHECK(rpc_can_start(http_static_handle()));
    ESP_ERROR_CHECK(admin_ui_start());
    ESP_ERROR_CHECK(ota_manager_init());
    ESP_ERROR_CHECK(http_static_install_fallback());

    // USB host stack last so the network/HTTP plane is up first. Devices
    // attaching after this point will log their descriptors to the
    // console (and eventually be claimed by class drivers).
    ESP_ERROR_CHECK(usb_host_start());

    ESP_LOGI(TAG, "Bimmerz Box ready");
}
