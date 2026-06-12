#include "rpc_can.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "boards/board.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

static const char *TAG = "rpc_can";

#define RPC_CAN_MAX_INDICES   2          // bump when CAN2 lands
#define RPC_CAN_RX_TICK_MS    50
#define RPC_CAN_BATCH_MAX     32

typedef struct {
    int                tx_gpio;
    int                rx_gpio;
    int                stby_gpio;        // -1 if tied to GND on the schematic
} can_pinmap_t;

typedef struct {
    int                holder_fd;
    httpd_handle_t     server;
    bool               exclusive;
    bool               installed;        // twai_driver_install_v2 done
    bool               started;          // twai_start_v2 done
    twai_handle_t      twai;
    uint32_t           bitrate;
    twai_mode_t        mode;
    TaskHandle_t       rx_task;
    bool               rx_run;
    can_pinmap_t       pins;
} can_session_t;

static can_session_t   s_sessions[RPC_CAN_MAX_INDICES];
static SemaphoreHandle_t s_lock;

// ---- pinmap ---------------------------------------------------------------

static can_pinmap_t pins_for_index(int idx) {
    can_pinmap_t p = { -1, -1, -1 };
    switch (idx) {
        case 0:
            p.tx_gpio   = BOARD_CAN0_TX_GPIO;
            p.rx_gpio   = BOARD_CAN0_RX_GPIO;
            p.stby_gpio = BOARD_CAN0_STBY_GPIO;
            break;
        case 1:
            p.tx_gpio   = BOARD_CAN1_TX_GPIO;
            p.rx_gpio   = BOARD_CAN1_RX_GPIO;
            p.stby_gpio = BOARD_CAN1_STBY_GPIO;
            break;
        default: break;
    }
    return p;
}

static bool pins_present(const can_pinmap_t *p) {
    return p->tx_gpio >= 0 && p->rx_gpio >= 0;
}

static int parse_can_index(const char *uri) {
    static const char prefix[] = "/rpc/can/";
    size_t prefix_len = sizeof(prefix) - 1;
    if (strncmp(uri, prefix, prefix_len) != 0) return -1;
    const char *tail = uri + prefix_len;
    if (!isdigit((unsigned char)*tail)) return -1;
    int v = 0;
    while (isdigit((unsigned char)*tail)) {
        v = v * 10 + (*tail - '0');
        tail++;
        if (v >= RPC_CAN_MAX_INDICES) return -1;
    }
    if (*tail != '\0' && *tail != '?' && *tail != '/') return -1;
    return v;
}

// ---- base64 / JSON envelopes (copied from rpc_uart — keep behaviour
//      identical so the dashboard / client libraries can share a parser) ----

static char *b64_encode(const uint8_t *src, size_t len) {
    if (len == 0) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t cap = ((len + 2) / 3) * 4 + 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t written = 0;
    if (mbedtls_base64_encode((unsigned char *)out, cap, &written, src, len) != 0) {
        free(out);
        return NULL;
    }
    out[written] = '\0';
    return out;
}

static size_t b64_decode(const char *src, uint8_t *out, size_t cap) {
    if (!src) return 0;
    size_t len = strlen(src);
    size_t written = 0;
    if (mbedtls_base64_decode(out, cap, &written, (const unsigned char *)src, len) != 0) {
        return 0;
    }
    return written;
}

static void ws_send_owned(httpd_handle_t srv, int fd, char *text) {
    if (!srv || fd == 0 || !text) { free(text); return; }
    httpd_ws_frame_t f = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len     = strlen(text),
    };
    httpd_ws_send_frame_async(srv, fd, &f);
    free(text);
}

static char *env_result(const cJSON *id, cJSON *result) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    if (id) cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, true));
    else    cJSON_AddNullToObject(root, "id");
    cJSON_AddItemToObject(root, "result", result);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

static char *env_error(const cJSON *id, int code, const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    if (id) cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, true));
    else    cJSON_AddNullToObject(root, "id");
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

static char *env_notification(const char *method, cJSON *params) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method);
    if (params) cJSON_AddItemToObject(root, "params", params);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

// ---- TWAI driver helpers --------------------------------------------------

