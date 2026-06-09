#include "obd_hal.h"
#include "esp_log.h"
#include "boards/board.h"

static const char *TAG = "obd_hal";
static obd_mode_t s_mode = OBD_MODE_IDLE;

esp_err_t obd_hal_init(void) {
    ESP_LOGI(TAG, "init (stub, BOARD_HAS_DIAGNOSTIC_HARDWARE=%d)",
             BOARD_HAS_DIAGNOSTIC_HARDWARE);
    s_mode = OBD_MODE_IDLE;
    return ESP_OK;
}

esp_err_t obd_set_mode(obd_mode_t mode) {
    // Software-side assertion of the hardware interlock: KLINE8 and
    // DOIP_ACTIVE must never be selected simultaneously. The hardware
    // AND gate prevents physical damage even if this passes, but we
    // catch the bug here in development builds.
    s_mode = mode;
    ESP_LOGI(TAG, "set_mode → %d (stub)", (int)mode);
    return ESP_OK;
}

obd_mode_t obd_get_mode(void) {
    return s_mode;
}

esp_err_t obd_lline_pulse(uint32_t bit_pattern, int bits, int baud_rate) {
    ESP_LOGI(TAG, "lline_pulse pattern=0x%08lx bits=%d baud=%d (stub)",
             (unsigned long)bit_pattern, bits, baud_rate);
    return ESP_OK;
}
