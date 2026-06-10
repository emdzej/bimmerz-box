#include "rpc_uart.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "boards/board.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

#include "kline_proto.h"
#include "transport_kline.h"

static const char *TAG = "rpc_uart";

#define RPC_UART_MAX_INDICES 4
#define RPC_UART_RX_CHUNK    256
#define RPC_UART_RX_TICK_MS  50

// Per-UART session. holder_fd == 0 means "no holder"; httpd's fd values
// are always > 0 for valid sockets. rx_task is created on open and torn
// down on close (or holder disconnect).
typedef struct {
    int                holder_fd;
    httpd_handle_t     server;
    bool               exclusive;
    bool               consume_echo;
    uint32_t           baud;
    char               parity[8];   // "none" / "even" / "odd"
    TaskHandle_t       rx_task;
    bool               rx_run;
} uart_session_t;

static uart_session_t  s_sessions[RPC_UART_MAX_INDICES];
static SemaphoreHandle_t s_lock;

// ---- index helpers --------------------------------------------------------

static int uart_num_for_index(int idx) {
    // Index 0 = K-line. Other indices reserved for future UARTs (IBus,
    // ...). When more land, extend this switch.
    if (idx == 0) return BOARD_KLINE_UART_NUM;
    return -1;
}

// Extracts the trailing digit(s) from `/rpc/uart/<n>`. Returns -1 if
// the URI doesn't match or the index is out of range.
static int parse_uart_index(const char *uri) {
    static const char prefix[] = "/rpc/uart/";
    size_t prefix_len = sizeof(prefix) - 1;
    if (strncmp(uri, prefix, prefix_len) != 0) return -1;
    const char *tail = uri + prefix_len;
    if (!isdigit((unsigned char)*tail)) return -1;
    int v = 0;
    while (isdigit((unsigned char)*tail)) {
        v = v * 10 + (*tail - '0');
        tail++;
        if (v >= RPC_UART_MAX_INDICES) return -1;
    }
    if (*tail != '\0' && *tail != '?' && *tail != '/') return -1;
    return v;
}

bool rpc_uart_kline_locked(void) {
    return s_sessions[0].holder_fd != 0;
}

// ---- base64 helpers -------------------------------------------------------

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

// ---- WS send helpers ------------------------------------------------------

// Sends an outgoing JSON-RPC envelope to a specific fd. `text` is malloc'd
// — the caller transfers ownership; this frees it.
static void ws_send_owned(httpd_handle_t srv, int fd, char *text) {
    if (!srv || fd == 0 || !text) {
        free(text);
        return;
    }
    httpd_ws_frame_t frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len     = strlen(text),
    };
    httpd_ws_send_frame_async(srv, fd, &frame);
    free(text);
}

static char *envelope_result(const cJSON *id, cJSON *result) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    if (id) cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, true));
    else    cJSON_AddNullToObject(root, "id");
    cJSON_AddItemToObject(root, "result", result);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

static char *envelope_error(const cJSON *id, int code, const char *message) {
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

static char *envelope_notification(const char *method, cJSON *params) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method);
    if (params) cJSON_AddItemToObject(root, "params", params);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

// ---- RX pump task ---------------------------------------------------------

static void rx_pump_task(void *arg) {
    int idx = (int)(intptr_t)arg;
    int uart_num = uart_num_for_index(idx);
    uint8_t buf[RPC_UART_RX_CHUNK];

    while (s_sessions[idx].rx_run) {
        int n = uart_read_bytes(uart_num, buf, sizeof(buf),
                                pdMS_TO_TICKS(RPC_UART_RX_TICK_MS));
        if (n <= 0) continue;
        // Snapshot holder under lock so a concurrent close doesn't pull
        // it out from under us mid-send.
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int            fd  = s_sessions[idx].holder_fd;
        httpd_handle_t srv = s_sessions[idx].server;
        xSemaphoreGive(s_lock);
        if (fd == 0) continue;

        char *b64 = b64_encode(buf, (size_t)n);
        if (!b64) continue;
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "data", b64);
        free(b64);
        char *text = envelope_notification("uart.rx", params);
        ws_send_owned(srv, fd, text);
    }

    s_sessions[idx].rx_task = NULL;
    vTaskDelete(NULL);
}

// ---- session ownership ----------------------------------------------------

