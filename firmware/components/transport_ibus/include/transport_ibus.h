#pragma once

// TH3122-backed BMW IBUS transport. Single-wire UART with bus
// arbitration via collision detection.

#include "esp_err.h"

esp_err_t transport_ibus_init(void);
