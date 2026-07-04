#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "lvgl.h"
#include "user_config.h"
#include "app_cfg.h"
#include "sntp_manager.h"
#include "wifi_manager.h"

static const char *TAG = "wifi_manager";

wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];
uint16_t       g_wifi_scan_n = 0;
bool           g_wifi_scanning = false;
static bool    g_wifi_inited = false;
bool           g_wifi_connected = false;
char           g_wifi_curr_ssid[33] = {0};
uint8_t        g_wifi_last_reason = 0;
int8_t         g_wifi_last_rssi = 0;
uint32_t       g_wifi_connect_started_ms = 0;

static wifi_status_cb_t g_wifi_status_cb = NULL;

void wifi_manager_register_status_cb(wifi_status_cb_t cb)
{
    g_wifi_status_cb = cb;
}

static uint8_t g_wifi_fail_count = 0;
static bool    g_wifi_roaming_scan = false;
#define WIFI_FAILS_BEFORE_ROAM 3

static bool wifi_has_remembered(const char *ssid)
{
    if (!ssid || !*ssid) return false;
    char key[16] = {0};
    size_t len = strlen(ssid);
    if (len > 15) len = 15;
    memcpy(key, ssid, len);
    key[len] = '\0';
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = 0;
    esp_err_t er = nvs_get_str(h, key, NULL, &l);
    nvs_close(h);
    return er == ESP_OK;
}

static bool wifi_has_remembered(const char *ssid);
static void wifi_kick_roam_scan(void);
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
void wifi_connect(const char *ssid, const char *pass);

static void wifi_kick_roam_scan(void)
{
    if (g_wifi_scanning) return;
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    g_wifi_roaming_scan = true;
    g_wifi_scanning = true;
    esp_err_t er = esp_wifi_scan_start(&sc, false);
    ESP_LOGI(TAG, "wifi: roaming scan -> %s", esp_err_to_name(er));
    if (er != ESP_OK) {
        g_wifi_scanning = false;
        g_wifi_roaming_scan = false;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "wifi: STA_START");
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *d =
                    (wifi_event_sta_disconnected_t *)data;
                g_wifi_last_reason = d ? d->reason : 0;
                ESP_LOGW(TAG, "wifi: disconnected reason=%u",
                         (unsigned)g_wifi_last_reason);
                g_wifi_connected = false;
                if (g_wifi_status_cb) g_wifi_status_cb(false, NULL);
                g_wifi_fail_count++;
                if (g_wifi_curr_ssid[0] && !g_wifi_scanning &&
                    g_wifi_fail_count < WIFI_FAILS_BEFORE_ROAM) {
                    esp_wifi_connect();
                } else if (g_cfg.wifi_autoconnect && !g_wifi_scanning) {
                    wifi_kick_roam_scan();
                }
                break;
            }
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "wifi: connected to %s", g_wifi_curr_ssid);
                g_wifi_connected = true;
                g_wifi_last_reason = 0;
                g_wifi_fail_count = 0;
                size_t ssid_len = strlen(g_wifi_curr_ssid);
                if (ssid_len >= sizeof(g_cfg.last_ssid)) ssid_len = sizeof(g_cfg.last_ssid) - 1;
                memcpy(g_cfg.last_ssid, g_wifi_curr_ssid, ssid_len);
                g_cfg.last_ssid[ssid_len] = '\0';
                app_cfg_save();
                break;
            case WIFI_EVENT_SCAN_DONE: {
                static wifi_ap_record_t recs[WIFI_MAX_SCAN_AP];
                uint16_t apc = WIFI_MAX_SCAN_AP;
                if (esp_wifi_scan_get_ap_records(&apc, recs) == ESP_OK) {
                    g_wifi_scan_n = apc;
                    for (int i = 0; i < apc; i++) {
                        strncpy(g_wifi_scan[i].ssid, (const char *)recs[i].ssid,
                                sizeof(g_wifi_scan[i].ssid) - 1);
                        g_wifi_scan[i].ssid[sizeof(g_wifi_scan[i].ssid) - 1] = 0;
                        g_wifi_scan[i].rssi = recs[i].rssi;
                        g_wifi_scan[i].auth = (uint8_t)recs[i].authmode;
                    }
                } else {
                    g_wifi_scan_n = 0;
                }
                ESP_LOGI(TAG, "wifi: scan done, n=%u", (unsigned)g_wifi_scan_n);
                g_wifi_scanning = false;
                if (g_wifi_roaming_scan) {
                    g_wifi_roaming_scan = false;
                    int best_i = -1;
                    int best_rssi = -127;
                    for (int i = 0; i < g_wifi_scan_n; i++) {
                        if (!wifi_has_remembered(g_wifi_scan[i].ssid)) continue;
                        if (g_wifi_scan[i].rssi > best_rssi) {
                            best_rssi = g_wifi_scan[i].rssi;
                            best_i = i;
                        }
                    }
                    if (best_i >= 0) {
                        const char *ssid = g_wifi_scan[best_i].ssid;
                        char pass[65] = {0};
                        app_cfg_get_ssid_pass(ssid, pass, sizeof(pass));
                        ESP_LOGI(TAG, "wifi: roaming to known %s rssi=%d",
                                 ssid, best_rssi);
                        g_wifi_fail_count = 0;
                        wifi_connect(ssid, pass);
                        break;
                    } else {
                        ESP_LOGI(TAG, "wifi: no remembered AP visible (%u seen)",
                                 (unsigned)g_wifi_scan_n);
                        g_wifi_fail_count = 0;
                    }
                }
                if (g_wifi_curr_ssid[0] && !g_wifi_connected) {
                    esp_wifi_connect();
                }
                break;
            }
            default: break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "wifi: got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        sntp_manager_start();
        if (g_wifi_status_cb) {
            char buf[40];
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ev->ip_info.ip));
            g_wifi_status_cb(true, buf);
        }
    }
}

