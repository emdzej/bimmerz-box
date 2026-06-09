#pragma once

// SD card mount management. Owns the FATFS instance backing
// /sdcard for the rest of the firmware.

#include "esp_err.h"

esp_err_t storage_mount(void);
esp_err_t storage_unmount(void);

// Returns the FATFS mount point (e.g. "/sdcard"). Stable across calls.
const char *storage_mount_point(void);
