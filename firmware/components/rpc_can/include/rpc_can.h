#pragma once

// Generic CAN RPC at /rpc/can/<n> — exposes one of the dongle's TWAI
// controllers (each behind a TJA1051T transceiver) as a JSON-RPC pipe
// over WebSocket. Frame-oriented; the wire surface is exactly what the
// TWAI controller emits/accepts (classical CAN, no FD).
//
// Index map (today):
//   /0 → TWAI0 / TJA1051T #1 (BOARD_CAN0_TX/RX_GPIO, BOARD_CAN0_STBY_GPIO)
//   /1 → TWAI1 / TJA1051T #2 (BOARD_CAN1_*)
//   /2 → reserved for the third controller — wiring TBD
// Indices whose pins are (-1) on the active board return "can_not_present".
//
// Methods:
//   can.open      { exclusive?, bitrate, mode? }
//                  bitrate: 25000 | 50000 | 100000 | 125000 | 250000
//                         | 500000 | 800000 | 1000000
//                  mode: "normal" | "listen-only" | "no-ack"
//   can.configure { bitrate?, mode? }   — re-installs the driver
//   can.send      { id, ext?, rtr?, data: "<base64>" }
//   can.sendBatch { frames: [{...}, ...] }
//   can.recover   {}                     — explicit bus-off recovery
//   can.close     {}
//
// Notifications:
//   can.rx     { id, ext, rtr, data: "<base64>", ts }   ts = µs since boot
//   can.state  { state, txErr, rxErr }                  active|passive|bus-off
//   can.error  { code, message }
//   can.revoked { by }
//
// Arbitration mirrors rpc_uart — one holder per controller; cooperative
// open kicks the previous holder (gets `can.revoked`), exclusive open
// returns `bus_busy` if held.

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t rpc_can_start(httpd_handle_t server);
