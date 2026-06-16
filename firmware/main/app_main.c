// Bimmerz Box — boot orchestration.
//
// See docs/firmware.md §3 for the staged boot sequence and the LED
// pattern each stage produces. This file only sequences the stages;
// the actual work lives in components/.
//
// Failure-handling policy
// -----------------------
// Three tiers per stage:
//
//   * Essential — ESP_ERROR_CHECK. Failure is fatal because nothing
//     downstream can work without it (logging, NVS, OBD interlock,
//     the HTTP server itself). A boot loop here is correct: the
//     bootloader's rollback-on-WDT logic will revert to the previous
//     known-good slot.
//
//   * Network bring-up (C6 link, Wi-Fi AP) — bounded retry. Cold-boot
//     SDIO handshakes occasionally glitch; retrying buys us robustness
//     against transient failures without a forever bootloop. Eventual
//     give-up is fatal (no UI is possible without Wi-Fi).
//
//   * Auxiliary services (diag VM, JSON-RPC, settings UI, OTA, USB) —
//     log-and-continue. A bad route registration or transient
//     componentFile-system error shouldn't brick the device. The
//     dongle still serves whatever the rest of the stack brought up,
//     and the user can recover via the surviving UI / re-flash.

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

// Bounded retry for stages whose first attempt occasionally fails on
// cold boot (SDIO handshake glitches, slave-firmware bring-up delays).
// Five attempts with a backoff is plenty in practice; total worst-case
// wait is ~7.5 s. Returns the last error if every attempt failed.
static esp_err_t retry_init(const char *name, esp_err_t (*fn)(void)) {
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 5; attempt++) {
        err = fn();
        if (err == ESP_OK) {
            if (attempt > 1) {
                ESP_LOGW(TAG, "%s ok on attempt %d", name, attempt);
            }
            return ESP_OK;
        }
        ESP_LOGW(TAG, "%s attempt %d/5 failed: %s",
                 name, attempt, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(500 * attempt));  // 0.5, 1.0, 1.5, 2.0 s
    }
    return err;
}

// Log-and-continue wrapper for auxiliary stages. Returns true if the
// init succeeded — caller may use that to skip downstream optional
// work, but otherwise boot proceeds regardless.
static bool try_init(const char *name, esp_err_t err) {
    if (err == ESP_OK) return true;
    ESP_LOGE(TAG, "%s failed: %s — continuing without it",
             name, esp_err_to_name(err));
    return false;
}

void app_main(void) {
    ESP_LOGI(TAG, "Bimmerz Box boot");

    // Stage 2: log bus + NVS — essential.
    ESP_ERROR_CHECK(log_bus_init());
    init_nvs();

    // Stage 3: SD card — essential (settings UI, apps, SGBDs).
    ESP_ERROR_CHECK(storage_mount());

    // Stage 5: OBD HAL — essential. Start in IDLE so no transceiver is
    // ever active before firmware has chosen a mode.
    ESP_ERROR_CHECK(obd_hal_init());
    ESP_ERROR_CHECK(obd_set_mode(OBD_MODE_IDLE));

    // Stage 6: C6 link via ESP-Hosted — retry on cold-boot SDIO glitches.
    ESP_ERROR_CHECK(retry_init("c6_host_init", c6_host_init));

    // Stage 7: Wi-Fi AP — depends on C6; same retry policy.
    ESP_ERROR_CHECK(retry_init("wifi_ap_init", wifi_ap_init));

    // Stage 8: ediabasx VM — auxiliary. Without it diagnostics don't
    // work but the dongle still serves its web UI.
    try_init("ediabasx_platform_init", ediabasx_platform_init());

    // Stage 9: HTTP + WS surface. http_static itself is essential —
    // without it there's no UI at all. The handlers registered on top
    // (jsonrpc, rpc_uart, rpc_can, admin_ui, ota_manager) are
    // auxiliary; a failed registration just drops that endpoint.
    // The fallback wildcard must be installed last (esp_http_server's
    // matcher returns the first registered match).
    ESP_ERROR_CHECK(http_static_start());
    try_init("jsonrpc_start",  jsonrpc_start());
    try_init("rpc_uart_start", rpc_uart_start(http_static_handle()));
    try_init("rpc_can_start",  rpc_can_start(http_static_handle()));
    try_init("admin_ui_start", admin_ui_start());
    try_init("ota_manager_init", ota_manager_init());
    ESP_ERROR_CHECK(http_static_install_fallback());

    // USB host stack last so the network/HTTP plane is up first.
    // Auxiliary — peripherals attaching later are nice-to-have.
    try_init("usb_host_start", usb_host_start());

    ESP_LOGI(TAG, "Bimmerz Box ready");
}
