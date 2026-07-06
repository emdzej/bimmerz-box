#pragma once

// HTTP server hosting the inpax / ncsx / nfsx / tunex / ediabasx
// single-page apps out of /sdcard/apps/<slug>/, plus the root-namespace
// dashboard hub out of /sdcard/sys/dashboard/. Also exposes /sdcard/data/
// read-only at the /data/ URL prefix for SGBD / DATEN / vehicle data
// files (consumed by the web apps via @emdzej/bimmerz-vfs). Co-listens
// with the JSON-RPC WebSocket endpoint on port 80. See docs/firmware.md §6.

#include "esp_err.h"
#include "esp_http_server.h"

// Two-phase start. `http_static_start()` brings the server up but does
// NOT install the catch-all wildcard yet — other components (admin_ui,
// jsonrpc) get a chance to register their specific routes first.
// `http_static_install_fallback()` then installs the wildcard that
// serves SD-card content for everything else. Calling start without
// install_fallback works but means unknown URIs return 404 immediately.
esp_err_t http_static_start(void);
esp_err_t http_static_install_fallback(void);

// Returns the shared httpd handle so other components (admin_ui,
// jsonrpc) can register additional URI handlers on the same listener.
// Returns NULL before http_static_start() has been called.
httpd_handle_t http_static_handle(void);
