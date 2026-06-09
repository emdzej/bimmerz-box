#include "log_bus.h"
#include "esp_log.h"

static const char *TAG = "log_bus";

esp_err_t log_bus_init(void) {
    ESP_LOGI(TAG, "init (stub)");
    return ESP_OK;
}
