#pragma once

// Master side of the ESP-Hosted link to the ESP32-C6 Wi-Fi/BLE
// co-processor. Will pull in the esp_hosted managed component once
// implementation begins.

#include "esp_err.h"

esp_err_t c6_host_init(void);