static bool timing_for_bitrate(uint32_t bitrate, twai_timing_config_t *out) {
    switch (bitrate) {
        case 25000:   *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_25KBITS();   return true;
        case 50000:   *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS();   return true;
        case 100000:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS();  return true;
        case 125000:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();  return true;
        case 250000:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();  return true;
        case 500000:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();  return true;
        case 800000:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS();  return true;
        case 1000000: *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();    return true;
        default: return false;
    }
}

static twai_mode_t mode_from_string(const char *s, twai_mode_t fallback) {
    if (!s) return fallback;
    if (strcasecmp(s, "listen-only") == 0 || strcasecmp(s, "listen_only") == 0)
        return TWAI_MODE_LISTEN_ONLY;
    if (strcasecmp(s, "no-ack") == 0 || strcasecmp(s, "no_ack") == 0)
        return TWAI_MODE_NO_ACK;
    return TWAI_MODE_NORMAL;
}

static const char *mode_to_string(twai_mode_t m) {
    switch (m) {
        case TWAI_MODE_LISTEN_ONLY: return "listen-only";
        case TWAI_MODE_NO_ACK:      return "no-ack";
        default:                    return "normal";
    }
}

// Brings the transceiver out of standby. The S pin on the TJA1051T is
// active-low for normal mode; idle high (internal pull-up) keeps the bus
// load disconnected. STBY < 0 means "tied to GND on schematic" — nothing
// to drive.
static void drive_standby(const can_pinmap_t *p, bool standby) {
    if (p->stby_gpio < 0) return;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << p->stby_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level((gpio_num_t)p->stby_gpio, standby ? 1 : 0);
}

static esp_err_t driver_install(can_session_t *s, int idx) {
    twai_timing_config_t t;
    if (!timing_for_bitrate(s->bitrate, &t)) return ESP_ERR_INVALID_ARG;

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(
        idx, s->pins.tx_gpio, s->pins.rx_gpio, s->mode);
    // Take everything; clients filter their own RX. Hardware filter
    // can be exposed later via the `filters:` option on can.open.
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    drive_standby(&s->pins, false);

    esp_err_t err = twai_driver_install_v2(&g, &t, &f, &s->twai);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%d] twai_driver_install_v2: %s", idx, esp_err_to_name(err));
        drive_standby(&s->pins, true);
        return err;
    }
    err = twai_start_v2(s->twai);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%d] twai_start_v2: %s", idx, esp_err_to_name(err));
        twai_driver_uninstall_v2(s->twai);
        s->twai = NULL;
        drive_standby(&s->pins, true);
        return err;
    }
    s->installed = true;
    s->started = true;
    return ESP_OK;
}

static void driver_uninstall(can_session_t *s) {
    if (s->started && s->twai) {
        twai_stop_v2(s->twai);
        s->started = false;
    }
    if (s->installed && s->twai) {
        twai_driver_uninstall_v2(s->twai);
        s->installed = false;
        s->twai = NULL;
    }
    drive_standby(&s->pins, true);
}

// ---- RX pump task ---------------------------------------------------------

static void rx_pump_task(void *arg) {
    int idx = (int)(intptr_t)arg;
    can_session_t *s = &s_sessions[idx];

    while (s->rx_run) {
        twai_message_t msg;
        if (!s->twai) { vTaskDelay(pdMS_TO_TICKS(RPC_CAN_RX_TICK_MS)); continue; }
        esp_err_t err = twai_receive_v2(s->twai, &msg,
                                         pdMS_TO_TICKS(RPC_CAN_RX_TICK_MS));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "[%d] twai_receive_v2: %s", idx, esp_err_to_name(err));
            continue;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int            fd  = s->holder_fd;
        httpd_handle_t srv = s->server;
        xSemaphoreGive(s_lock);
        if (fd == 0) continue;

        char *b64 = b64_encode(msg.data, msg.data_length_code);
        cJSON *params = cJSON_CreateObject();
        cJSON_AddNumberToObject(params, "id", (double)msg.identifier);
        cJSON_AddBoolToObject(params, "ext", msg.extd);
        cJSON_AddBoolToObject(params, "rtr", msg.rtr);
        cJSON_AddStringToObject(params, "data", b64 ? b64 : "");
        cJSON_AddNumberToObject(params, "ts", (double)esp_timer_get_time());
        free(b64);
        char *text = env_notification("can.rx", params);
        ws_send_owned(srv, fd, text);
    }
    s->rx_task = NULL;
    vTaskDelete(NULL);
}

