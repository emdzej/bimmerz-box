#pragma once

// Centralized log sink. Records pushed by any task are fanned out to:
//   - the UART console (USB-C CDC-ACM in production),
//   - an in-PSRAM ring buffer,
//   - JSON-RPC log notifications to subscribed WS clients.
//
// See docs/firmware.md §13.

#include "esp_err.h"

esp_err_t log_bus_init(void);
