#include "usb_host.h"

#include <string.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "usb/vcp_ftdi.h"

static const char *TAG = "usb_host";

static usb_host_client_handle_t s_client = NULL;
static cdc_acm_dev_hdl_t        s_ftdi_hdl = NULL;

// ---- generic enumeration logger ------------------------------------------
//
// Just runs the host lib events loop. The CDC-ACM driver installs its own
// client; it will open/own FTDI/CDC/CP210x devices. We don't register a
// second client of our own because two clients both trying to grab the
// same FTDI device causes ftdi_vcp_open to return NOT_FOUND.

static void usb_host_lib_task(void *arg) {
    while (1) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

// ---- K+DCAN smart-cable handshake ----------------------------------------
//
// 3 probe telegrams (BMW DS2 framing, "fast checksum" = sum of bytes mod 256).
// Adapter echoes our TX on the wire then returns its response. We just log
// everything and decode after the firmware-version probe.

static uint8_t bmw_sum(const uint8_t *d, size_t n) {
    uint8_t s = 0; while (n--) s += *d++; return s;
}

#define KDCAN_RX_BUF 256
static uint8_t s_rx[KDCAN_RX_BUF];
static volatile size_t s_rx_len = 0;
static SemaphoreHandle_t s_rx_sem = NULL;

static bool on_ftdi_data(const uint8_t *data, size_t len, void *arg) {
    char hex[3 * 32 + 4];
    size_t n_print = len > 32 ? 32 : len;
    int o = 0;
    for (size_t i = 0; i < n_print; ++i) {
        o += snprintf(hex + o, sizeof(hex) - o, "%02X ", data[i]);
    }
    ESP_LOGI(TAG, "FTDI RX[%zu]: %s%s", len, hex, len > 32 ? "..." : "");

    size_t copy = (s_rx_len + len > sizeof(s_rx)) ? sizeof(s_rx) - s_rx_len : len;
    if (copy > 0) {
        memcpy(s_rx + s_rx_len, data, copy);
        s_rx_len += copy;
    }
    if (s_rx_sem) xSemaphoreGive(s_rx_sem);
    return true;
}

static void on_ftdi_event(const cdc_acm_host_dev_event_data_t *event, void *arg) {
    ESP_LOGW(TAG, "FTDI event: type=%d", (int)event->type);
}

// Send a probe and wait up to `timeout_ms` for the expected total length
// (echo + response). Returns the number of bytes received.
static size_t kdcan_probe(const uint8_t *tx, size_t tx_len,
                          size_t expect_resp_len, uint32_t timeout_ms) {
    size_t want = tx_len + expect_resp_len;
    s_rx_len = 0;
    xSemaphoreTake(s_rx_sem, 0);   // drain stale signal

    if (cdc_acm_host_data_tx_blocking(s_ftdi_hdl, tx, tx_len, 1000) != ESP_OK) {
        ESP_LOGE(TAG, "TX failed");
        return 0;
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (s_rx_len < want) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) break;
        xSemaphoreTake(s_rx_sem, deadline - now);
    }
    return s_rx_len;
}

static void kdcan_handshake_task(void *arg) {
    // Brief settle delay after open so the cable's MCU is ready.
    vTaskDelay(pdMS_TO_TICKS(200));

    // 1. Ignition probe — wakes the adapter; dumb cables ignore it.
    uint8_t ign[6] = {0x82, 0xF1, 0xF1, 0xFE, 0xFE, 0};
    ign[5] = bmw_sum(ign, 5);
    ESP_LOGI(TAG, "K+DCAN: sending ignition probe (6 bytes)");
    kdcan_probe(ign, 6, 6, 1000);

    // 2. Escape-mode probe — leaves boot/idle state on smart cables.
    uint8_t esc[8] = {0x84, 0xF1, 0xF1, 0x06, 0x55, 0xAA, 0xD5, 0};
    esc[7] = bmw_sum(esc, 7);
    ESP_LOGI(TAG, "K+DCAN: sending escape probe (8 bytes)");
    kdcan_probe(esc, 8, 8, 1000);

    // 3. Firmware probe — returns adapter type + version. The 9-byte
    // response after our 6-byte echo encodes [hdr…][type_hi type_lo][ver_hi ver_lo][csum].
    uint8_t fw[6] = {0x82, 0xF1, 0xF1, 0xFD, 0xFD, 0};
    fw[5] = bmw_sum(fw, 5);
    ESP_LOGI(TAG, "K+DCAN: sending firmware probe (6 bytes)");
    size_t got = kdcan_probe(fw, 6, 9, 1500);

    if (got >= 6 + 9) {
        const uint8_t *resp = s_rx + 6;  // skip echo
        uint16_t type = ((uint16_t)resp[4] << 8) | resp[5];
        uint16_t ver  = ((uint16_t)resp[6] << 8) | resp[7];
        ESP_LOGI(TAG, "K+DCAN adapter: type=0x%04X version=0x%04X is_kdcan=%s",
                 type, ver, type >= 0x0002 ? "yes" : "no (passthrough)");
    } else {
        ESP_LOGW(TAG, "K+DCAN firmware probe: only %u bytes back (need >= 15) — "
                      "cable may be unpowered or in passthrough mode", (unsigned)got);
    }
    vTaskDelete(NULL);
}

