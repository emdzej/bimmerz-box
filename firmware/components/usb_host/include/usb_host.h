#pragma once

// Enumerate connected USB host devices behind the onboard hub IC.
// Logs VID/PID + Manufacturer/Product/Serial strings for each device
// that attaches. Used as a probe before wiring class-specific drivers
// (FTDI, CH340, CDC-ACM, MSC) — once we know what's plugged in, we
// know which managed component to pull next.

#include "esp_err.h"

esp_err_t usb_host_start(void);
