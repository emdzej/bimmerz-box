#include "transport_kline.h"

#include <strings.h>
#include <string.h>

#include "boards/board.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"

#include "kline_proto.h"

// Optional weak hook into rpc_uart — when /rpc/uart/0 holds the K-line,
// the VM's xsend bails so the SGBD's error trap fires instead of our
// transact stomping on the direct-UART app's bytes.
extern bool rpc_uart_kline_locked(void) __attribute__((weak));

static const char *TAG = "transport_kline";

#define K_RX_BUF   1024
#define K_TX_BUF   512
// BMW DS2 (older E36/E38/E39/E46 ECUs) framing: 9600 baud, 8 data bits,
// EVEN parity, 1 stop bit. KWP2000 (newer) uses 10400 8N1; switch at
// runtime via transport_kline_set_baud / transport_kline_set_parity.
#define K_BAUD_DEFAULT 9600
#define K_PARITY_DEFAULT UART_PARITY_EVEN

// Mirror of ds2.ts EchoTimeout (`EdInterfaceObd.cs:131`) — budget for
// reading the half-duplex wire-echo back after a write.
#define K_ECHO_TIMEOUT_MS 250u

static bool s_installed = false;

// ---- low-level UART helpers ----------------------------------------------

static esp_err_t install_uart(uint32_t baud) {
    if (BOARD_KLINE_TX_GPIO < 0 || BOARD_KLINE_RX_GPIO < 0) {
        ESP_LOGW(TAG, "no K-line pins configured for this board — skipping");
        return ESP_OK;
    }

    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = K_PARITY_DEFAULT,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(BOARD_KLINE_UART_NUM, K_RX_BUF, K_TX_BUF,
                                        0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(BOARD_KLINE_UART_NUM, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_set_pin(BOARD_KLINE_UART_NUM,
                       BOARD_KLINE_TX_GPIO, BOARD_KLINE_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin: %s", esp_err_to_name(err));
        return err;
    }
    s_installed = true;
    ESP_LOGI(TAG, "UART%d up on TX=%d RX=%d @ %u baud 8E1 (DS2)",
             (int)BOARD_KLINE_UART_NUM,
             BOARD_KLINE_TX_GPIO, BOARD_KLINE_RX_GPIO, (unsigned)baud);
    return ESP_OK;
}

esp_err_t transport_kline_set_baud(uint32_t baud) {
    if (!s_installed) return ESP_ERR_INVALID_STATE;
    esp_err_t err = uart_set_baudrate(BOARD_KLINE_UART_NUM, baud);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "baud → %u", (unsigned)baud);
    }
    return err;
}

esp_err_t transport_kline_set_parity(const char *parity) {
    if (!s_installed || !parity) return ESP_ERR_INVALID_STATE;
    uart_parity_t p;
    if (strcasecmp(parity, "none") == 0)      p = UART_PARITY_DISABLE;
    else if (strcasecmp(parity, "even") == 0) p = UART_PARITY_EVEN;
    else if (strcasecmp(parity, "odd")  == 0) p = UART_PARITY_ODD;
    else return ESP_ERR_INVALID_ARG;
    esp_err_t err = uart_set_parity(BOARD_KLINE_UART_NUM, p);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "parity → %s", parity);
    }
    return err;
}

esp_err_t transport_kline_set_loopback(bool enabled) {
    if (!s_installed) return ESP_ERR_INVALID_STATE;
    esp_err_t err = uart_set_loop_back(BOARD_KLINE_UART_NUM, enabled);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "loopback → %s", enabled ? "ON" : "OFF");
    }
    return err;
}

