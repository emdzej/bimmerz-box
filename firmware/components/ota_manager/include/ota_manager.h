#pragma once

// P4 firmware OTA — streams a new application image into the inactive
// `ota_X` partition, then reboots into it. Boot-time rollback is wired
// via `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`: a new image stays
// `PENDING_VERIFY` until this component marks it valid (60s after a
// successful boot). Any reset inside that window triggers rollback to
// the previously-known-good slot — so a brick from a bad upload is
// recoverable without USB-JTAG.
//
// Endpoint registered on http_static's shared httpd:
//   POST /settings/ota/firmware
//     Content-Type: application/octet-stream
//     body:         raw .bin (the same artefact `idf.py build` produces)
//     response:     { ok, version, partition, size } on success
//                   { ok: false, error } on failure
//   On success the device reboots ~500 ms after the response flushes.
//
// Future channels (C6 firmware, asset bundles) live in this component
// too — see docs/firmware.md.

#include "esp_err.h"

esp_err_t ota_manager_init(void);
