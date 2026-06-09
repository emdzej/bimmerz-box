#pragma once

// Wi-Fi Access Point bring-up. Runs through the C6 co-processor over
// ESP-Hosted (see c6_host). Owns AP SSID/password from NVS.

#include "esp_err.h"

esp_err_t wifi_ap_init(void);
