#include "admin_ui.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "http_static.h"

static const char *TAG = "admin_ui";

#define NVS_NAMESPACE     "bimmerz"
#define POST_MAX_BYTES    4096
#define RESTART_DELAY_MS  500

// File browser limits
#define FS_ROOT           "/sdcard"
#define FS_PATH_MAX       384
#define FS_QUERY_MAX      512
#define FS_UPLOAD_CHUNK   4096

// EMBED_FILES symbols for web/admin.html ------------------------------------
extern const uint8_t admin_html_start[] asm("_binary_admin_html_start");
extern const uint8_t admin_html_end[]   asm("_binary_admin_html_end");

// ---- string NVS helpers ---------------------------------------------------
static esp_err_t nvs_get_str_default(nvs_handle_t h, const char *key,
                                      char *out, size_t out_len,
                                      const char *fallback) {
    size_t len = out_len;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        size_t fl = strlen(fallback);
        if (fl >= out_len) fl = out_len - 1;
        memcpy(out, fallback, fl);
        out[fl] = '\0';
        return ESP_OK;
    }
    return err;
}

static esp_err_t nvs_get_u8_default(nvs_handle_t h, const char *key,
                                     uint8_t *out, uint8_t fallback) {
    esp_err_t err = nvs_get_u8(h, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = fallback;
        return ESP_OK;
    }
    return err;
}

// Default SSID is "BimmerzBox-XXXX" derived from the MAC, so it's
// stable across factory resets but unique per device.
static void make_default_ssid(char *out, size_t out_len) {
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        snprintf(out, out_len, "BimmerzBox");
        return;
    }
    snprintf(out, out_len, "BimmerzBox-%02X%02X", mac[4], mac[5]);
}

// ---- handlers -------------------------------------------------------------

static esp_err_t handle_admin_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)admin_html_start,
                           admin_html_end - admin_html_start);
}

static esp_err_t handle_api_info(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();

    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char chip_id[18];
    snprintf(chip_id, sizeof(chip_id), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "firmware", app->version);
    cJSON_AddStringToObject(r, "chip_id", chip_id);
    cJSON_AddStringToObject(r, "idf_version", IDF_VER);
    cJSON_AddNumberToObject(r, "uptime_s", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(r, "free_heap_kb",
                            heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024);
    cJSON_AddNumberToObject(r, "largest_block_kb",
                            heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) / 1024);

    char *json = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, strlen(json));
    free(json);
    return err;
}

static esp_err_t handle_api_config_get(httpd_req_t *req) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return httpd_resp_send_500(req);
    }

    char ssid[33], password[64], ap_ip[24], eth_ip[24], eth_gw[20];
    char eth_mode[12], default_app[16];
    uint8_t channel = 6;

    char default_ssid[33];
    make_default_ssid(default_ssid, sizeof(default_ssid));

    if (err == ESP_OK) {
        nvs_get_str_default(h, "ap_ssid",     ssid,        sizeof(ssid),        default_ssid);
        nvs_get_str_default(h, "ap_password", password,    sizeof(password),    "bimmerzbox");
        nvs_get_str_default(h, "ap_ip",       ap_ip,       sizeof(ap_ip),       "172.16.7.1/24");
        nvs_get_str_default(h, "eth_mode",    eth_mode,    sizeof(eth_mode),    "off");
        nvs_get_str_default(h, "eth_ip",      eth_ip,      sizeof(eth_ip),      "");
        nvs_get_str_default(h, "eth_gw",      eth_gw,      sizeof(eth_gw),      "");
        nvs_get_str_default(h, "default_app", default_app, sizeof(default_app), "inpax");
        nvs_get_u8_default(h, "ap_channel", &channel, 6);
        nvs_close(h);
    } else {
        strcpy(ssid, default_ssid);
        strcpy(password, "bimmerzbox");
        strcpy(ap_ip, "172.16.7.1/24");
        strcpy(eth_mode, "off");
        strcpy(eth_ip, "");
        strcpy(eth_gw, "");
        strcpy(default_app, "inpax");
    }

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "ap_ssid",     ssid);
    cJSON_AddStringToObject(r, "ap_password", password);
    cJSON_AddNumberToObject(r, "ap_channel",  channel);
    cJSON_AddStringToObject(r, "ap_ip",       ap_ip);
    cJSON_AddStringToObject(r, "eth_mode",    eth_mode);
    cJSON_AddStringToObject(r, "eth_ip",      eth_ip);
    cJSON_AddStringToObject(r, "eth_gw",      eth_gw);
    cJSON_AddStringToObject(r, "default_app", default_app);
    char *json = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);

    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_send(req, json, strlen(json));
    free(json);
    return e;
}

