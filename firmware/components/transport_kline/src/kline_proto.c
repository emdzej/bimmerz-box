#include "kline_proto.h"

#include <string.h>

// All functions in this file are 1:1 ports of the TypeScript reference at
// ediabasx/packages/interface-serial/src/kdcan/. Each function carries the
// matching TS function name in its banner so future divergences are easy
// to spot.

// ---- Checksums -----------------------------------------------------------

// Port of checksum.ts: calcChecksumBmwFast
uint8_t kline_bmw_fast_checksum(const uint8_t *data, size_t offset, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        // uint8_t wrap is the C equivalent of TS `& 0xff`.
        sum = (uint8_t)(sum + data[offset + i]);
    }
    return sum;
}

// Port of ds2.ts: calcChecksumXor
uint8_t kline_xor_checksum(const uint8_t *data, size_t offset, size_t length) {
    uint8_t x = 0;
    for (size_t i = 0; i < length; ++i) {
        x = (uint8_t)(x ^ data[offset + i]);
    }
    return x;
}

// ---- BMW-FAST framing ---------------------------------------------------

// Port of kwp2000.ts: buildBmwFastTelegram
size_t kline_build_bmw_fast_telegram(const uint8_t *payload, size_t payload_len,
                                     uint8_t ecu_address, uint8_t tester_address,
                                     uint8_t *out, size_t out_cap) {
    if (!out) return 0;
    // The TS builder only emits 3- or 4-byte headers; payloads beyond
    // 0xFF would require the 6-byte extended header (header[3]=0,
    // length at header[4..5]), which the TS code parses but never emits.
    if (payload_len > 0xFFu) return 0;

    size_t header_len = (payload_len > 0x3F) ? 4u : 3u;
    size_t total = header_len + payload_len + 1u;  // + 1-byte checksum
    if (total > out_cap) return 0;

    if (header_len == 4) {
        out[0] = 0x80;
        out[1] = ecu_address;
        out[2] = tester_address;
        out[3] = (uint8_t)payload_len;
    } else {
        out[0] = (uint8_t)(0x80u | (payload_len & 0x3Fu));
        out[1] = ecu_address;
        out[2] = tester_address;
    }
    if (payload && payload_len > 0) {
        memcpy(out + header_len, payload, payload_len);
    }
    out[total - 1] = kline_bmw_fast_checksum(out, 0, total - 1);
    return total;
}

// Port of kwp2000.ts: calcBmwFastLength
size_t kline_bmw_fast_total_length(const uint8_t *received, size_t received_len) {
    if (!received || received_len < 1) return 0;
    size_t len_field = received[0] & 0x3Fu;

    if (len_field == 0) {
        // 4-byte header form or 6-byte extended form: need at least 4 bytes
        // to read header[3]; if header[3] is also 0 we need 6 bytes total.
        if (received_len < 4) return 0;
        if (received[3] == 0) {
            if (received_len < 6) return 0;
            return ((size_t)received[4] << 8) + (size_t)received[5] + 6u + 1u;
        }
        return (size_t)received[3] + 4u + 1u;
    }
    return len_field + 3u + 1u;
}

// Port of kwp2000.ts: getBmwFastDataWindow
bool kline_bmw_fast_payload_window(const uint8_t *received, size_t received_len,
                                   size_t *out_offset, size_t *out_length) {
    if (!received || received_len < 3) return false;
    size_t len_field = received[0] & 0x3Fu;

    size_t offset, length;
    if (len_field == 0) {
        if (received_len < 4) return false;
        if (received[3] == 0) {
            if (received_len < 6) return false;
            offset = 6;
            length = ((size_t)received[4] << 8) + (size_t)received[5];
        } else {
            offset = 4;
            length = received[3];
        }
    } else {
        offset = 3;
        length = len_field;
    }
    if (received_len < offset + length + 1u) return false;  // need checksum byte too
    if (out_offset) *out_offset = offset;
    if (out_length) *out_length = length;
    return true;
}

// ---- DS1 / DS2 / Concept-1 framing --------------------------------------

// Port of ds2.ts: answerLenForConcept
bool kline_ds2_answer_len(uint16_t concept, kline_ds2_answer_len_t *out) {
    if (!out) return false;
    switch (concept) {
        case 0x0001:
            out->header_len_signed = -2;
            out->length_addend = 0;
            return true;
        case 0x0005:
        case 0x0006:
            out->header_len_signed = -1;
            out->length_addend = 0;
            return true;
        default:
            return false;
    }
}

// Port of ds2.ts: telLengthDs2
size_t kline_ds2_tel_length(const uint8_t *received_header,
                            size_t received_header_len,
                            const kline_ds2_answer_len_t *answer_len) {
    if (!received_header || !answer_len) return 0;
    int16_t header_len_signed = answer_len->header_len_signed;
    int16_t addend = answer_len->length_addend;
    if (header_len_signed >= 0) {
        return (size_t)header_len_signed;
    }
    size_t length_byte_offset = (size_t)(-header_len_signed);
    // TS check: `if (receivedHeader.length < offset) return 0` — so the
    // caller must have at least `offset + 1` bytes for the read to be in
    // bounds. We enforce the tighter (safer) check.
    if (received_header_len < length_byte_offset + 1u) return 0;
    return (size_t)received_header[length_byte_offset] + (size_t)(int)addend;
}
