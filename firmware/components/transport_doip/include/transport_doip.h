#pragma once

// DoIP (ISO 13400) transport over Ethernet for F/G chassis. Drives
// the obd_hal into DOIP_ACTIVE before opening the gateway socket.

#include "esp_err.h"

esp_err_t transport_doip_init(void);