static void release_session_locked(int idx) {
    s_sessions[idx].rx_run = false;
    s_sessions[idx].holder_fd = 0;
    s_sessions[idx].server = NULL;
    s_sessions[idx].exclusive = false;
}

// Tells the previous holder we kicked them out (best-effort), then frees
// the slot. Called from uart.open when exclusive=false and someone else
// already owns.
static void revoke_previous_holder(int idx, const char *peer_ip) {
    int            old_fd  = s_sessions[idx].holder_fd;
    httpd_handle_t old_srv = s_sessions[idx].server;
    if (old_fd != 0 && old_srv) {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "by", peer_ip ? peer_ip : "?");
        char *text = envelope_notification("uart.revoked", params);
        if (text) ws_send_owned(old_srv, old_fd, text);
    }
    // RX task will exit on next tick when rx_run flips false.
    release_session_locked(idx);
}

// ---- methods --------------------------------------------------------------

static const char *parity_from_params(const cJSON *p, const char *fallback) {
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(p, "parity");
    if (cJSON_IsString(j) && j->valuestring) return j->valuestring;
    return fallback;
}

static uint32_t baud_from_params(const cJSON *p, uint32_t fallback) {
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(p, "baud");
    if (cJSON_IsNumber(j) && j->valueint > 0) return (uint32_t)j->valueint;
    return fallback;
}

static bool bool_from_params(const cJSON *p, const char *key, bool fallback) {
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(p, key);
    if (cJSON_IsBool(j)) return cJSON_IsTrue(j);
    return fallback;
}

// Apply the UART config currently stored on the session to the driver.
// Only used for index 0 (K-line) today; other indices error out before
// reaching here.
static esp_err_t apply_session_config(int idx) {
    if (idx != 0) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err = transport_kline_set_baud(s_sessions[idx].baud);
    if (err != ESP_OK) return err;
    return transport_kline_set_parity(s_sessions[idx].parity);
}

static cJSON *handle_open(int idx, int fd, httpd_handle_t srv,
                           const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    if (uart_num_for_index(idx) < 0) {
        cJSON_AddStringToObject(r, "error", "uart_not_present");
        return r;
    }
    bool want_exclusive = bool_from_params(p, "exclusive", false);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    uart_session_t *s = &s_sessions[idx];

    // Already held by someone else?
    if (s->holder_fd != 0 && s->holder_fd != fd) {
        if (want_exclusive) {
            xSemaphoreGive(s_lock);
            cJSON_AddStringToObject(r, "error", "bus_busy");
            return r;
        }
        // Cooperative — kick the previous holder.
        revoke_previous_holder(idx, "rpc-client");
    }

    s->holder_fd     = fd;
    s->server        = srv;
    s->exclusive     = want_exclusive;
    s->consume_echo  = bool_from_params(p, "consumeEcho", true);
    s->baud          = baud_from_params(p, idx == 0 ? 9600u : 115200u);
    const char *par  = parity_from_params(p, idx == 0 ? "even" : "none");
    strncpy(s->parity, par, sizeof(s->parity) - 1);
    s->parity[sizeof(s->parity) - 1] = '\0';

    apply_session_config(idx);

    if (!s->rx_task) {
        s->rx_run = true;
        BaseType_t ok = xTaskCreate(rx_pump_task, "uart_rx",
                                     4096, (void *)(intptr_t)idx,
                                     tskIDLE_PRIORITY + 4,
                                     &s->rx_task);
        if (ok != pdPASS) {
            s->rx_run = false;
            s->holder_fd = 0;
            xSemaphoreGive(s_lock);
            cJSON_AddStringToObject(r, "error", "rx_task_failed");
            return r;
        }
    }
    xSemaphoreGive(s_lock);

    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddNumberToObject(r, "baud", (double)s->baud);
    cJSON_AddStringToObject(r, "parity", s->parity);
    cJSON_AddBoolToObject(r, "exclusive", s->exclusive);
    cJSON_AddBoolToObject(r, "consumeEcho", s->consume_echo);
    return r;
}

