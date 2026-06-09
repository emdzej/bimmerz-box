#pragma once

// JSON-RPC 2.0 server over WebSocket. Mirrors the protocol exposed by
// ~/Projects/my/ediabasx/packages/ediabasx-server/src/ediabas-server.ts
// so the existing @emdzej/ediabasx-client works unchanged. See
// docs/firmware.md §7.

#include "esp_err.h"

esp_err_t jsonrpc_start(void);
