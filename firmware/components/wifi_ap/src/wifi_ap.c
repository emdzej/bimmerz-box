#include "wifi_ap.h"

#include <string.h>
#include <stdio.h>

#include "dns_server.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"

static const char *TAG = "wifi_ap";

/* NVS namespace + key. Shared with admin_ui's `/api/config` GET/POST
   surface (same "bimmerz" namespace + "captive_dns" u8: 0 = off,
   non-zero = on). Missing key → treat as enabled to preserve the
   previous behaviour on first boot after an OTA. */
#define NVS_NAMESPACE      "bimmerz"
#define NVS_KEY_CAPTIVE    "captive_dns"

static bool captive_dns_enabled(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return true;
    uint8_t v = 1;
    (void)nvs_get_u8(h, NVS_KEY_CAPTIVE, &v);
    nvs_close(h);
    return v != 0;
}

// In-car AP settings. The 172.16.7.0/24 subnet is intentionally
// unusual to reduce the chance of collision with a phone's personal
// hotspot or another in-car device. See docs/firmware.md §12.
#define AP_IP        "172.16.7.1"
#define AP_NETMASK   "255.255.255.0"
#define AP_GATEWAY   "172.16.7.1"
#define AP_CHANNEL   6
#define AP_MAX_CONN  4

// Default password used until a per-device random one is generated and
// persisted to NVS. TODO: replace with NVS-backed random on first boot.
#define AP_DEFAULT_PASSWORD "bimmerzbox"

static esp_err_t configure_ap_netif(esp_netif_t *netif) {
    esp_netif_ip_info_t ip = { 0 };
    ip.ip.addr      = esp_ip4addr_aton(AP_IP);
    ip.netmask.addr = esp_ip4addr_aton(AP_NETMASK);
    ip.gw.addr      = esp_ip4addr_aton(AP_GATEWAY);

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));
    return ESP_OK;
}

static void build_ssid(char *out, size_t out_len) {
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        snprintf(out, out_len, "BimmerzBox");
        return;
    }
    snprintf(out, out_len, "BimmerzBox-%02X%02X", mac[4], mac[5]);
}

esp_err_t wifi_ap_init(void) {
    ESP_LOGI(TAG, "starting AP");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap returned NULL");
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(configure_ap_netif(ap_netif));

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    wifi_config_t ap_cfg = {
        .ap = {
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };
    build_ssid((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = (uint8_t)strlen((const char *)ap_cfg.ap.ssid);
    strncpy((char *)ap_cfg.ap.password, AP_DEFAULT_PASSWORD,
            sizeof(ap_cfg.ap.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP up: ssid=\"%s\" ip=" AP_IP " channel=%d",
             (const char *)ap_cfg.ap.ssid, AP_CHANNEL);

    // Captive-portal DNS hijack: every A query gets the AP IP. Combined
    // with the http_static host-header redirect this triggers iOS /
    // Android / Windows captive-portal detection and pops the dongle's
    // hub UI automatically when a client joins the AP.
    //
    // Toggleable from admin_ui / `/api/config` — some users prefer to
    // suppress the OS captive-portal sheet (which some clients pin to
    // a stripped-down browser view without full JS / WebSocket
    // support). When disabled, the DNS resolver isn't started at all;
    // clients must reach the dongle by IP (`http://172.16.7.1/`).
    // Takes effect on the next boot (settings.html shows a note).
    if (captive_dns_enabled()) {
        dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
        if (start_dns_server(&dns_cfg) == NULL) {
            ESP_LOGW(TAG, "captive DNS server failed to start");
        } else {
            ESP_LOGI(TAG, "captive DNS server up — all queries → " AP_IP);
        }
    } else {
        ESP_LOGI(TAG, "captive DNS disabled via NVS (`captive_dns=0`) — no DNS hijack");
    }
    return ESP_OK;
}
