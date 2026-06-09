#pragma once

// USB Mass-Storage class device exposing the SD card to a connected
// host. Used as a no-Wi-Fi asset OTA path. See docs/firmware.md §11.4.

#include "esp_err.h"

esp_err_t usb_msc_start(void);
esp_err_t usb_msc_stop(void);
