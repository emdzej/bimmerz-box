#pragma once

// Manages the three OTA channels: P4 firmware, C6 firmware, asset
// bundles. Exposes admin endpoints via the http_static server. See
// docs/firmware.md §11.

#include "esp_err.h"

esp_err_t ota_manager_init(void);