// ---- CDC-ACM new-device callback: open as FTDI VCP, start handshake ------

// Defer opening the device into a worker task so we don't block the
// CDC-ACM driver's new_dev callback, and so the device has a moment to
// finish whatever the driver is doing internally before we grab it.
static void ftdi_claim_task(void *arg) {
    uint16_t pid = (uint16_t)(uintptr_t)arg;

    vTaskDelay(pdMS_TO_TICKS(50));

    cdc_acm_host_device_config_t cfg = {
        .connection_timeout_ms = 2000,
        .out_buffer_size = 256,
        .in_buffer_size  = 256,   // FTDI driver caps to FT232 packet size; 1024 isn't supported
        .event_cb = on_ftdi_event,
        .data_cb  = on_ftdi_data,
        .user_arg = NULL,
    };
    esp_err_t err = ftdi_vcp_open(pid, 0, &cfg, &s_ftdi_hdl);
    if (err != ESP_OK || s_ftdi_hdl == NULL) {
        ESP_LOGE(TAG, "ftdi_vcp_open(PID=0x%04X) failed: %s", pid, esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "FTDI opened (PID=0x%04X)", pid);

    cdc_acm_line_coding_t lc = {
        .dwDTERate = 115200,
        .bCharFormat = 0,
        .bParityType = 0,
        .bDataBits = 8,
    };
    err = cdc_acm_host_line_coding_set(s_ftdi_hdl, &lc);
    ESP_LOGI(TAG, "line_coding_set 115200 8N1: %s", esp_err_to_name(err));

    // FTDI K+DCAN cables expect the host to assert DTR + RTS as a
    // "ready" signal before the cable's MCU starts answering probe
    // telegrams. Without this the IN endpoint stays silent.
    err = cdc_acm_host_set_control_line_state(s_ftdi_hdl, true, true);
    ESP_LOGI(TAG, "set_control_line_state(DTR=1,RTS=1): %s", esp_err_to_name(err));

    // Drop any residual RX before probing.
    vTaskDelay(pdMS_TO_TICKS(100));

    xTaskCreate(kdcan_handshake_task, "kdcan_hs", 4096, NULL, 4, NULL);
    vTaskDelete(NULL);
}

static void cdc_new_dev_cb(usb_device_handle_t dev) {
    const usb_device_desc_t *desc = NULL;
    if (usb_host_get_device_descriptor(dev, &desc) != ESP_OK || !desc) return;

    ESP_LOGI(TAG, "CDC new_dev: VID=0x%04X PID=0x%04X", desc->idVendor, desc->idProduct);

    if (desc->idVendor != FTDI_VID) {
        ESP_LOGI(TAG, "  not FTDI — skipping");
        return;
    }
    if (s_ftdi_hdl != NULL) {
        ESP_LOGW(TAG, "  FTDI already claimed");
        return;
    }
    xTaskCreate(ftdi_claim_task, "ftdi_claim", 4096,
                (void *)(uintptr_t)desc->idProduct, 5, NULL);
}

// ---- public --------------------------------------------------------------

esp_err_t usb_host_start(void) {
    ESP_LOGI(TAG, "installing USB host library");
    esp_log_level_set("USB HOST", ESP_LOG_INFO);
    esp_log_level_set("CDC_ACM", ESP_LOG_INFO);

    usb_host_config_t cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .peripheral_map = BIT0,
    };
    esp_err_t err = usb_host_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreatePinnedToCore(usb_host_lib_task, "usb_lib",
                                 4096, NULL, 6, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;

    // CDC-ACM driver — its own internal client + task. Auto-fires
    // cdc_new_dev_cb whenever an FTDI/CDC/CP210x device enumerates.
    err = cdc_acm_host_install(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc_acm_host_install: %s", esp_err_to_name(err));
        return err;
    }
    s_rx_sem = xSemaphoreCreateBinary();
    cdc_acm_host_register_new_dev_callback(cdc_new_dev_cb);
    ESP_LOGI(TAG, "CDC-ACM host installed");
    return ESP_OK;
}
