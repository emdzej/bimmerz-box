#pragma once

// Pure-logic K-line protocol primitives: BMW-FAST (KWP2000) and DS1/DS2/
// Concept-1 framing, plus the matching checksums.
//
// Ported verbatim from the TypeScript reference in
// ediabasx/packages/interface-serial/src/kdcan/:
//   - checksum.ts         → kline_bmw_fast_checksum
//   - ds2.ts              → kline_xor_checksum, kline_ds2_answer_len,
//                           kline_ds2_tel_length
//   - kwp2000.ts          → kline_build_bmw_fast_telegram,
//                           kline_bmw_fast_total_length,
//                           kline_bmw_fast_payload_window
//
// No I/O here — these are used by transport_kline (and any RPC handler)
// to frame bytes before sending them down the UART and to parse what
// comes back. The init pulses (5-baud slow init, fast init) live in
// transport_kline.h because they need the UART/GPIO peripherals.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Constants (mirror constants.ts / slowInit.ts / fastInit.ts) ---------
#define KLINE_FIVE_BAUD_BIT_TIME_MS    200u
#define KLINE_FAST_INIT_BREAK_MS       25u
#define KLINE_FAST_INIT_IDLE_MS        25u
#define KLINE_KWP_KEYBYTE_KWP2000      0x8fu

// Worst-case BMW-FAST framing overhead (4-byte header + 1-byte checksum).
#define KLINE_BMW_FAST_TELEGRAM_OVERHEAD 5u

// ---- Checksums -----------------------------------------------------------

// Sum of bytes [offset .. offset+length-1] mod 256.
// Port of TS calcChecksumBmwFast.
uint8_t kline_bmw_fast_checksum(const uint8_t *data, size_t offset, size_t length);

// XOR of bytes [offset .. offset+length-1].
// Port of TS calcChecksumXor.
uint8_t kline_xor_checksum(const uint8_t *data, size_t offset, size_t length);

// ---- BMW-FAST (KWP2000) framing -----------------------------------------

// Builds a BMW-FAST request around `payload` and writes it into `out`.
// Returns the total written length, or 0 on overflow / payload too large
// (the builder never emits the 6-byte extended header — payloads > 0xFF
// are rejected, matching the TS builder).
//
// `out_cap` must be >= payload_len + KLINE_BMW_FAST_TELEGRAM_OVERHEAD.
//
// Port of TS buildBmwFastTelegram.
size_t kline_build_bmw_fast_telegram(const uint8_t *payload, size_t payload_len,
                                     uint8_t ecu_address, uint8_t tester_address,
                                     uint8_t *out, size_t out_cap);

// Given some received bytes, returns the total telegram length (header +
// payload + checksum) the frame should reach.
//
// Returns 0 if the buffer doesn't yet contain enough bytes to compute the
// length (caller should read more and try again).
//
// Port of TS calcBmwFastLength.
size_t kline_bmw_fast_total_length(const uint8_t *received, size_t received_len);

// Locates the payload window inside a fully-received BMW-FAST telegram.
// On success returns true and fills *out_offset / *out_length; otherwise
// returns false (buffer too short or invalid header).
//
// Port of TS getBmwFastDataWindow.
bool kline_bmw_fast_payload_window(const uint8_t *received, size_t received_len,
                                   size_t *out_offset, size_t *out_length);

// ---- DS1 / DS2 / Concept-1 framing --------------------------------------

// CommAnswerLen[0..1] tuple from C# EdInterfaceObd:
//   header_len_signed < 0  → byte at offset (-header_len_signed) is total length
//   header_len_signed >= 0 → fixed total length
//   length_addend is added to the length byte (rarely non-zero on BMW).
typedef struct {
    int16_t header_len_signed;
    int16_t length_addend;
} kline_ds2_answer_len_t;

// Fills *out for the given concept ID. Returns true if concept is
// recognised (0x0001 / 0x0005 / 0x0006).
//
// Port of TS answerLenForConcept.
bool kline_ds2_answer_len(uint16_t concept, kline_ds2_answer_len_t *out);

// Computes the total telegram length given the already-received header
// bytes. Returns 0 if the buffer is too short to determine the length.
//
// Port of TS telLengthDs2.
size_t kline_ds2_tel_length(const uint8_t *received_header,
                            size_t received_header_len,
                            const kline_ds2_answer_len_t *answer_len);

#ifdef __cplusplus
}
#endif