// Walks the request body JSON and writes each known key into NVS.
// Unknown keys are silently ignored.
static esp_err_t handle_api_config_post(httpd_req_t *req) {
    if (req->content_len > POST_MAX_BYTES) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
    }
    char *body = malloc(req->content_len + 1);
    if (!body) return httpd_resp_send_500(req);
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) { free(body); return ESP_FAIL; }
    body[n] = '\0';

    cJSON *root = cJSON_ParseWithLength(body, n);
    free(body);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "not a JSON object");
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }

    static const char * const str_keys[] = {
        "ap_ssid", "ap_password", "ap_ip",
        "eth_mode", "eth_ip", "eth_gw", "default_app", NULL
    };
    for (size_t i = 0; str_keys[i] != NULL; ++i) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, str_keys[i]);
        if (cJSON_IsString(v) && v->valuestring) {
            nvs_set_str(h, str_keys[i], v->valuestring);
        }
    }
    const cJSON *ch = cJSON_GetObjectItemCaseSensitive(root, "ap_channel");
    if (cJSON_IsNumber(ch)) {
        int n_ch = ch->valueint;
        if (n_ch >= 1 && n_ch <= 13) nvs_set_u8(h, "ap_channel", (uint8_t)n_ch);
    }

    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    cJSON_Delete(root);

    if (err != ESP_OK) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static void delayed_restart_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
}

