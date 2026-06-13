#include "ota_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "http_static.h"

static const char *TAG = "ota_manager";

// Streaming chunk size for the HTTP body read + ota_write. 8 KB is
// the sweet spot for esp_http_server's recv loop and matches the
// flash sector size for efficient erase-then-write.
#define OTA_CHUNK_BYTES     (8 * 1024)

// Delay between flushing the success response and rebooting. Plenty
// for the TCP ACK round-trip on local Wi-Fi; long enough that the
// browser has rendered the "rebooting…" toast.
#define OTA_RESTART_DELAY_MS  500

// Grace period before an OTA-installed image is marked permanently
// valid. Any panic / watchdog reset inside this window causes the
// bootloader to roll back to the previous slot.
#define ROLLBACK_GRACE_MS    (60 * 1000)

// --- response helpers ------------------------------------------------------

static esp_err_t send_json_err(httpd_req_t *req, int status_code,
                               const char *err) {
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", err);
    httpd_resp_set_status(req,
        status_code == 400 ? "400 Bad Request" :
        status_code == 413 ? "413 Payload Too Large" :
                              "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// --- restart-after-response timer ------------------------------------------

static void deferred_restart_cb(void *arg) {
    (void)arg;
    ESP_LOGW(TAG, "OTA complete — restarting now");
    esp_restart();
}

static void schedule_restart(uint32_t delay_ms) {
    static esp_timer_handle_t restart_timer;
    if (!restart_timer) {
        const esp_timer_create_args_t args = {
            .callback = &deferred_restart_cb,
            .name = "ota_restart",
        };
        if (esp_timer_create(&args, &restart_timer) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create failed — restarting immediately");
            esp_restart();
        }
    }
    esp_timer_start_once(restart_timer, (uint64_t)delay_ms * 1000);
}

// --- POST /settings/ota/firmware -------------------------------------------

static esp_err_t handle_ota_firmware(httpd_req_t *req) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target  = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        ESP_LOGE(TAG, "no OTA target partition available");
        return send_json_err(req, 500, "no OTA partition");
    }
    if (target == running) {
        // Shouldn't happen with dual-ota_X partitions, but guards
        // against a misconfigured partition table at runtime.
        return send_json_err(req, 500, "target partition is the running one");
    }

    int total = req->content_len;
    if (total <= 0) {
        return send_json_err(req, 400, "empty body");
    }
    if ((size_t)total > target->size) {
        ESP_LOGE(TAG, "upload %d B > partition %" PRIu32 " B",
                 total, target->size);
        return send_json_err(req, 413, "image larger than partition");
    }

    ESP_LOGI(TAG, "OTA begin: %s @ 0x%" PRIx32 " (%" PRIu32 " B), incoming %d B",
             target->label, target->address, target->size, total);

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        return send_json_err(req, 500, esp_err_to_name(err));
    }

    char *buf = malloc(OTA_CHUNK_BYTES);
    if (!buf) {
        esp_ota_abort(handle);
        return send_json_err(req, 500, "alloc failed");
    }

    int received = 0;
    bool header_checked = false;
    while (received < total) {
        int want = total - received;
        if (want > OTA_CHUNK_BYTES) want = OTA_CHUNK_BYTES;
        int got = httpd_req_recv(req, buf, want);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "recv failed at %d/%d: %d", received, total, got);
            esp_ota_abort(handle);
            free(buf);
            return send_json_err(req, 500, "recv failed");
        }

        // Validate the image header on the first chunk so a wrong
        // file fails fast before we've spent time writing to flash.
        if (!header_checked) {
            const size_t min_hdr =
                sizeof(esp_image_header_t) +
                sizeof(esp_image_segment_header_t) +
                sizeof(esp_app_desc_t);
            if ((size_t)got < min_hdr) {
                ESP_LOGE(TAG, "first chunk too small for header (%d)", got);
                esp_ota_abort(handle);
                free(buf);
                return send_json_err(req, 400, "bad image header");
            }
            esp_app_desc_t incoming;
            memcpy(&incoming,
                   buf + sizeof(esp_image_header_t)
                       + sizeof(esp_image_segment_header_t),
                   sizeof(esp_app_desc_t));
            if (incoming.magic_word != ESP_APP_DESC_MAGIC_WORD) {
                ESP_LOGE(TAG, "image magic mismatch: 0x%08" PRIx32,
                         incoming.magic_word);
                esp_ota_abort(handle);
                free(buf);
                return send_json_err(req, 400, "not an ESP app image");
            }
            ESP_LOGI(TAG, "incoming firmware: %s (idf %s)",
                     incoming.version, incoming.idf_ver);
            header_checked = true;
        }

        err = esp_ota_write(handle, buf, got);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write at %d: %s", received, esp_err_to_name(err));
            esp_ota_abort(handle);
            free(buf);
            return send_json_err(req, 500, esp_err_to_name(err));
        }
        received += got;
    }
    free(buf);

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        return send_json_err(req, 500, esp_err_to_name(err));
    }
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition: %s", esp_err_to_name(err));
        return send_json_err(req, 500, esp_err_to_name(err));
    }

    esp_app_desc_t desc = { 0 };
    esp_ota_get_partition_description(target, &desc);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddStringToObject(r, "version", desc.version);
    cJSON_AddStringToObject(r, "partition", target->label);
    cJSON_AddNumberToObject(r, "size", received);
    cJSON_AddNumberToObject(r, "restart_ms", OTA_RESTART_DELAY_MS);
    char *json = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    ESP_LOGW(TAG, "OTA staged on %s (v%s, %d B) — restart in %d ms",
             target->label, desc.version, received, OTA_RESTART_DELAY_MS);
    schedule_restart(OTA_RESTART_DELAY_MS);
    return ESP_OK;
}

// --- boot-time rollback handling -------------------------------------------

static void rollback_grace_cb(void *arg) {
    (void)arg;
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "rollback grace expired — image marked valid");
    } else {
        ESP_LOGW(TAG, "mark_app_valid failed: %s", esp_err_to_name(err));
    }
}

static void arm_rollback_grace_if_pending(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "running partition %s is already valid (state=%d)",
                 running->label, state);
        return;
    }
    ESP_LOGW(TAG, "running partition %s is PENDING_VERIFY — %d ms grace",
             running->label, ROLLBACK_GRACE_MS);
    static esp_timer_handle_t grace_timer;
    const esp_timer_create_args_t args = {
        .callback = &rollback_grace_cb,
        .name = "ota_grace",
    };
    if (esp_timer_create(&args, &grace_timer) != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create(grace) failed — marking valid now");
        esp_ota_mark_app_valid_cancel_rollback();
        return;
    }
    esp_timer_start_once(grace_timer, (uint64_t)ROLLBACK_GRACE_MS * 1000);
}

// --- registration ----------------------------------------------------------

esp_err_t ota_manager_init(void) {
    httpd_handle_t server = http_static_handle();
    if (!server) {
        ESP_LOGE(TAG, "http_static not started — OTA route not registered");
        return ESP_FAIL;
    }

    static const httpd_uri_t route = {
        .uri      = "/settings/ota/firmware",
        .method   = HTTP_POST,
        .handler  = handle_ota_firmware,
    };
    esp_err_t err = httpd_register_uri_handler(server, &route);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register %s failed: %s", route.uri, esp_err_to_name(err));
        return err;
    }

    arm_rollback_grace_if_pending();

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "ready — running=%s next=%s",
             running ? running->label : "?",
             next ? next->label : "?");
    return ESP_OK;
}