esp_err_t transport_kline_wire_test(transport_kline_wire_result_t *out) {
    if (!s_installed || !out) return ESP_ERR_INVALID_STATE;
    if (BOARD_KLINE_TX_GPIO < 0 || BOARD_KLINE_RX_GPIO < 0) return ESP_ERR_INVALID_STATE;

    const gpio_num_t tx = (gpio_num_t)BOARD_KLINE_TX_GPIO;
    const gpio_num_t rx = (gpio_num_t)BOARD_KLINE_RX_GPIO;

    // Detach UART by routing zero-signal to TX pin and disconnecting RX
    // pin from the UART input. We re-bind via uart_set_pin at the end.
    esp_rom_gpio_pad_select_gpio(tx);
    esp_rom_gpio_pad_select_gpio(rx);
    gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    gpio_set_direction(rx, GPIO_MODE_INPUT);

    // 1. TX high — UART idle equivalent. L9637D's open-drain output
    // releases the bus; internal 510Ω pull-up to Vs holds K-line high,
    // so the RxD output to MCU should be high.
    gpio_set_level(tx, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    int rx_hi = gpio_get_level(rx);

    // 2. TX low — L9637D drives K-line low, so RxD output should go low.
    gpio_set_level(tx, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    int rx_lo = gpio_get_level(rx);

    // Restore TX high before reattaching UART (idle state).
    gpio_set_level(tx, 1);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Restore UART control. uart_set_pin re-routes the IO matrix.
    esp_err_t err = uart_set_pin(BOARD_KLINE_UART_NUM, tx, rx,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    out->rx_when_tx_high = rx_hi;
    out->rx_when_tx_low  = rx_lo;
    out->loop_ok = (rx_hi == 1 && rx_lo == 0);

    ESP_LOGI(TAG, "wire test: TX=1→RX=%d, TX=0→RX=%d, loop_ok=%d",
             rx_hi, rx_lo, out->loop_ok);
    return err;
}

esp_err_t transport_kline_hold_tx(int level, uint32_t hold_ms,
                                   int *out_rx_during) {
    if (!s_installed) return ESP_ERR_INVALID_STATE;
    if (BOARD_KLINE_TX_GPIO < 0 || BOARD_KLINE_RX_GPIO < 0) return ESP_ERR_INVALID_STATE;

    const gpio_num_t tx = (gpio_num_t)BOARD_KLINE_TX_GPIO;
    const gpio_num_t rx = (gpio_num_t)BOARD_KLINE_RX_GPIO;

    esp_rom_gpio_pad_select_gpio(tx);
    esp_rom_gpio_pad_select_gpio(rx);
    gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    gpio_set_direction(rx, GPIO_MODE_INPUT);

    gpio_set_level(tx, level ? 1 : 0);
    if (hold_ms > 30000) hold_ms = 30000;        // cap for safety
    vTaskDelay(pdMS_TO_TICKS(hold_ms / 2));
    int rx_mid = gpio_get_level(rx);
    vTaskDelay(pdMS_TO_TICKS(hold_ms - hold_ms / 2));

    // Restore idle-high then re-attach UART.
    gpio_set_level(tx, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    esp_err_t err = uart_set_pin(BOARD_KLINE_UART_NUM, tx, rx,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    if (out_rx_during) *out_rx_during = rx_mid;
    ESP_LOGI(TAG, "hold_tx: drove TX=%d for %u ms, RX read=%d mid-hold",
             level, (unsigned)hold_ms, rx_mid);
    return err;
}

// ---- bit-bang helpers ----------------------------------------------------

// Detach the UART from BOARD_KLINE_TX/RX, switch them to plain GPIO,
// and drive TX idle high. Caller toggles TX, then calls
// kline_gpio_release() to give the pins back to the UART.
static esp_err_t kline_gpio_take(gpio_num_t *out_tx, gpio_num_t *out_rx) {
    if (!s_installed) return ESP_ERR_INVALID_STATE;
    if (BOARD_KLINE_TX_GPIO < 0 || BOARD_KLINE_RX_GPIO < 0) return ESP_ERR_INVALID_STATE;
    gpio_num_t tx = (gpio_num_t)BOARD_KLINE_TX_GPIO;
    gpio_num_t rx = (gpio_num_t)BOARD_KLINE_RX_GPIO;
    esp_rom_gpio_pad_select_gpio(tx);
    esp_rom_gpio_pad_select_gpio(rx);
    gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    gpio_set_direction(rx, GPIO_MODE_INPUT);
    gpio_set_level(tx, 1);  // idle high
    if (out_tx) *out_tx = tx;
    if (out_rx) *out_rx = rx;
    return ESP_OK;
}

static esp_err_t kline_gpio_release(gpio_num_t tx, gpio_num_t rx) {
    // Restore idle high before the IO matrix routes UART back onto the pin
    // — otherwise the UART's internal TX-idle-high signal would briefly
    // race with whatever we last drove and the L9637D might see a glitch.
    gpio_set_level(tx, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    esp_err_t err = uart_set_pin(BOARD_KLINE_UART_NUM, tx, rx,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // Drain any stale RX bytes the bit-bang generated (the K-line is
    // half-duplex — our own pulse echoes back as garbage at the wrong
    // baud, and we don't want it polluting subsequent reads).
    if (err == ESP_OK) {
        uart_flush_input(BOARD_KLINE_UART_NUM);
    }
    return err;
}

esp_err_t transport_kline_send_5baud(uint8_t value, uint32_t bit_time_ms) {
    if (bit_time_ms == 0) bit_time_ms = KLINE_FIVE_BAUD_BIT_TIME_MS;

    gpio_num_t tx, rx;
    esp_err_t err = kline_gpio_take(&tx, &rx);
    if (err != ESP_OK) return err;

    // Mirrors slowInit.ts buildBitSequence: [0, b0, b1, ..., b7, 1].
    // Start bit (low) → 8 data bits LSB-first → stop bit (high).
    const TickType_t bit_ticks = pdMS_TO_TICKS(bit_time_ms);

    gpio_set_level(tx, 0);           // start bit
    vTaskDelay(bit_ticks);
    for (int i = 0; i < 8; ++i) {
        gpio_set_level(tx, (value >> i) & 0x01);
        vTaskDelay(bit_ticks);
    }
    gpio_set_level(tx, 1);           // stop bit
    vTaskDelay(bit_ticks);

    ESP_LOGI(TAG, "5-baud init: sent 0x%02X (%u ms/bit, total ~%u ms)",
             value, (unsigned)bit_time_ms, (unsigned)(bit_time_ms * 10));
    return kline_gpio_release(tx, rx);
}

esp_err_t transport_kline_send_fast_init(uint32_t break_ms, uint32_t idle_ms) {
    if (break_ms == 0) break_ms = KLINE_FAST_INIT_BREAK_MS;
    if (idle_ms  == 0) idle_ms  = KLINE_FAST_INIT_IDLE_MS;

    gpio_num_t tx, rx;
    esp_err_t err = kline_gpio_take(&tx, &rx);
    if (err != ESP_OK) return err;

    gpio_set_level(tx, 0);           // break
    vTaskDelay(pdMS_TO_TICKS(break_ms));
    gpio_set_level(tx, 1);           // idle
    vTaskDelay(pdMS_TO_TICKS(idle_ms));

    ESP_LOGI(TAG, "fast init: %u ms low, %u ms idle",
             (unsigned)break_ms, (unsigned)idle_ms);
    return kline_gpio_release(tx, rx);
}

esp_err_t transport_kline_read(uint8_t *buf, size_t cap, size_t *out_len,
                               uint32_t timeout_ms) {
    if (!s_installed) return ESP_ERR_INVALID_STATE;
    if (out_len) *out_len = 0;
    if (!buf || cap == 0) return ESP_ERR_INVALID_ARG;
    int got = uart_read_bytes(BOARD_KLINE_UART_NUM, buf, cap,
                              pdMS_TO_TICKS(timeout_ms));
    if (got < 0) return ESP_FAIL;
    if (out_len) *out_len = (size_t)got;
    return ESP_OK;
}

esp_err_t transport_kline_write_and_consume_echo(const uint8_t *tx, size_t len) {
    if (!s_installed) return ESP_ERR_INVALID_STATE;
    if (!tx || len == 0) return ESP_ERR_INVALID_ARG;

    uart_flush_input(BOARD_KLINE_UART_NUM);
    int wrote = uart_write_bytes(BOARD_KLINE_UART_NUM, tx, len);
    if (wrote < 0 || (size_t)wrote != len) {
        ESP_LOGE(TAG, "uart_write_bytes returned %d", wrote);
        return ESP_FAIL;
    }
    uart_wait_tx_done(BOARD_KLINE_UART_NUM, pdMS_TO_TICKS(200));

    // Read back the half-duplex echo. Mirrors ds2.ts hasAdapterEcho path
    // (we always have echo on a single-wire bus).
    uint8_t stack[64];
    uint8_t *echo = stack;
    bool heap = false;
    if (len > sizeof(stack)) {
        echo = malloc(len);
        if (!echo) return ESP_ERR_NO_MEM;
        heap = true;
    }
    int got = uart_read_bytes(BOARD_KLINE_UART_NUM, echo, len,
                              pdMS_TO_TICKS(K_ECHO_TIMEOUT_MS));
    if (got != (int)len) {
        ESP_LOGW(TAG, "echo short: got %d of %u", got, (unsigned)len);
        if (heap) free(echo);
        return ESP_FAIL;
    }
    if (memcmp(echo, tx, len) != 0) {
        ESP_LOGW(TAG, "echo mismatch");
        if (heap) free(echo);
        return ESP_FAIL;
    }
    if (heap) free(echo);
    return ESP_OK;
}

esp_err_t transport_kline_raw(const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_cap, size_t *rx_len,
                              uint32_t timeout_ms) {
    if (!s_installed) return ESP_ERR_INVALID_STATE;
    if (rx_len) *rx_len = 0;

    if (tx && tx_len > 0) {
        uart_flush_input(BOARD_KLINE_UART_NUM);
        int wrote = uart_write_bytes(BOARD_KLINE_UART_NUM, tx, tx_len);
        if (wrote < 0 || (size_t)wrote != tx_len) {
            ESP_LOGE(TAG, "uart_write_bytes returned %d", wrote);
            return ESP_FAIL;
        }
        // Wait for the TX to clock out before we start the read window.
        uart_wait_tx_done(BOARD_KLINE_UART_NUM, pdMS_TO_TICKS(200));
    }

    if (!rx || rx_cap == 0) return ESP_OK;

    // K-line at 10400 baud = ~960 byte/s. A read attempt with
    // timeout_ms waits for the first byte; subsequent bytes follow at
    // bus rate. We accept "no bytes received" as a non-error so the
    // bench test where no ECU is connected still completes cleanly.
    int got = uart_read_bytes(BOARD_KLINE_UART_NUM, rx, rx_cap,
                              pdMS_TO_TICKS(timeout_ms));
    if (got < 0) {
        ESP_LOGE(TAG, "uart_read_bytes returned %d", got);
        return ESP_FAIL;
    }
    if (rx_len) *rx_len = (size_t)got;
    return ESP_OK;
}

// ---- canonical protocol transactions -------------------------------------

// Mirrors C# ObdAddRecTimeout — fixed buffer added to every receive timeout
// to absorb USB-Serial driver latency. We're on a direct UART (no USB
// serial in the way) but matching the TS code keeps timing behaviour
// identical to the reference; FreeRTOS scheduler jitter takes the place
// of the USB latency the TS code budgets for.
#define K_ADD_REC_TIMEOUT_MS 20u

// Monotonic millisecond clock. Used for regen-time enforcement so a
// back-to-back send pair honours the ECU's required idle gap between
// transactions.
static inline uint64_t kline_now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

// Read exactly `want` bytes into `buf`, with `first_timeout_ms` budget
// for the first byte and `idle_timeout_ms` for each subsequent gap.
// Returns the number of bytes actually read.
static size_t kline_read_exact(uint8_t *buf, size_t want,
                                uint32_t first_timeout_ms,
                                uint32_t idle_timeout_ms) {
    size_t got = 0;
    while (got < want) {
        uint32_t t = (got == 0) ? first_timeout_ms : idle_timeout_ms;
        int n = uart_read_bytes(BOARD_KLINE_UART_NUM, buf + got, want - got,
                                 pdMS_TO_TICKS(t + K_ADD_REC_TIMEOUT_MS));
        if (n <= 0) break;
        got += (size_t)n;
    }
    return got;
}

// Used by both ds2_transact and the VM dispatcher to remember when the
// last successful response landed, so the next call can honour the
// SGBD's ParRegenTime (CommParameter[6]).
static uint64_t s_last_response_ms = 0;

esp_err_t transport_kline_ds2_transact(const transport_kline_ds2_cfg_t *cfg,
                                        const uint8_t *req, size_t req_len,
                                        uint8_t *resp, size_t resp_cap,
                                        size_t *resp_len) {
    if (resp_len) *resp_len = 0;
    if (!s_installed || !cfg || !req || req_len == 0 || !resp || resp_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    kline_ds2_answer_len_t al;
    if (!kline_ds2_answer_len(cfg->concept, &al)) {
        ESP_LOGW(TAG, "ds2: unknown concept 0x%04X", cfg->concept);
        return ESP_ERR_INVALID_ARG;
    }

    // 1. Honour ParRegenTime since last successful response.
    if (cfg->regen_time_ms > 0 && s_last_response_ms > 0) {
        uint64_t elapsed = kline_now_ms() - s_last_response_ms;
        if (elapsed < cfg->regen_time_ms) {
            vTaskDelay(pdMS_TO_TICKS(cfg->regen_time_ms - elapsed));
        }
    }

    // 2. Make sure the UART matches the cfg's baud / 8E1.
    if (cfg->baud_rate > 0) {
        uart_set_baudrate(BOARD_KLINE_UART_NUM, cfg->baud_rate);
    }
    uart_set_parity(BOARD_KLINE_UART_NUM, UART_PARITY_EVEN);

    // 3. Build TX buffer (request + optional XOR).
    uint8_t tx[260];
    if (req_len + 1 > sizeof(tx)) return ESP_ERR_INVALID_SIZE;
    memcpy(tx, req, req_len);
    size_t tx_len = req_len;
    if (!cfg->checksum_by_user) {
        tx[tx_len++] = kline_xor_checksum(tx, 0, req_len);
    }

    // 4. Pre-send purge — drain any stale RX from a probe / partial
    // prior frame so the upcoming echo read isn't poisoned.
    uart_flush_input(BOARD_KLINE_UART_NUM);

    // 5. Send (optionally with inter-byte delay) and wait for TX to clock out.
    if (cfg->inter_byte_ms > 0) {
        for (size_t i = 0; i < tx_len; ++i) {
            uart_write_bytes(BOARD_KLINE_UART_NUM, &tx[i], 1);
            uart_wait_tx_done(BOARD_KLINE_UART_NUM, pdMS_TO_TICKS(50));
            if (i < tx_len - 1) {
                vTaskDelay(pdMS_TO_TICKS(cfg->inter_byte_ms));
            }
        }
    } else {
        uart_write_bytes(BOARD_KLINE_UART_NUM, tx, tx_len);
        uart_wait_tx_done(BOARD_KLINE_UART_NUM, pdMS_TO_TICKS(500));
    }

    // 6. Consume half-duplex echo.
    uint8_t echo[260];
    size_t echo_got = kline_read_exact(echo, tx_len, K_ECHO_TIMEOUT_MS,
                                        cfg->tel_end_ms);
    if (echo_got != tx_len) {
        ESP_LOGW(TAG, "ds2: incomplete echo (%u/%u)",
                 (unsigned)echo_got, (unsigned)tx_len);
        return ESP_FAIL;
    }
    if (memcmp(echo, tx, tx_len) != 0) {
        ESP_LOGW(TAG, "ds2: echo mismatch");
        return ESP_FAIL;
    }

    // 7. Read response header per concept rules.
    //    header_len = -al.header_len_signed + 1 (the byte AT the length
    //    offset is itself part of the header).
    size_t header_len = (al.header_len_signed < 0)
        ? (size_t)(-al.header_len_signed) + 1u
        : (size_t)al.header_len_signed;
    if (header_len == 0 || header_len > resp_cap) return ESP_FAIL;

    uint8_t buf[256];
    size_t got = kline_read_exact(buf, header_len, cfg->timeout_std_ms,
                                   cfg->tel_end_ms);
    if (got < header_len) {
        if (resp_len) *resp_len = got;
        memcpy(resp, buf, got);
        ESP_LOGW(TAG, "ds2: short header (%u/%u)",
                 (unsigned)got, (unsigned)header_len);
        return ESP_FAIL;
    }

    // 8. Compute total length and read the rest.
    size_t total = kline_ds2_tel_length(buf, got, &al);
    if (total == 0 || total > sizeof(buf) || total > resp_cap) {
        if (resp_len) *resp_len = got;
        memcpy(resp, buf, got);
        return ESP_FAIL;
    }
    if (total > got) {
        size_t more = kline_read_exact(buf + got, total - got,
                                        cfg->tel_end_ms, cfg->tel_end_ms);
        got += more;
        if (got < total) {
            if (resp_len) *resp_len = got;
            memcpy(resp, buf, got);
            ESP_LOGW(TAG, "ds2: short tail (%u/%u)",
                     (unsigned)got, (unsigned)total);
            return ESP_FAIL;
        }
    }

    // 9. Verify XOR.
    uint8_t expected = kline_xor_checksum(buf, 0, total - 1);
    if (!cfg->checksum_no_check && buf[total - 1] != expected) {
        if (resp_len) *resp_len = total;
        memcpy(resp, buf, total);
        ESP_LOGW(TAG, "ds2: checksum mismatch (expected 0x%02X, got 0x%02X)",
                 expected, buf[total - 1]);
        return ESP_FAIL;
    }

    memcpy(resp, buf, total);
    if (resp_len) *resp_len = total;
    s_last_response_ms = kline_now_ms();
    return ESP_OK;
}

esp_err_t transport_kline_kwp_transact(const transport_kline_kwp_cfg_t *cfg,
                                        const uint8_t *payload, size_t payload_len,
                                        uint8_t *resp, size_t resp_cap,
                                        size_t *resp_len) {
    if (resp_len) *resp_len = 0;
    if (!s_installed || !cfg || !resp || resp_cap == 0 || payload_len > 0xFF) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1. ParP3 regen since last response.
    if (cfg->regen_p3_ms > 0 && s_last_response_ms > 0) {
        uint64_t elapsed = kline_now_ms() - s_last_response_ms;
        if (elapsed < cfg->regen_p3_ms) {
            vTaskDelay(pdMS_TO_TICKS(cfg->regen_p3_ms - elapsed));
        }
    }

    // 2. Configure UART: KWP2000 is 8N1 at the SGBD's baud (typically 10400).
    if (cfg->baud_rate > 0) {
        uart_set_baudrate(BOARD_KLINE_UART_NUM, cfg->baud_rate);
    }
    uart_set_parity(BOARD_KLINE_UART_NUM, UART_PARITY_DISABLE);

    // 3. Frame outbound telegram.
    uint8_t tx[260];
    size_t tx_len = kline_build_bmw_fast_telegram(payload, payload_len,
                                                   cfg->ecu_address,
                                                   cfg->tester_address,
                                                   tx, sizeof(tx));
    if (tx_len == 0) return ESP_ERR_INVALID_SIZE;

    // 4. Pre-send purge + send + wait TX done.
    uart_flush_input(BOARD_KLINE_UART_NUM);
    uart_write_bytes(BOARD_KLINE_UART_NUM, tx, tx_len);
    uart_wait_tx_done(BOARD_KLINE_UART_NUM, pdMS_TO_TICKS(500));

    // 5. Consume echo.
    uint8_t echo[260];
    size_t echo_got = kline_read_exact(echo, tx_len, K_ECHO_TIMEOUT_MS,
                                        cfg->timeout_p1_ms);
    if (echo_got != tx_len || memcmp(echo, tx, tx_len) != 0) {
        ESP_LOGW(TAG, "kwp: echo missing or mismatched");
        return ESP_FAIL;
    }

    // 6. Read response: first up to 4 header bytes, then maybe 2 extended
    //    bytes if the length field signals extended form, then the rest.
    uint8_t rx[256];
    size_t got = kline_read_exact(rx, 4, cfg->timeout_p2_ms, cfg->timeout_p1_ms);
    if (got < 4) {
        memcpy(resp, rx, got);
        if (resp_len) *resp_len = got;
        ESP_LOGW(TAG, "kwp: short header (%u/4)", (unsigned)got);
        return ESP_FAIL;
    }
    if ((rx[0] & 0xC0) != 0x80) {
        memcpy(resp, rx, got);
        if (resp_len) *resp_len = got;
        ESP_LOGW(TAG, "kwp: invalid header byte 0x%02X", rx[0]);
        return ESP_FAIL;
    }

    // Extended header form: length at rx[4..5].
    if ((rx[0] & 0x3F) == 0 && rx[3] == 0) {
        size_t more = kline_read_exact(rx + 4, 2, cfg->timeout_p1_ms,
                                        cfg->timeout_p1_ms);
        got += more;
        if (got < 6) {
            memcpy(resp, rx, got);
            if (resp_len) *resp_len = got;
            return ESP_FAIL;
        }
    }

    size_t total = kline_bmw_fast_total_length(rx, got);
    if (total == 0 || total > sizeof(rx) || total > resp_cap) {
        memcpy(resp, rx, got);
        if (resp_len) *resp_len = got;
        return ESP_FAIL;
    }
    if (total > got) {
        size_t more = kline_read_exact(rx + got, total - got,
                                        cfg->timeout_p1_ms, cfg->timeout_p1_ms);
        got += more;
        if (got < total) {
            memcpy(resp, rx, got);
            if (resp_len) *resp_len = got;
            return ESP_FAIL;
        }
    }

    // 7. Verify checksum.
    uint8_t expected = kline_bmw_fast_checksum(rx, 0, total - 1);
    if (rx[total - 1] != expected) {
        memcpy(resp, rx, total);
        if (resp_len) *resp_len = total;
        ESP_LOGW(TAG, "kwp: checksum mismatch (expected 0x%02X, got 0x%02X)",
                 expected, rx[total - 1]);
        return ESP_FAIL;
    }

    memcpy(resp, rx, total);
    if (resp_len) *resp_len = total;
    s_last_response_ms = kline_now_ms();
    return ESP_OK;
}

// ---- edxn_transport_t vtable ---------------------------------------------
//
// The VM drives this transport via four kinds of calls:
//   - connect()/disconnect()/is_connected() — session lifecycle
//   - set_parameter(index, value) — positional from xsetpar (the SGBD's
//     CommParameter[] array), plus the magic 0x8042 from xreps
//   - set_answer_lengths() — xawlen, mostly for fixed-frame Concept-0
//   - send(req, resp) — xsend, the actual transaction
//
// We buffer params as they arrive and lazily dispatch send() to the
// appropriate protocol session based on params[0] (concept). Mirrors
// the TS SerialInterface.setCommParameter / .rawData split — same
// inputs, same outputs, same wire-level behaviour.

#define K_PARAM_MAX 16

static struct {
    bool     connected;
    uint32_t params[K_PARAM_MAX];
    uint32_t param_mask;          // bitmask of set indices
    uint16_t answer_lengths[16];
    size_t   answer_length_count;
    uint8_t  repeat_count;        // xreps
    uint8_t  key_bytes[2];        // populated after a slow/fast init
    size_t   key_bytes_len;
} s_session = { 0 };

static inline uint32_t pget(uint8_t i, uint32_t fallback) {
    return (s_session.param_mask & (1u << i)) ? s_session.params[i] : fallback;
}
static inline bool pset(uint8_t i) {
    return (s_session.param_mask & (1u << i)) != 0;
}

static edxn_error_t k_connect(edxn_transport_t *t) {
    (void)t;
    if (!s_installed) return EDXN_ERR_TRANSPORT;
    s_session.connected = true;
    s_last_response_ms = 0;
    ESP_LOGI(TAG, "xconnect");
    return EDXN_OK;
}

static void k_disconnect(edxn_transport_t *t) {
    (void)t;
    s_session.connected = false;
    s_session.param_mask = 0;
    s_session.answer_length_count = 0;
    s_session.repeat_count = 0;
    s_session.key_bytes_len = 0;
    s_last_response_ms = 0;
}

static bool k_is_connected(edxn_transport_t *t) {
    (void)t;
    return s_session.connected;
}

static edxn_error_t k_set_parameter(edxn_transport_t *t,
                                     uint16_t param, uint32_t value) {
    (void)t;
    if (param == 0x8042) {
        s_session.repeat_count = (uint8_t)(value & 0xFF);
        ESP_LOGI(TAG, "xreps: repeat_count = %u", (unsigned)s_session.repeat_count);
        return EDXN_OK;
    }
    if (param < K_PARAM_MAX) {
        s_session.params[param] = value;
        s_session.param_mask |= (1u << param);
        ESP_LOGI(TAG, "xsetpar[%u] = 0x%08X (%u)",
                 (unsigned)param, (unsigned)value, (unsigned)value);
        return EDXN_OK;
    }
    ESP_LOGW(TAG, "xsetpar[%u] out of range (value 0x%08X)",
             (unsigned)param, (unsigned)value);
    return EDXN_OK;
}

static edxn_error_t k_get_parameter(edxn_transport_t *t,
                                     uint16_t param, uint32_t *value) {
    (void)t;
    if (!value) return EDXN_ERR_OPERAND;
    if (param == 0x8042) { *value = s_session.repeat_count; return EDXN_OK; }
    if (param < K_PARAM_MAX) {
        *value = (s_session.param_mask & (1u << param)) ? s_session.params[param] : 0;
        return EDXN_OK;
    }
    *value = 0;
    return EDXN_OK;
}

static edxn_error_t k_set_answer_lengths(edxn_transport_t *t,
                                          const uint16_t *lengths, size_t count) {
    (void)t;
    if (count > sizeof(s_session.answer_lengths) / sizeof(s_session.answer_lengths[0])) {
        count = sizeof(s_session.answer_lengths) / sizeof(s_session.answer_lengths[0]);
    }
    if (count > 0 && lengths) {
        memcpy(s_session.answer_lengths, lengths, count * sizeof(uint16_t));
    }
    s_session.answer_length_count = count;
    return EDXN_OK;
}

// Frequent-mode is not supported (no background timer driving the K-line),
// but BMW SGBDs routinely call xstopf during INITIALISIERUNG to clear any
// inherited frequent-mode state. With no callback, xstopf returns
// comm_error → INITIALISIERUNG aborts before reaching xsetpar. Stub these
// out as no-ops so init completes cleanly. transmit/receive return empty
// data — any SGBD that actually depends on frequent mode would still
// degrade gracefully (the BEST2 program inspects the result register).
static edxn_error_t k_stop_frequent(edxn_transport_t *t) { (void)t; return EDXN_OK; }
static edxn_error_t k_transmit_frequent(edxn_transport_t *t, const uint8_t *d, size_t n) {
    (void)t; (void)d; (void)n; return EDXN_OK;
}
static edxn_error_t k_receive_frequent(edxn_transport_t *t, uint8_t *d, size_t *len, size_t cap) {
    (void)t; (void)d; (void)cap; if (len) *len = 0; return EDXN_OK;
}

static const uint8_t *k_key_bytes(edxn_transport_t *t, size_t *len) {
    (void)t;
    if (len) *len = s_session.key_bytes_len;
    return s_session.key_bytes_len > 0 ? s_session.key_bytes : NULL;
}

// xsend dispatcher. Looks at the latest CommParameter[0] and routes to
// the matching protocol transaction. Mirrors the TS rawData() / DS2 /
// KWP / passthrough split.
static edxn_error_t k_send(edxn_transport_t *t,
                            const uint8_t *req, size_t req_len,
                            uint8_t *resp, size_t *resp_len, size_t resp_cap) {
    (void)t;
    if (!s_installed) return EDXN_ERR_TRANSPORT;
    if (resp_len) *resp_len = 0;
    if (!req || req_len == 0) return EDXN_ERR_OPERAND;

    // Direct-UART app currently owns the wire (someone has /rpc/uart/0
    // open). Refuse the VM-side send so the SGBD's comm-error trap
    // fires cleanly instead of stomping on the other app's bytes.
    if (rpc_uart_kline_locked && rpc_uart_kline_locked()) {
        ESP_LOGW(TAG, "k_send: K-line held by /rpc/uart/0 — refusing");
        return EDXN_ERR_TRANSPORT;
    }

    // Match C# EdInterfaceObd's default CommParameter for K-line: when the
    // SGBD doesn't explicitly set concept via xsetpar, treat the request
    // as DS2 (concept 0x0006) at 9600 8E1. MSS54DS0 and several other BMW
    // production SGBDs rely on this default — their INITIALISIERUNG only
    // calls xconnect, never xsetpar. The TS reference (rawData) doesn't
    // port this default yet, so without it the SGBD's 3-byte payload would
    // hit the wire without XOR and the ECU ignores it.
    uint16_t concept = pset(0) ? (uint16_t)s_session.params[0] : 0x0006;
    ESP_LOGI(TAG, "xsend: concept=0x%04X req_len=%u param_mask=0x%08X%s",
             concept, (unsigned)req_len, (unsigned)s_session.param_mask,
             pset(0) ? "" : " (default DS2)");

    // DS1 / Concept-1 / DS2 (positional layout, see SerialInterface.setCommParameter)
    //   [0] concept                  (0x0001 / 0x0005 / 0x0006)
    //   [1] baud rate                (default 9600)
    //   [2..4] addressing (BEST2-owned)
    //   [5] ParTimeoutStd            (initial response timeout)
    //   [6] ParRegenTime             (gap between transactions)
    //   [7] ParTimeoutTelEnd         (inter-byte / end-of-telegram timeout)
    //   [8] ParInterbyteTime         (optional inter-byte send delay)
    //   [9] checksumByUser           (optional; non-zero = caller-supplied)
    if (concept == 0x0001 || concept == 0x0005 || concept == 0x0006) {
        transport_kline_ds2_cfg_t cfg = {
            .concept          = concept,
            .baud_rate        = pget(1, 9600),
            .timeout_std_ms   = pget(5, 1000),
            .regen_time_ms    = pget(6, 0),
            .tel_end_ms       = pget(7, 20),
            .inter_byte_ms    = pset(8) ? s_session.params[8] : 0,
            .checksum_by_user = pset(9) && s_session.params[9] != 0,
            .checksum_no_check = false,
        };
        // xreps: retry on transient failures. C# ObdTrans runs 1 + N
        // attempts; many BMW DS2 ECUs only respond on the 2nd/3rd try.
        size_t attempts = (size_t)s_session.repeat_count + 1u;
        for (size_t i = 0; i < attempts; ++i) {
            size_t got = 0;
            esp_err_t err = transport_kline_ds2_transact(&cfg, req, req_len,
                                                          resp, resp_cap, &got);
            if (err == ESP_OK) {
                if (resp_len) *resp_len = got;
                return EDXN_OK;
            }
            ESP_LOGW(TAG, "ds2 attempt %u/%u failed: %s",
                     (unsigned)(i + 1), (unsigned)attempts, esp_err_to_name(err));
            uart_flush_input(BOARD_KLINE_UART_NUM);
        }
        return EDXN_ERR_TRANSPORT;
    }

    // BMW-FAST / KWP2000 (concept 0x010C per TS roadmap).
    //   [1] baud rate (10400)
    //   [2] tester address
    //   [3] ecu address
    //   [5] P2 (response timeout)
    //   [6] P3 (gap between transactions)
    //   [7] P1 (inter-byte timeout)
    if (concept == 0x010C) {
        transport_kline_kwp_cfg_t cfg = {
            .ecu_address     = (uint8_t)(pget(3, 0x10) & 0xFF),
            .tester_address  = (uint8_t)(pget(2, 0xF1) & 0xFF),
            .baud_rate       = pget(1, 10400),
            .timeout_p2_ms   = pget(5, 1200),
            .timeout_p1_ms   = pget(7, 50),
            .regen_p3_ms     = pget(6, 20),
        };
        size_t got = 0;
        esp_err_t err = transport_kline_kwp_transact(&cfg, req, req_len,
                                                      resp, resp_cap, &got);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "kwp transact: %s", esp_err_to_name(err));
            return EDXN_ERR_TRANSPORT;
        }
        if (resp_len) *resp_len = got;
        return EDXN_OK;
    }

    // Unknown / unconfigured concept — log loudly and refuse to talk so the
    // SGBD's error trap fires instead of garbling the K-line at the wrong baud.
    ESP_LOGW(TAG, "k_send: no protocol for concept 0x%04X (params not set yet?)",
             concept);
    return EDXN_ERR_TRANSPORT;
}

// xtype / xvers — match the C# OBD32.dll constants ("OBD" / 0xD1) like
// the TS implementation does. Visible to BEST2 via UTILITY.PRG's
// INTERFACE job (TYP / VERSION result fields).
static const char *k_iface_type(edxn_transport_t *t) { (void)t; return "OBD"; }
static uint32_t    k_iface_ver(edxn_transport_t *t)  { (void)t; return 0xD1; }

// Ignition / battery — return constant 12000 mV when connected, 0 otherwise.
// We don't have a way to probe these on the L9637D-only path (no
// DSR/voltage telemetry from a smart adapter), so we mirror the TS
// SerialInterface fallback: "connected == ignition on == 12 V".
// BEST2 programs typically check >= 10000 mV as the "ignition on"
// threshold — 12000 mV trivially passes.
static uint32_t k_ignition_mv(edxn_transport_t *t) {
    (void)t; return s_session.connected ? 12000u : 0u;
}
static uint32_t k_battery_mv(edxn_transport_t *t) {
    (void)t; return s_session.connected ? 12000u : 0u;
}

static edxn_transport_t s_vtable = {
    .ctx = NULL,
    .connect             = k_connect,
    .disconnect          = k_disconnect,
    .send                = k_send,
    .transmit_frequent   = k_transmit_frequent,
    .receive_frequent    = k_receive_frequent,
    .stop_frequent       = k_stop_frequent,
    .is_connected        = k_is_connected,
    .set_parameter       = k_set_parameter,
    .get_parameter       = k_get_parameter,
    .set_answer_lengths  = k_set_answer_lengths,
    .key_bytes           = k_key_bytes,
    .interface_type      = k_iface_type,
    .interface_version   = k_iface_ver,
    .ignition_mv         = k_ignition_mv,
    .battery_mv          = k_battery_mv,
    // Optional callbacks (reset/boot/state/get_port/loop_test/set_program_voltage/
    // raw_data/set_port/switch_si_relais) left NULL — the VM falls back to
    // either no-op or comm_error per the transport.h contract.
};

edxn_transport_t *transport_kline_vtable(void) {
    return s_installed ? &s_vtable : NULL;
}

esp_err_t transport_kline_init(void) {
    if (s_installed) return ESP_OK;
    return install_uart(K_BAUD_DEFAULT);
}
