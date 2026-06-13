#include "http_static.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "lwip/inet.h"

#include "storage.h"

static const char *TAG = "http_static";

#define APPS_ROOT      "/sdcard/apps"
#define SYS_ROOT       "/sdcard/sys"
#define DATA_ROOT      "/sdcard/data"
#define HUB_APP        "dashboard"        // lives under SYS_ROOT, served at "/"
#define AP_IP_STRING   "172.16.7.1" // must track wifi_ap.c AP_IP
#define FILE_CHUNK     4096

// Embedded welcome page (registered via EMBED_FILES in CMakeLists). Served
// as the captive-portal landing screen for newly-joined clients.
extern const uint8_t welcome_html_start[] asm("_binary_welcome_html_start");
extern const uint8_t welcome_html_end[]   asm("_binary_welcome_html_end");

static httpd_handle_t s_server = NULL;

// ---- captive-portal accept tracking --------------------------------------
//
// Small ring of client IPv4 addresses that have tapped "Continue" on the
// /welcome screen. Once accepted, the dongle starts returning
// platform-appropriate success responses to captive-probe requests from
// that IP, which tells the OS the network is online and dismisses the
// captive sheet. New devices get a fresh welcome.

#define CAPTIVE_ACCEPTED_MAX 16
static uint32_t s_accepted_ips[CAPTIVE_ACCEPTED_MAX];
static size_t   s_accepted_next = 0;
static size_t   s_accepted_count = 0;

static uint32_t client_ipv4(httpd_req_t *req) {
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) return 0;
    struct sockaddr_in6 addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) != 0) return 0;
    // ESP-IDF's lwIP sockets are IPv6-by-default; AP clients reach us via
    // IPv4-mapped-IPv6 addresses. Pull the embedded IPv4 out of the last
    // 4 bytes of the v6 address, or use the v4 address directly when the
    // peer is v4-only.
    if (((struct sockaddr *)&addr)->sa_family == AF_INET) {
        return ((struct sockaddr_in *)&addr)->sin_addr.s_addr;
    }
    return *(const uint32_t *)&addr.sin6_addr.s6_addr[12];
}

static bool captive_is_accepted(uint32_t ip) {
    if (ip == 0) return false;
    for (size_t i = 0; i < s_accepted_count; ++i) {
        if (s_accepted_ips[i] == ip) return true;
    }
    return false;
}

static void captive_mark_accepted(uint32_t ip) {
    if (ip == 0 || captive_is_accepted(ip)) return;
    s_accepted_ips[s_accepted_next] = ip;
    s_accepted_next = (s_accepted_next + 1) % CAPTIVE_ACCEPTED_MAX;
    if (s_accepted_count < CAPTIVE_ACCEPTED_MAX) s_accepted_count++;
    struct in_addr a = { .s_addr = ip };
    ESP_LOGI(TAG, "captive: marked %s as accepted", inet_ntoa(a));
}

// ---- helpers --------------------------------------------------------------

static const char *content_type_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcasecmp(dot, ".html") || !strcasecmp(dot, ".htm"))   return "text/html; charset=utf-8";
    if (!strcasecmp(dot, ".css"))                                 return "text/css; charset=utf-8";
    if (!strcasecmp(dot, ".js")  || !strcasecmp(dot, ".mjs"))     return "application/javascript; charset=utf-8";
    if (!strcasecmp(dot, ".json"))                                return "application/json; charset=utf-8";
    if (!strcasecmp(dot, ".svg"))                                 return "image/svg+xml";
    if (!strcasecmp(dot, ".png"))                                 return "image/png";
    if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg"))    return "image/jpeg";
    if (!strcasecmp(dot, ".ico"))                                 return "image/x-icon";
    if (!strcasecmp(dot, ".webp"))                                return "image/webp";
    if (!strcasecmp(dot, ".woff"))                                return "font/woff";
    if (!strcasecmp(dot, ".woff2"))                               return "font/woff2";
    if (!strcasecmp(dot, ".csv"))                                 return "text/csv; charset=utf-8";
    if (!strcasecmp(dot, ".wasm"))                                return "application/wasm";
    if (!strcasecmp(dot, ".map"))                                 return "application/json; charset=utf-8";
    return "application/octet-stream";
}

