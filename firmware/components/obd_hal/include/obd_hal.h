#pragma once

// OBD HAL — the only code that may drive the K-line / L-line / DoIP
// GPIOs. Enforces the pin-8 mutual exclusion in software in addition
// to the hardware interlock gate. See docs/firmware.md §9 and
// docs/hardware.md §8.

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    OBD_MODE_IDLE,           // all transceivers off — safe default
    OBD_MODE_KLINE7,         // K-line on OBD pin 7 only
    OBD_MODE_KLINE8,         // K-line on OBD pin 8 only
    OBD_MODE_KLINE_BOTH,     // K-line on pins 7 and 8 (E-series default)
    OBD_MODE_DOIP_ACTIVE,    // DoIP +12 V activation on pin 8 (F/G chassis)
    OBD_MODE_CAN_ONLY,       // CAN active, K-line and DoIP both off
} obd_mode_t;

esp_err_t obd_hal_init(void);
esp_err_t obd_set_mode(obd_mode_t mode);
obd_mode_t obd_get_mode(void);

// Output-only 5-baud KWP wake pulse on OBD pin 15. Blocks until the
// pattern has been clocked out.
esp_err_t obd_lline_pulse(uint32_t bit_pattern, int bits, int baud_rate);