static esp_err_t handle_api_restart(httpd_req_t *req) {
    ESP_LOGW(TAG, "restart requested via /api/restart");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    xTaskCreate(delayed_restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t handle_api_factory_reset(httpd_req_t *req) {
    ESP_LOGW(TAG, "factory reset requested via /api/factory-reset");
    nvs_flash_erase();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    xTaskCreate(delayed_restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// Single dispatcher used for any /api/config request — methods diverge here
// so we don't need two URI handlers fighting over the same path.
static esp_err_t handle_api_config(httpd_req_t *req) {
    if (req->method == HTTP_GET)  return handle_api_config_get(req);
    if (req->method == HTTP_POST) return handle_api_config_post(req);
    return httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, NULL);
}

// ---- file browser ---------------------------------------------------------
//
// Endpoints below all live under /api/files. Paths are URL-encoded in the
// `path` query parameter and must:
//   - start with "/sdcard" (the FATFS mount point),
//   - contain no ".." or "//" segments,
//   - stay under FS_PATH_MAX chars.
// Anything else is rejected with 400.

// URL-decode src into dst (in place is OK if dst==src). Stops at first NUL
// or '&'. Returns length written (no NUL terminator added).
static size_t url_decode(char *dst, const char *src, size_t dst_max) {
    size_t o = 0;
    while (*src && *src != '&' && o + 1 < dst_max) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[o++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[o++] = ' ';
            src++;
        } else {
            dst[o++] = *src++;
        }
    }
    dst[o] = '\0';
    return o;
}

// Extract `?path=...` value into out; returns ESP_OK if present + safe.
static esp_err_t get_path_arg(httpd_req_t *req, char *out, size_t out_len) {
    char query[FS_QUERY_MAX];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    char raw[FS_PATH_MAX];
    if (httpd_query_key_value(query, "path", raw, sizeof(raw)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    url_decode(out, raw, out_len);

    // Safety: must start with /sdcard and stay there.
    if (strncmp(out, FS_ROOT, strlen(FS_ROOT)) != 0)        return ESP_ERR_INVALID_ARG;
    if (strstr(out, "/../") != NULL)                        return ESP_ERR_INVALID_ARG;
    if (strstr(out, "//") != NULL)                          return ESP_ERR_INVALID_ARG;
    size_t n = strlen(out);
    if (n >= 3 && strcmp(out + n - 3, "/..") == 0)          return ESP_ERR_INVALID_ARG;
    if (strcmp(out, FS_ROOT "/..") == 0)                    return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static const char *content_type_for_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcasecmp(dot, ".html"))                                return "text/html; charset=utf-8";
    if (!strcasecmp(dot, ".txt") || !strcasecmp(dot, ".log"))     return "text/plain; charset=utf-8";
    if (!strcasecmp(dot, ".json"))                                return "application/json; charset=utf-8";
    if (!strcasecmp(dot, ".csv"))                                 return "text/csv; charset=utf-8";
    if (!strcasecmp(dot, ".js"))                                  return "application/javascript; charset=utf-8";
    if (!strcasecmp(dot, ".css"))                                 return "text/css; charset=utf-8";
    if (!strcasecmp(dot, ".png"))                                 return "image/png";
    if (!strcasecmp(dot, ".svg"))                                 return "image/svg+xml";
    return "application/octet-stream";
}

// GET /api/files?path=/sdcard/<dir>
//
// Returns {path, entries:[{name, type:"dir"|"file", size, mtime}]}
static esp_err_t handle_api_files_list(httpd_req_t *req) {
    char path[FS_PATH_MAX];
    esp_err_t e = get_path_arg(req, path, sizeof(path));
    if (e != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    DIR *d = opendir(path);
    if (!d) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not a directory");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "path", path);
    cJSON *entries = cJSON_AddArrayToObject(root, "entries");

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        // Leave headroom for the longest FAT LFN (255) plus a separator.
        char child[FS_PATH_MAX + 256];
        int wrote = snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
        if (wrote <= 0 || (size_t)wrote >= sizeof(child)) continue;
        struct stat st;
        if (stat(child, &st) != 0) continue;

        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", de->d_name);
        cJSON_AddStringToObject(o, "type", S_ISDIR(st.st_mode) ? "dir" : "file");
        cJSON_AddNumberToObject(o, "size", (double)st.st_size);
        cJSON_AddNumberToObject(o, "mtime", (double)st.st_mtime);
        cJSON_AddItemToArray(entries, o);
    }
    closedir(d);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, json, strlen(json));
    free(json);
    return r;
}

// GET /api/files/raw?path=...
static esp_err_t handle_api_files_raw(httpd_req_t *req) {
    char path[FS_PATH_MAX];
    if (get_path_arg(req, path, sizeof(path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) return httpd_resp_send_404(req);

    httpd_resp_set_type(req, content_type_for_ext(path));

    // Encourage the browser to save the file rather than try to render it.
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char disp[FS_PATH_MAX + 32];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", base);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    char buf[FS_UPLOAD_CHUNK];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            close(fd);
            return ESP_FAIL;
        }
    }
    close(fd);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// POST /api/files/upload?path=...
//
// Raw request body is streamed straight into the target file. No multipart
// parsing — keeps the firmware small. The JS side does fetch(url, {method:
// "POST", body: file}).
static esp_err_t handle_api_files_upload(httpd_req_t *req) {
    char path[FS_PATH_MAX];
    if (get_path_arg(req, path, sizeof(path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        ESP_LOGE(TAG, "upload: open(%s) failed: %d", path, errno);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
    }

    size_t remaining = req->content_len;
    char buf[FS_UPLOAD_CHUNK];
    while (remaining > 0) {
        size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        int got = httpd_req_recv(req, buf, want);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            close(fd);
            unlink(path);
            return ESP_FAIL;
        }
        if (write(fd, buf, got) != got) {
            close(fd);
            unlink(path);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
        }
        remaining -= got;
    }
    close(fd);

    ESP_LOGI(TAG, "uploaded %s (%u bytes)", path, (unsigned)req->content_len);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

// POST /api/files/mkdir?path=...
static esp_err_t handle_api_files_mkdir(httpd_req_t *req) {
    char path[FS_PATH_MAX];
    if (get_path_arg(req, path, sizeof(path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    if (mkdir(path, 0755) != 0) {
        ESP_LOGE(TAG, "mkdir(%s) failed: %d", path, errno);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mkdir failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

// DELETE /api/files?path=...   (also accepts POST /api/files/delete for
// browsers that prefer to avoid the DELETE verb)
static esp_err_t handle_api_files_delete(httpd_req_t *req) {
    char path[FS_PATH_MAX];
    if (get_path_arg(req, path, sizeof(path)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return httpd_resp_send_404(req);
    }
    int rc = S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path);
    if (rc != 0) {
        ESP_LOGE(TAG, "%s(%s) failed: %d",
                 S_ISDIR(st.st_mode) ? "rmdir" : "unlink", path, errno);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   S_ISDIR(st.st_mode) ? "rmdir failed (empty?)" : "unlink failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

// ---- registration ---------------------------------------------------------

esp_err_t admin_ui_start(void) {
    httpd_handle_t server = http_static_handle();
    if (!server) {
        ESP_LOGE(TAG, "http_static not started — admin routes not registered");
        return ESP_FAIL;
    }

    // Note: these paths must be registered BEFORE http_static's wildcard
    // "/*" handler is the only fallback. esp_http_server matches on
    // longest-prefix specificity, so explicit paths win over "/*".

    static const httpd_uri_t routes[] = {
        { .uri = "/admin",            .method = HTTP_GET,    .handler = handle_admin_root },
        { .uri = "/admin/",           .method = HTTP_GET,    .handler = handle_admin_root },
        { .uri = "/api/info",         .method = HTTP_GET,    .handler = handle_api_info },
        { .uri = "/api/config",       .method = HTTP_GET,    .handler = handle_api_config },
        { .uri = "/api/config",       .method = HTTP_POST,   .handler = handle_api_config },
        { .uri = "/api/restart",      .method = HTTP_POST,   .handler = handle_api_restart },
        { .uri = "/api/factory-reset",.method = HTTP_POST,   .handler = handle_api_factory_reset },
        { .uri = "/api/files",        .method = HTTP_GET,    .handler = handle_api_files_list },
        { .uri = "/api/files",        .method = HTTP_DELETE, .handler = handle_api_files_delete },
        { .uri = "/api/files/raw",    .method = HTTP_GET,    .handler = handle_api_files_raw },
        { .uri = "/api/files/upload", .method = HTTP_POST,   .handler = handle_api_files_upload },
        { .uri = "/api/files/mkdir",  .method = HTTP_POST,   .handler = handle_api_files_mkdir },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t e = httpd_register_uri_handler(server, &routes[i]);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "register %s (method %d) failed: %s",
                     routes[i].uri, routes[i].method, esp_err_to_name(e));
            return e;
        }
    }
    ESP_LOGI(TAG, "admin UI ready at /admin/ (embedded, %u bytes)",
             (unsigned)(admin_html_end - admin_html_start));
    return ESP_OK;
}