// Returns true if `uri` contains any traversal segments. Defensive even
// though esp_http_server normalizes — `..` slipping past would let a
// client read outside the SD card roots.
static bool uri_is_safe(const char *uri) {
    if (strstr(uri, "/../") != NULL) return false;
    if (strstr(uri, "/./") != NULL)  return false;
    if (strstr(uri, "//") != NULL)   return false;
    size_t n = strlen(uri);
    if (n >= 3 && strcmp(uri + n - 3, "/..") == 0) return false;
    return true;
}

static esp_err_t send_file(httpd_req_t *req, const char *fs_path) {
    int fd = open(fs_path, O_RDONLY);
    if (fd < 0) return ESP_FAIL;

    httpd_resp_set_type(req, content_type_for(fs_path));

    // Hashed asset bundles under /assets/ are content-addressable; cache
    // aggressively. Everything else gets a short-lived cache.
    if (strstr(fs_path, "/assets/") != NULL) {
        httpd_resp_set_hdr(req, "Cache-Control",
                           "public, max-age=31536000, immutable");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    }

    char buf[FILE_CHUNK];
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

// Extract the first path segment after "/" (e.g. "/inpax/foo" → "inpax").
// Writes into `out` and returns true on success.
static bool first_segment(const char *uri, char *out, size_t out_len) {
    if (uri[0] != '/') return false;
    const char *start = uri + 1;
    const char *slash = strchr(start, '/');
    size_t len = slash ? (size_t)(slash - start) : strlen(start);
    if (len == 0 || len >= out_len) return false;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

// ---- request handlers -----------------------------------------------------

// Captive-portal check: if the request's Host header isn't our AP IP, the
// client landed here via the DNS hijack (or typed some random URL while
// connected to the AP). Redirecting to http://<ap-ip>/ trips iOS/Android/
// Windows captive-portal detection so the dongle's hub UI pops up.
static bool host_matches_ap(httpd_req_t *req) {
    char host[64];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        return true;  // no Host → don't redirect
    }
    // Trim port suffix if present (e.g. "172.16.7.1:80")
    char *colon = strchr(host, ':');
    if (colon) *colon = '\0';
    return strcmp(host, AP_IP_STRING) == 0;
}

// 302 to the welcome screen — the captive sheet on iOS/Android/Windows
// follows this and renders /welcome inside its sandboxed WebView.
static esp_err_t captive_redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_IP_STRING "/welcome");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Welcome to BimmerzBox\n", -1);
}

// Platform-specific captive-probe success responses. Returned once a
// client has tapped "Continue" — the OS sees its probe URL respond with
// the expected "online" payload and dismisses the captive sheet.
//
// Probe URLs (the OS hits these with the original Host: of the probe
// domain, which our DNS hijack has routed to us):
//   iOS / macOS  : /hotspot-detect.html  → 200 with literal Success HTML
//   Android      : /generate_204          → 204 No Content
//   Windows MSFT : /connecttest.txt       → 200 "Microsoft Connect Test"
//   Windows NCSI : /ncsi.txt              → 200 "Microsoft NCSI"
//
// Anything else: return a plain 204 — harmless, and many OSes treat any
// 2xx without a redirect on their probe URL as "online".
static esp_err_t serve_captive_success(httpd_req_t *req) {
    const char *uri = req->uri;
    if (strstr(uri, "hotspot-detect") || strstr(uri, "library/test/success")) {
        httpd_resp_set_type(req, "text/html");
        static const char body[] =
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
            "<BODY>Success</BODY></HTML>";
        return httpd_resp_send(req, body, sizeof(body) - 1);
    }
    if (strstr(uri, "generate_204") || strstr(uri, "gen_204")) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }
    if (strstr(uri, "connecttest")) {
        httpd_resp_set_type(req, "text/plain");
        static const char body[] = "Microsoft Connect Test";
        return httpd_resp_send(req, body, sizeof(body) - 1);
    }
    if (strstr(uri, "ncsi.txt")) {
        httpd_resp_set_type(req, "text/plain");
        static const char body[] = "Microsoft NCSI";
        return httpd_resp_send(req, body, sizeof(body) - 1);
    }
    // Unknown probe path. A 204 is the safest "yep, online" answer.
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// GET /welcome[.html] — serves the embedded welcome page from flash.
// The captive sheet redirects here on first contact; the page's
// "Continue" button does the POST /captive/accept + opens the
// dashboard in the system browser via <a target="_blank">.
static esp_err_t serve_welcome(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    size_t len = welcome_html_end - welcome_html_start;
    return httpd_resp_send(req, (const char *)welcome_html_start, len);
}

