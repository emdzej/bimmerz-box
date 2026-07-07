#pragma once

// Generic UART RPC at /rpc/uart/<n> — exposes a thin JSON-RPC pipe over
// WebSocket to one of the dongle's UART instances. Used by apps that
// need direct K-line access (nfsx flasher) rather than the ediabasx VM
// (which owns the higher-level SGBD execution path).
//
// Index map (today):
//   /0 → K-line (BOARD_KLINE_UART_NUM, via the same UART driver that
//         transport_kline drives — exclusive arbitration between this
//         endpoint and the ediabasx VM is enforced via
//         `rpc_uart_kline_locked()`).
//   /1..n → not present yet (returns "uart_not_present").
//
// Methods (request/response, all JSON-RPC 2.0):
//   uart.open      { exclusive?, baud?, parity?, dataBits?, stopBits?, consumeEcho? }
//   uart.configure { baud?, parity?, dataBits?, stopBits?, consumeEcho? }
//   uart.write     { data: "<base64>" }
//   uart.transact  { data: "<base64>", readMs, readBytes? }
//   uart.slowInit  { value, bitTimeMs?, baudAfter?, parityAfter? }
//   uart.fastInit  { breakMs?, idleMs? }
//   uart.close     {}
//
// Notifications (server → holder only):
//   uart.rx        { data: "<base64>" }   — streamed RX while open
//   uart.revoked   { by: "<peer-ip>" }    — cooperative open kicked us off
//   uart.error     { message }            — wire-level failures
//
// Arbitration: each index has at most one holder. While index 0 is held,
// ediabasx `job` requests fail with EDXN_ERR_TRANSPORT (same physical
// bus). Holder is tracked by WS fd; lost on disconnect → auto-release.

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t rpc_uart_start(httpd_handle_t server);

// Returns true if a /rpc/uart/0 session currently holds the K-line.
// transport_kline's edxn_transport_t.send checks this and returns
// EDXN_ERR_TRANSPORT so the VM's `job` calls fail cleanly while a
// direct-UART app is using the wire.
bool rpc_uart_kline_locked(void);

// Called from http_static's `close_fn` for every socket close (clean,
// RST, or keep-alive reap). Releases any UART session held by the
// closing fd so a follow-up `uart.open` from another peer doesn't
// see a stale holder. Safe to call before `rpc_uart_start()` — no-ops
// if the session table isn't initialised yet.
void rpc_uart_on_socket_close(int sockfd);