static void wifi_autoconnect(void)
{
    if (!g_cfg.wifi_autoconnect) {
        ESP_LOGI(TAG, "auto-connect: disabled in settings");
        return;
    }

    if (!g_cfg.last_ssid[0] && DEFAULT_WIFI_SSID[0]) {
        strncpy(g_cfg.last_ssid, DEFAULT_WIFI_SSID,
                sizeof(g_cfg.last_ssid) - 1);
    }

    char pass[65] = {0};
    bool have_pass = app_cfg_get_ssid_pass(g_cfg.last_ssid, pass, sizeof(pass));
    if (!have_pass && DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0] &&
        strcmp(g_cfg.last_ssid, DEFAULT_WIFI_SSID) == 0) {
        strncpy(pass, DEFAULT_WIFI_PASS, sizeof(pass) - 1);
        app_cfg_save_ssid_pass(g_cfg.last_ssid, pass);
        have_pass = true;
    }

    if (g_cfg.last_ssid[0] && have_pass) {
        ESP_LOGI(TAG, "auto-connect: %s (pass_len=%u)",
                 g_cfg.last_ssid, (unsigned)strlen(pass));
        wifi_connect(g_cfg.last_ssid, pass);
    } else {
        ESP_LOGI(TAG, "auto-connect: no credentials yet (use Settings -> Wi-Fi)");
    }
}

void wifi_manager_init(void)
{
    if (g_wifi_inited) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());
    g_wifi_inited = true;

    wifi_autoconnect();
}

void wifi_start_scan(void)
{
    wifi_manager_init();
    g_wifi_scanning = true;
    esp_wifi_disconnect();
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t er = esp_wifi_scan_start(&sc, false);
    if (er != ESP_OK) {
        ESP_LOGW(TAG, "wifi: scan_start=%s", esp_err_to_name(er));
        g_wifi_scanning = false;
    }
}

const char *wifi_reason_str(uint8_t reason)
{
    switch (reason) {
        case 0:                              return "";
        case WIFI_REASON_AUTH_EXPIRE:        return "auth expired";
        case WIFI_REASON_AUTH_LEAVE:         return "auth leave";
        case WIFI_REASON_ASSOC_EXPIRE:       return "assoc expired";
        case WIFI_REASON_ASSOC_TOOMANY:      return "AP full";
        case WIFI_REASON_NOT_AUTHED:         return "not authed";
        case WIFI_REASON_NOT_ASSOCED:        return "not assoced";
        case WIFI_REASON_ASSOC_LEAVE:        return "assoc leave";
        case WIFI_REASON_ASSOC_NOT_AUTHED:   return "assoc not authed";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD:return "pwrcap bad";
        case WIFI_REASON_BEACON_TIMEOUT:     return "beacon timeout";
        case WIFI_REASON_NO_AP_FOUND:        return "AP not found";
        case WIFI_REASON_AUTH_FAIL:          return "auth fail (wrong pass?)";
        case WIFI_REASON_ASSOC_FAIL:         return "assoc fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:  return "handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL:    return "connection fail";
        case WIFI_REASON_AP_TSF_RESET:       return "AP tsf reset";
        case WIFI_REASON_ROAMING:            return "roaming";
        default:                             return "disconnected";
    }
}

void wifi_connect(const char *ssid, const char *pass)
{
    wifi_manager_init();
    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass ? pass : "", sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = (pass && pass[0])
                                    ? WIFI_AUTH_WPA2_PSK
                                    : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    strncpy(g_wifi_curr_ssid, ssid, sizeof(g_wifi_curr_ssid) - 1);
    g_wifi_curr_ssid[sizeof(g_wifi_curr_ssid) - 1] = 0;
    g_wifi_connect_started_ms = lv_tick_get();
    g_wifi_last_reason = 0;
    esp_wifi_disconnect();
    esp_err_t er = esp_wifi_connect();
    ESP_LOGI(TAG, "wifi: connect %s pass_len=%u auth=%d -> %s",
             ssid, (unsigned)(pass ? strlen(pass) : 0),
             (int)wc.sta.threshold.authmode, esp_err_to_name(er));
}