// ---- session ownership ----------------------------------------------------

static void release_session_locked(int idx) {
    can_session_t *s = &s_sessions[idx];
    s->rx_run = false;
    // Let the rx task observe rx_run=false on its next tick; meanwhile
    // tear down the driver so the controller stops driving the bus.
    driver_uninstall(s);
    s->holder_fd = 0;
    s->server = NULL;
    s->exclusive = false;
}

static void revoke_previous_holder(int idx, const char *by) {
    can_session_t *s = &s_sessions[idx];
    if (s->holder_fd != 0 && s->server) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "by", by ? by : "?");
        char *text = env_notification("can.revoked", p);
        ws_send_owned(s->server, s->holder_fd, text);
    }
    release_session_locked(idx);
}

// ---- helpers --------------------------------------------------------------

static bool bool_from(const cJSON *p, const char *key, bool fallback) {
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(p, key);
    if (cJSON_IsBool(j)) return cJSON_IsTrue(j);
    return fallback;
}

static uint32_t u32_from(const cJSON *p, const char *key, uint32_t fallback) {
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(p, key);
    if (cJSON_IsNumber(j) && j->valuedouble >= 0) return (uint32_t)j->valuedouble;
    return fallback;
}

static const char *str_from(const cJSON *p, const char *key, const char *fallback) {
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(p, key);
    if (cJSON_IsString(j) && j->valuestring) return j->valuestring;
    return fallback;
}

static cJSON *build_frame_from_json(const cJSON *fr, twai_message_t *msg) {
    if (!cJSON_IsObject(fr)) return cJSON_CreateString("frame_not_object");
    memset(msg, 0, sizeof(*msg));
    msg->identifier = u32_from(fr, "id", 0);
    msg->extd       = bool_from(fr, "ext", false);
    msg->rtr        = bool_from(fr, "rtr", false);
    const char *data_b64 = str_from(fr, "data", "");
    uint8_t buf[64];
    size_t n = b64_decode(data_b64, buf, sizeof(buf));
    if (n > 8) return cJSON_CreateString("data_too_long_for_classical_can");
    msg->data_length_code = (uint8_t)n;
    if (n > 0) memcpy(msg->data, buf, n);
    return NULL;
}

// ---- methods --------------------------------------------------------------

static cJSON *handle_open(int idx, int fd, httpd_handle_t srv, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    if (idx < 0 || idx >= RPC_CAN_MAX_INDICES) {
        cJSON_AddStringToObject(r, "error", "can_not_present");
        return r;
    }
    can_pinmap_t pinmap = pins_for_index(idx);
    if (!pins_present(&pinmap)) {
        cJSON_AddStringToObject(r, "error", "can_not_present");
        return r;
    }
    uint32_t bitrate = u32_from(p, "bitrate", 500000);
    twai_timing_config_t probe;
    if (!timing_for_bitrate(bitrate, &probe)) {
        cJSON_AddStringToObject(r, "error", "unsupported_bitrate");
        return r;
    }
    twai_mode_t mode = mode_from_string(str_from(p, "mode", "normal"),
                                          TWAI_MODE_NORMAL);
    bool want_exclusive = bool_from(p, "exclusive", false);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    can_session_t *s = &s_sessions[idx];
    if (s->holder_fd != 0 && s->holder_fd != fd) {
        if (want_exclusive) {
            xSemaphoreGive(s_lock);
            cJSON_AddStringToObject(r, "error", "bus_busy");
            return r;
        }
        revoke_previous_holder(idx, "rpc-client");
    }

    s->holder_fd = fd;
    s->server    = srv;
    s->exclusive = want_exclusive;
    s->pins      = pinmap;
    s->bitrate   = bitrate;
    s->mode      = mode;

    esp_err_t err = driver_install(s, idx);
    if (err != ESP_OK) {
        s->holder_fd = 0;
        s->server = NULL;
        xSemaphoreGive(s_lock);
        cJSON_AddStringToObject(r, "error", esp_err_to_name(err));
        return r;
    }

    if (!s->rx_task) {
        s->rx_run = true;
        BaseType_t ok = xTaskCreate(rx_pump_task, "can_rx",
                                     4096, (void *)(intptr_t)idx,
                                     tskIDLE_PRIORITY + 4, &s->rx_task);
        if (ok != pdPASS) {
            s->rx_run = false;
            driver_uninstall(s);
            s->holder_fd = 0;
            s->server = NULL;
            xSemaphoreGive(s_lock);
            cJSON_AddStringToObject(r, "error", "rx_task_failed");
            return r;
        }
    }
    xSemaphoreGive(s_lock);

    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddNumberToObject(r, "bitrate", (double)bitrate);
    cJSON_AddStringToObject(r, "mode", mode_to_string(mode));
    cJSON_AddBoolToObject(r, "exclusive", want_exclusive);
    return r;
}

