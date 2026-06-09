#include "c6_host.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "c6_host";

esp_err_t c6_host_init(void) {
    // In ESP-Hosted 1.4.x the SDIO transport is lazily brought up on the
    // first esp_wifi_*() call. Touching the host API here (e.g. asking
    // for the slave firmware version) triggers a TX before the link
    // exists, which crashes with a tlsf double-free inside the error
    // path. So this stage is a no-op — wifi_ap_init() lights it up.
    ESP_LOGI(TAG, "C6 link bring-up deferred to wifi_ap (ESP-Hosted lazy init)");
    return ESP_OK;
}
