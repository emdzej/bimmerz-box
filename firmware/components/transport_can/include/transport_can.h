#pragma once

// TJA1051T/3-backed CAN transport via the P4's TWAI controller.
// Implements ISO-TP segmentation for EDIABAS-style diagnostics.

#include "esp_err.h"

esp_err_t transport_can_init(void);