static cJSON *handle_configure(int idx, int fd, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    can_session_t *s = &s_sessions[idx];
    if (s->holder_fd != fd || s->holder_fd == 0) {
        xSemaphoreGive(s_lock);
        cJSON_AddStringToObject(r, "error", "not_holder");
        return r;
    }
    uint32_t bitrate = u32_from(p, "bitrate", s->bitrate);
    twai_mode_t mode = mode_from_string(str_from(p, "mode", NULL), s->mode);
    twai_timing_config_t probe;
    if (!timing_for_bitrate(bitrate, &probe)) {
        xSemaphoreGive(s_lock);
        cJSON_AddStringToObject(r, "error", "unsupported_bitrate");
        return r;
    }

    // TWAI doesn't support runtime bitrate / mode change → stop, uninstall,
    // re-install.
    driver_uninstall(s);
    s->bitrate = bitrate;
    s->mode    = mode;
    esp_err_t err = driver_install(s, idx);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(r, "error", esp_err_to_name(err));
        return r;
    }
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddNumberToObject(r, "bitrate", (double)bitrate);
    cJSON_AddStringToObject(r, "mode", mode_to_string(mode));
    return r;
}

static cJSON *handle_send(int idx, int fd, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    can_session_t *s = &s_sessions[idx];
    if (s->holder_fd != fd || !s->started) {
        cJSON_AddStringToObject(r, "error", "not_holder_or_closed");
        return r;
    }
    twai_message_t msg;
    cJSON *err = build_frame_from_json(p, &msg);
    if (err) {
        cJSON_AddItemToObject(r, "error", err);
        return r;
    }
    esp_err_t terr = twai_transmit_v2(s->twai, &msg, pdMS_TO_TICKS(100));
    if (terr != ESP_OK) {
        cJSON_AddStringToObject(r, "error", esp_err_to_name(terr));
        return r;
    }
    cJSON_AddBoolToObject(r, "ok", true);
    return r;
}

static cJSON *handle_send_batch(int idx, int fd, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    can_session_t *s = &s_sessions[idx];
    if (s->holder_fd != fd || !s->started) {
        cJSON_AddStringToObject(r, "error", "not_holder_or_closed");
        return r;
    }
    const cJSON *frames = cJSON_GetObjectItemCaseSensitive(p, "frames");
    if (!cJSON_IsArray(frames)) {
        cJSON_AddStringToObject(r, "error", "frames_not_array");
        return r;
    }
    int n = cJSON_GetArraySize(frames);
    if (n > RPC_CAN_BATCH_MAX) n = RPC_CAN_BATCH_MAX;
    int sent = 0;
    for (int i = 0; i < n; ++i) {
        cJSON *fr = cJSON_GetArrayItem(frames, i);
        twai_message_t msg;
        if (build_frame_from_json(fr, &msg) != NULL) break;
        if (twai_transmit_v2(s->twai, &msg, pdMS_TO_TICKS(100)) != ESP_OK) break;
        sent++;
    }
    cJSON_AddNumberToObject(r, "sent", sent);
    cJSON_AddNumberToObject(r, "requested", n);
    return r;
}

static cJSON *handle_recover(int idx, int fd) {
    cJSON *r = cJSON_CreateObject();
    can_session_t *s = &s_sessions[idx];
    if (s->holder_fd != fd || !s->installed) {
        cJSON_AddStringToObject(r, "error", "not_holder_or_closed");
        return r;
    }
    esp_err_t err = twai_initiate_recovery_v2(s->twai);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(r, "error", esp_err_to_name(err));
        return r;
    }
    cJSON_AddBoolToObject(r, "ok", true);
    return r;
}

