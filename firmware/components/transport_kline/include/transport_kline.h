#pragma once

// L9637D-backed K-line transport for the ediabasx VM.
//
// On boot the UART driver is installed (10400 baud 8N1 default, no flow
// control) and the transport vtable is wired up. Higher layers — the
// ediabasx VM or `klineProbe` — can call `transport_kline_send_raw()`
// to push bytes and collect RX (with the half-duplex echo intact, so
// the caller knows what was its own TX vs ECU response).

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ediabasx/transport.h"

esp_err_t transport_kline_init(void);

// Returns the ediabasx transport vtable so ediabasx_platform can attach
// it to the VM. NULL if init failed.
edxn_transport_t *transport_kline_vtable(void);

// Low-level synchronous probe: flush RX, write `tx_len` bytes, then read
// up to `rx_cap` bytes within `timeout_ms` of TX completion. Returns
// the number of bytes received in *rx_len (which includes the K-line
// echo of the TX bytes). Used by the debug `klineProbe` RPC and
// internally by the transport's `send`.
esp_err_t transport_kline_raw(const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_cap, size_t *rx_len,
                              uint32_t timeout_ms);

// Set the K-line UART baud rate. Default is 10400 (KWP2000). KWP
// variants use 9600 / 10400 / 38400; ISO-9141 uses 10400 after the
// 5-baud init handshake.
esp_err_t transport_kline_set_baud(uint32_t baud);

// Switch UART parity at runtime. DS2 / Concept-1 (older BMW ECUs)
// use 8E1; KWP2000 uses 8N1. `parity` accepts: "none" / "even" / "odd".
esp_err_t transport_kline_set_parity(const char *parity);

// Toggle the UART peripheral's internal TX→RX loopback. Diagnostic
// only — proves the UART driver itself is healthy without involving
// the L9637D / wiring.
esp_err_t transport_kline_set_loopback(bool enabled);

// Standalone wire-level check that bypasses UART completely: detaches
// the UART pins, drives GPIO_KLINE_TX high then low as GPIO outputs,
// and samples GPIO_KLINE_RX between each step. With L9637D powered and
// wired correctly, RX should mirror TX (high→high, low→low). The UART
// is restored before returning so subsequent klineProbe calls keep
// working.
typedef struct {
    int rx_when_tx_high;   // sampled GPIO_KLINE_RX after TX=1 (idle ~3.3V via pull-up)
    int rx_when_tx_low;    // sampled GPIO_KLINE_RX after TX=0 (L9637D pulls K-line low)
    bool loop_ok;          // true iff high->high AND low->low
} transport_kline_wire_result_t;

esp_err_t transport_kline_wire_test(transport_kline_wire_result_t *out);

// Detach the UART pins, drive GPIO_KLINE_TX to `level` (0 or 1) for
// `hold_ms` milliseconds while sampling GPIO_KLINE_RX, then restore the
// UART. Long-hold version of wire_test that lets you probe with a
// multimeter / scope while the line is held steady.
esp_err_t transport_kline_hold_tx(int level, uint32_t hold_ms,
                                   int *out_rx_during);

// 5-baud slow init: detach UART, bit-bang `value` on GPIO_KLINE_TX as
// [start=0, b0..b7 LSB-first, stop=1] each held for `bit_time_ms`
// (the ISO 9141 / KWP slow-init default is 200 ms — pass 0 to use it).
// Restores the UART driver at its current baud/parity before returning
// and drains stale RX so subsequent reads only see ECU bytes.
//
// Port of TS send5BaudInit (no-pulse / direct-GPIO branch).
esp_err_t transport_kline_send_5baud(uint8_t value, uint32_t bit_time_ms);

// KWP2000 fast init: detach UART, drive TX low for `break_ms`, idle high
// for `idle_ms`, restore UART. Pass 0 for either to use the defaults
// (25 ms / 25 ms per the TS constants).
//
// Port of TS sendFastInit (no-pulse / direct-GPIO branch).
esp_err_t transport_kline_send_fast_init(uint32_t break_ms, uint32_t idle_ms);

// Reads up to `cap` bytes from K-line within `timeout_ms`. Returns
// ESP_OK with *out_len set to the byte count (0 if nothing arrived
// before the timeout). Used to collect key bytes after slow init.
esp_err_t transport_kline_read(uint8_t *buf, size_t cap, size_t *out_len,
                               uint32_t timeout_ms);

// Write `len` bytes to K-line and consume our own half-duplex echo
// (so the caller's next read sees only ECU bytes). Returns ESP_OK if
// the echo matched what we sent; ESP_FAIL otherwise. Echo timeout is
// 250 ms, matching the EchoTimeout constant in ds2.ts.
esp_err_t transport_kline_write_and_consume_echo(const uint8_t *tx, size_t len);

// Full DS2 transaction (one send / receive cycle). Mirrors the TS
// Ds2Session.sendRequest algorithm: honour regen time, optionally
// append XOR, send (with optional inter-byte delay), consume echo,
// read header per concept rules, read tail, verify XOR.
//
// `cfg->concept` must be 0x0001 / 0x0005 / 0x0006.
// `req` is the BEST2 send buffer (no XOR unless cfg->checksum_by_user).
// On success returns the full received telegram (including XOR) via
// `resp` / *resp_len; on framing / checksum failures returns ESP_FAIL
// with whatever bytes arrived left in *resp_len for diagnosis.
typedef struct {
    uint16_t concept;
    uint32_t baud_rate;
    uint32_t timeout_std_ms;
    uint32_t tel_end_ms;
    uint32_t regen_time_ms;
    uint32_t inter_byte_ms;       // 0 = no per-byte delay
    bool checksum_by_user;        // caller has appended XOR
    bool checksum_no_check;       // skip RX XOR verification
} transport_kline_ds2_cfg_t;

esp_err_t transport_kline_ds2_transact(const transport_kline_ds2_cfg_t *cfg,
                                        const uint8_t *req, size_t req_len,
                                        uint8_t *resp, size_t resp_cap,
                                        size_t *resp_len);

// Full BMW-FAST (KWP2000) transaction. Mirrors TS Kwp2000Session.sendRequest:
// honour P3 regen, build [hdr ecu tester payload checksum] frame, send,
// consume echo, read header (3 or 4 byte) → total length → tail →
// verify checksum.
//
// Returns the full received telegram including checksum in `resp` /
// *resp_len.
typedef struct {
    uint8_t  ecu_address;
    uint8_t  tester_address;
    uint32_t baud_rate;           // KWP typically 10400
    uint32_t timeout_p2_ms;       // first-byte read budget
    uint32_t timeout_p1_ms;       // inter-byte read budget
    uint32_t regen_p3_ms;         // gap between transactions
} transport_kline_kwp_cfg_t;

esp_err_t transport_kline_kwp_transact(const transport_kline_kwp_cfg_t *cfg,
                                        const uint8_t *payload, size_t payload_len,
                                        uint8_t *resp, size_t resp_cap,
                                        size_t *resp_len);