static cJSON *handle_configure(int idx, int fd, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uart_session_t *s = &s_sessions[idx];
    if (s->holder_fd != fd || s->holder_fd == 0) {
        xSemaphoreGive(s_lock);
        cJSON_AddStringToObject(r, "error", "not_holder");
        return r;
    }
    s->baud = baud_from_params(p, s->baud);
    const char *par = parity_from_params(p, s->parity);
    strncpy(s->parity, par, sizeof(s->parity) - 1);
    s->parity[sizeof(s->parity) - 1] = '\0';
    s->consume_echo = bool_from_params(p, "consumeEcho", s->consume_echo);
    apply_session_config(idx);
    xSemaphoreGive(s_lock);

    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddNumberToObject(r, "baud", (double)s->baud);
    cJSON_AddStringToObject(r, "parity", s->parity);
    cJSON_AddBoolToObject(r, "consumeEcho", s->consume_echo);
    return r;
}

static cJSON *handle_write(int idx, int fd, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    if (s_sessions[idx].holder_fd != fd) {
        cJSON_AddStringToObject(r, "error", "not_holder");
        return r;
    }
    const cJSON *dj = cJSON_GetObjectItemCaseSensitive(p, "data");
    if (!cJSON_IsString(dj) || !dj->valuestring) {
        cJSON_AddStringToObject(r, "error", "bad_data");
        return r;
    }
    uint8_t buf[1024];
    size_t n = b64_decode(dj->valuestring, buf, sizeof(buf));
    if (n == 0) {
        cJSON_AddStringToObject(r, "error", "bad_base64_or_empty");
        return r;
    }
    esp_err_t err;
    if (s_sessions[idx].consume_echo) {
        err = transport_kline_write_and_consume_echo(buf, n);
    } else {
        // Raw write — let the RX pump deliver the echo as part of rx.
        // transport_kline_raw with NULL rx works as a fire-and-forget
        // write (it does TX + waits for tx_done).
        size_t got = 0;
        err = transport_kline_raw(buf, n, NULL, 0, &got, 0);
    }
    if (err != ESP_OK) {
        cJSON_AddStringToObject(r, "error", esp_err_to_name(err));
        return r;
    }
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddNumberToObject(r, "wrote", (double)n);
    return r;
}

static cJSON *handle_transact(int idx, int fd, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    if (s_sessions[idx].holder_fd != fd) {
        cJSON_AddStringToObject(r, "error", "not_holder");
        return r;
    }
    const cJSON *dj  = cJSON_GetObjectItemCaseSensitive(p, "data");
    const cJSON *tj  = cJSON_GetObjectItemCaseSensitive(p, "readMs");
    const cJSON *nj  = cJSON_GetObjectItemCaseSensitive(p, "readBytes");

    uint8_t tx[1024];
    size_t tx_len = b64_decode(cJSON_IsString(dj) ? dj->valuestring : NULL,
                                tx, sizeof(tx));
    uint32_t timeout_ms = (cJSON_IsNumber(tj) && tj->valueint > 0)
                          ? (uint32_t)tj->valueint : 200;
    size_t want = (cJSON_IsNumber(nj) && nj->valueint > 0)
                  ? (size_t)nj->valueint : 256;
    if (want > 1024) want = 1024;

    // transact reads ALL bytes within the timeout (echo + response) when
    // consumeEcho is off, otherwise consumes echo first then reads
    // `want` more bytes.
    uint8_t rx[1024];
    size_t got = 0;
    esp_err_t err = ESP_OK;
    if (s_sessions[idx].consume_echo && tx_len > 0) {
        err = transport_kline_write_and_consume_echo(tx, tx_len);
        if (err == ESP_OK) {
            err = transport_kline_read(rx, want, &got, timeout_ms);
        }
    } else {
        err = transport_kline_raw(tx_len > 0 ? tx : NULL, tx_len,
                                   rx, want, &got, timeout_ms);
    }
    if (err != ESP_OK) {
        cJSON_AddStringToObject(r, "error", esp_err_to_name(err));
        return r;
    }
    char *b64 = b64_encode(rx, got);
    cJSON_AddStringToObject(r, "data", b64 ? b64 : "");
    free(b64);
    cJSON_AddNumberToObject(r, "len", (double)got);
    return r;
}