static cJSON *handle_close(int idx, int fd) {
    cJSON *r = cJSON_CreateObject();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_sessions[idx].holder_fd == fd) {
        release_session_locked(idx);
    }
    xSemaphoreGive(s_lock);
    cJSON_AddBoolToObject(r, "ok", true);
    return r;
}

// ---- dispatcher -----------------------------------------------------------

static char *dispatch(int idx, int fd, httpd_handle_t srv,
                       const char *msg, size_t len) {
    cJSON *root = cJSON_ParseWithLength(msg, len);
    if (!root) return env_error(NULL, -32700, "Parse error");
    const cJSON *id     = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsString(method) || !method->valuestring) {
        char *resp = env_error(id, -32600, "Invalid Request");
        cJSON_Delete(root);
        return resp;
    }
    const char *m = method->valuestring;
    cJSON *result = NULL;
    if      (strcmp(m, "can.open")      == 0) result = handle_open(idx, fd, srv, params);
    else if (strcmp(m, "can.configure") == 0) result = handle_configure(idx, fd, params);
    else if (strcmp(m, "can.send")      == 0) result = handle_send(idx, fd, params);
    else if (strcmp(m, "can.sendBatch") == 0) result = handle_send_batch(idx, fd, params);
    else if (strcmp(m, "can.recover")   == 0) result = handle_recover(idx, fd);
    else if (strcmp(m, "can.close")     == 0) result = handle_close(idx, fd);
    else {
        char *resp = env_error(id, -32601, "Method not found");
        cJSON_Delete(root);
        return resp;
    }
    char *resp = id ? env_result(id, result) : NULL;
    if (!id) cJSON_Delete(result);
    cJSON_Delete(root);
    return resp;
}

// ---- WebSocket handler ----------------------------------------------------

// See rpc_uart.c — esp_http_server doesn't keep `req->uri` populated on
// WS-frame re-entries, so we stash the CAN index at handshake time and
// read it back per frame.
static int session_idx_get(httpd_req_t *req) {
    intptr_t v = (intptr_t)httpd_sess_get_ctx(req->handle, httpd_req_to_sockfd(req));
    return v > 0 ? (int)(v - 1) : -1;
}

static void session_idx_set(httpd_req_t *req, int idx) {
    httpd_sess_set_ctx(req->handle, httpd_req_to_sockfd(req),
                        (void *)(intptr_t)(idx + 1), NULL);
}

static esp_err_t ws_handler(httpd_req_t *req) {
    int idx;
    if (req->method == HTTP_GET) {
        idx = parse_can_index(req->uri);
        if (idx < 0) {
            return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such can");
        }
        session_idx_set(req, idx);
        ESP_LOGI(TAG, "ws[%d] connection opened (fd=%d)", idx,
                 httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    idx = session_idx_get(req);
    if (idx < 0) {
        ESP_LOGW(TAG, "ws frame on socket with no stashed idx — closing");
        return ESP_FAIL;
    }
    httpd_ws_frame_t frame = { 0 };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0) return ESP_OK;

    uint8_t *buf = malloc(frame.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) { free(buf); return err; }
    buf[frame.len] = '\0';

    int            fd  = httpd_req_to_sockfd(req);
    httpd_handle_t srv = req->handle;
    char *resp = dispatch(idx, fd, srv, (const char *)buf, frame.len);
    free(buf);
    if (resp) {
        httpd_ws_frame_t out = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)resp,
            .len     = strlen(resp),
        };
        err = httpd_ws_send_frame(req, &out);
        free(resp);
    }
    return err;
}

// ---- start ----------------------------------------------------------------

esp_err_t rpc_can_start(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_ARG;
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }
    httpd_uri_t cfg = {
        .uri          = "/rpc/can/*",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    esp_err_t err = httpd_register_uri_handler(server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /rpc/can/* failed: %s", esp_err_to_name(err));
        return err;
    }
    // Log which indices have wiring vs are stub-only on this board.
    for (int i = 0; i < RPC_CAN_MAX_INDICES; ++i) {
        can_pinmap_t p = pins_for_index(i);
        if (pins_present(&p)) {
            ESP_LOGI(TAG, "/rpc/can/%d ready (TX=%d RX=%d STBY=%d)",
                     i, p.tx_gpio, p.rx_gpio, p.stby_gpio);
        } else {
            ESP_LOGI(TAG, "/rpc/can/%d not wired on this board", i);
        }
    }
    return ESP_OK;
}
