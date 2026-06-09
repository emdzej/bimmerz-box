#pragma once

// In-flash configuration web app. Always available — survives a missing
// or corrupt SD card. Registers:
//   GET  /admin/           — single-page HTML config UI (EMBED_FILES)
//   GET  /api/info         — firmware version, chip ID, uptime, free heap
//   GET  /api/config       — current NVS-backed config snapshot
//   POST /api/config       — update config (partial JSON merged into NVS)
//   POST /api/restart      — esp_restart()
//   POST /api/factory-reset — nvs_flash_erase() then restart
//
// All handlers run on the shared httpd from http_static_handle().

#include "esp_err.h"

esp_err_t admin_ui_start(void);