static cJSON *handle_slow_init(int idx, int fd, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    if (s_sessions[idx].holder_fd != fd) {
        cJSON_AddStringToObject(r, "error", "not_holder");
        return r;
    }
    const cJSON *vj  = cJSON_GetObjectItemCaseSensitive(p, "value");
    const cJSON *btj = cJSON_GetObjectItemCaseSensitive(p, "bitTimeMs");
    const cJSON *baj = cJSON_GetObjectItemCaseSensitive(p, "baudAfter");
    const cJSON *paj = cJSON_GetObjectItemCaseSensitive(p, "parityAfter");

    uint8_t value = (cJSON_IsNumber(vj)) ? (uint8_t)(vj->valueint & 0xFF) : 0x33;
    uint32_t bit_ms = (cJSON_IsNumber(btj) && btj->valueint > 0)
                      ? (uint32_t)btj->valueint : 0;

    esp_err_t err = transport_kline_send_5baud(value, bit_ms);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(r, "error", esp_err_to_name(err));
        return r;
    }
    if (cJSON_IsNumber(baj) && baj->valueint > 0) {
        s_sessions[idx].baud = (uint32_t)baj->valueint;
        transport_kline_set_baud(s_sessions[idx].baud);
    }
    if (cJSON_IsString(paj) && paj->valuestring) {
        strncpy(s_sessions[idx].parity, paj->valuestring,
                sizeof(s_sessions[idx].parity) - 1);
        s_sessions[idx].parity[sizeof(s_sessions[idx].parity) - 1] = '\0';
        transport_kline_set_parity(s_sessions[idx].parity);
    }
    cJSON_AddBoolToObject(r, "ok", true);
    return r;
}

static cJSON *handle_fast_init(int idx, int fd, const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    if (s_sessions[idx].holder_fd != fd) {
        cJSON_AddStringToObject(r, "error", "not_holder");
        return r;
    }
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(p, "breakMs");
    const cJSON *ij = cJSON_GetObjectItemCaseSensitive(p, "idleMs");
    uint32_t br   = (cJSON_IsNumber(bj) && bj->valueint > 0) ? (uint32_t)bj->valueint : 0;
    uint32_t idle = (cJSON_IsNumber(ij) && ij->valueint > 0) ? (uint32_t)ij->valueint : 0;
    esp_err_t err = transport_kline_send_fast_init(br, idle);
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
    if (!root) return envelope_error(NULL, -32700, "Parse error");

    const cJSON *id     = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsString(method) || !method->valuestring) {
        char *resp = envelope_error(id, -32600, "Invalid Request");
        cJSON_Delete(root);
        return resp;
    }
    const char *m = method->valuestring;
    cJSON *result = NULL;
    if      (strcmp(m, "uart.open")      == 0) result = handle_open(idx, fd, srv, params);
    else if (strcmp(m, "uart.configure") == 0) result = handle_configure(idx, fd, params);
    else if (strcmp(m, "uart.write")     == 0) result = handle_write(idx, fd, params);
    else if (strcmp(m, "uart.transact")  == 0) result = handle_transact(idx, fd, params);
    else if (strcmp(m, "uart.slowInit")  == 0) result = handle_slow_init(idx, fd, params);
    else if (strcmp(m, "uart.fastInit")  == 0) result = handle_fast_init(idx, fd, params);
    else if (strcmp(m, "uart.close")     == 0) result = handle_close(idx, fd);
    else {
        char *resp = envelope_error(id, -32601, "Method not found");
        cJSON_Delete(root);
        return resp;
    }

    char *resp = id ? envelope_result(id, result) : NULL;
    if (!id) cJSON_Delete(result);
    cJSON_Delete(root);
    return resp;
}

// ---- WebSocket handler ----------------------------------------------------

static esp_err_t ws_handler(httpd_req_t *req) {
    int idx = parse_uart_index(req->uri);
    if (idx < 0) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such uart");
    }
    if (req->method == HTTP_GET) {
        // Handshake complete — esp_http_server returns ESP_OK and the
        // connection becomes a WS. Subsequent calls are frames.
        ESP_LOGI(TAG, "ws[%d] connection opened (fd=%d)", idx,
                 httpd_req_to_sockfd(req));
        return ESP_OK;
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

esp_err_t rpc_uart_start(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_ARG;
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }

    // One wildcard handler covers all `/rpc/uart/<n>` paths. The
    // handler parses the index out of the URI on each request.
    httpd_uri_t cfg = {
        .uri          = "/rpc/uart/*",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    esp_err_t err = httpd_register_uri_handler(server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /rpc/uart/* failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "WS endpoint up at /rpc/uart/<n> (n=0 → K-line)");
    return ESP_OK;
}