// POST /captive/accept — mark this client IP as accepted so subsequent
// captive-probe requests get the "online" response. The welcome page's
// Continue button fires this with `keepalive: true` so the request
// survives the same-gesture window.open that opens the dashboard in
// the system browser.
static esp_err_t handle_captive_accept(httpd_req_t *req) {
    uint32_t ip = client_ipv4(req);
    captive_mark_accepted(ip);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t handle_data(httpd_req_t *req) {
    // URI is "/data/<rest>"; strip "/data" prefix to get the SD-card path.
    const char *rest = req->uri + 5;  // skip "/data"
    if (*rest != '/') {
        return httpd_resp_send_404(req);
    }
    char fs_path[256];
    int n = snprintf(fs_path, sizeof(fs_path), "%s%s", DATA_ROOT, rest);
    if (n <= 0 || (size_t)n >= sizeof(fs_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "uri too long");
    }
    if (send_file(req, fs_path) != ESP_OK) {
        ESP_LOGI(TAG, "404 %s", fs_path);
        return httpd_resp_send_404(req);
    }
    return ESP_OK;
}

// Returns true if `seg` is a real subdirectory under /sdcard/apps/.
// User apps live there (auto-discovered by the dashboard via
// /api/files?path=/sdcard/apps). The hub itself lives in /sdcard/sys/
// and is served at "/" by handle_hub.
static bool is_app_dir(const char *seg) {
    char path[256];
    int n = snprintf(path, sizeof(path), "%s/%s", APPS_ROOT, seg);
    if (n <= 0 || (size_t)n >= sizeof(path)) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// /<app>/* — serves the literal file from /sdcard/apps/<app>/ if it
// exists, otherwise falls back to /sdcard/apps/<app>/index.html so the
// SPA's hash-routing works.
static esp_err_t handle_app(httpd_req_t *req) {
    char app[64];
    if (!first_segment(req->uri, app, sizeof(app))) {
        return httpd_resp_send_404(req);
    }
    char fs_path[256];
    int n = snprintf(fs_path, sizeof(fs_path), "%s%s", APPS_ROOT, req->uri);
    if (n <= 0 || (size_t)n >= sizeof(fs_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "uri too long");
    }

    struct stat st;
    if (stat(fs_path, &st) == 0 && S_ISREG(st.st_mode)) {
        return send_file(req, fs_path);
    }

    char index_path[256];
    n = snprintf(index_path, sizeof(index_path), "%s/%s/index.html",
                 APPS_ROOT, app);
    if (n <= 0 || (size_t)n >= sizeof(index_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "uri too long");
    }
    if (send_file(req, index_path) != ESP_OK) {
        ESP_LOGI(TAG, "404 %s (and %s)", fs_path, index_path);
        return httpd_resp_send_404(req);
    }
    return ESP_OK;
}

// Hub handler — serves the root namespace from /sdcard/sys/dashboard/.
//   "/"          → /sdcard/sys/dashboard/index.html
//   "/foo.js"    → /sdcard/sys/dashboard/foo.js (literal)
//   "/<other>"   → /sdcard/sys/dashboard/index.html (SPA fallback)
static esp_err_t handle_hub(httpd_req_t *req) {
    char fs_path[256];
    int n;
    if (strcmp(req->uri, "/") == 0) {
        n = snprintf(fs_path, sizeof(fs_path), "%s/%s/index.html", SYS_ROOT, HUB_APP);
    } else {
        n = snprintf(fs_path, sizeof(fs_path), "%s/%s%s", SYS_ROOT, HUB_APP, req->uri);
    }
    if (n <= 0 || (size_t)n >= sizeof(fs_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "uri too long");
    }

    struct stat st;
    if (stat(fs_path, &st) == 0 && S_ISREG(st.st_mode)) {
        return send_file(req, fs_path);
    }

    // SPA fallback to hub's index.html
    char index_path[256];
    n = snprintf(index_path, sizeof(index_path), "%s/%s/index.html",
                 SYS_ROOT, HUB_APP);
    if (n <= 0 || (size_t)n >= sizeof(index_path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "uri too long");
    }
    if (send_file(req, index_path) != ESP_OK) {
        ESP_LOGI(TAG, "404 %s (and %s)", fs_path, index_path);
        return httpd_resp_send_404(req);
    }
    return ESP_OK;
}

// Top-level dispatcher invoked by the single wildcard handler.
//   1. Wrong-host requests → either return "online" probe success (when
//      the client has tapped Continue), or 302 to /welcome (otherwise)
//   2. /welcome[.html] → embedded captive welcome page
//   3. /data/* → SD-card vehicle data (read-only)
//   4. /<known-app>/* (real dir under /sdcard/web/, not "dashboard") → app handler
//   5. Anything else → hub at /
static esp_err_t handle_any(httpd_req_t *req) {
    if (!uri_is_safe(req->uri)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    if (!host_matches_ap(req)) {
        if (captive_is_accepted(client_ipv4(req))) {
            return serve_captive_success(req);
        }
        return captive_redirect(req);
    }
    if (strcmp(req->uri, "/welcome") == 0 ||
        strcmp(req->uri, "/welcome.html") == 0) {
        return serve_welcome(req);
    }
    if (strncmp(req->uri, "/data/", 6) == 0 ||
        strcmp(req->uri, "/data") == 0) {
        return handle_data(req);
    }
    char seg[64];
    if (first_segment(req->uri, seg, sizeof(seg)) && is_app_dir(seg)) {
        return handle_app(req);
    }
    return handle_hub(req);
}

// ---- start ----------------------------------------------------------------

esp_err_t http_static_start(void) {
    if (s_server != NULL) {
        ESP_LOGW(TAG, "already started");
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    // Counted live: 13 (admin_ui — /settings/* + /api/*) + 1
    // (ota_manager — /settings/ota/firmware) + 1 (jsonrpc) + 1
    // (rpc_uart) + 1 (rpc_can) + 1 (captive accept POST) + 2 (wildcard
    // GET+HEAD) = 20 today. 24 leaves room for the next four endpoints
    // without another silent boot-loop. ESP_ERROR_CHECK on
    // http_static_install_fallback() panics if this is too low.
    cfg.max_uri_handlers = 24;
    cfg.stack_size = 8192;

    // Long-running JSON-RPC jobs (NCSX C_FA_LESEN, EDIABASX flash
    // reads, anything that does many DS2 / KWP round-trips on the
    // slow K-line) hold the WS handler for tens of seconds. The
    // default 5 s socket timeouts close the connection mid-job and
    // the client sees "WebSocket error". 120 s is well past the
    // longest job we've measured (~30 s for FA + coding read on
    // E46) with margin for slow ECUs.
    cfg.recv_wait_timeout = 120;
    cfg.send_wait_timeout = 120;

    // Keepalive — push small TCP probes during idle stretches so a
    // long-running job doesn't get killed by an intermediate NAT
    // (or the OS's own dead-connection detector) deciding the
    // socket has gone quiet.
    cfg.keep_alive_enable    = true;
    cfg.keep_alive_idle      = 30;   // s of idle before first probe
    cfg.keep_alive_interval  = 10;   // s between probes
    cfg.keep_alive_count     = 6;    // dead after 6 missed probes

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "HTTP server up on port %d (apps=%s, sys=%s, data=%s)",
             cfg.server_port, APPS_ROOT, SYS_ROOT, DATA_ROOT);
    return ESP_OK;
}

esp_err_t http_static_install_fallback(void) {
    if (s_server == NULL) {
        ESP_LOGE(TAG, "install_fallback called before start");
        return ESP_FAIL;
    }

    // Captive-portal accept endpoint — registered before the wildcard so
    // POST /captive/accept hits this specific handler instead of falling
    // through to handle_any (which is GET/HEAD only).
    httpd_uri_t accept = {
        .uri = "/captive/accept",
        .method = HTTP_POST,
        .handler = handle_captive_accept,
        .user_ctx = NULL,
    };
    esp_err_t err = httpd_register_uri_handler(s_server, &accept);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register POST /captive/accept failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    // Register the same handler for both GET and HEAD so the dashboard
    // can probe tile presence with HEAD without triggering 405.
    static const httpd_method_t methods[] = { HTTP_GET, HTTP_HEAD };
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        httpd_uri_t any = {
            .uri = "/*",
            .method = methods[i],
            .handler = handle_any,
            .user_ctx = NULL,
        };
        err = httpd_register_uri_handler(s_server, &any);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register /* (method %d) failed: %s",
                     methods[i], esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "static-file fallback installed "
                  "(GET + HEAD + POST /captive/accept)");
    return ESP_OK;
}

httpd_handle_t http_static_handle(void) {
    return s_server;
}
