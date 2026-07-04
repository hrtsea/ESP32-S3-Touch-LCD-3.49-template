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
#include "disp_driver.h"
#include "sntp_manager.h"

static const char *TAG = "wifi_manager";

#define WIFI_MAX_SCAN_AP 16

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t auth;
} wifi_scan_ap_t;

wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];
uint16_t       g_wifi_scan_n = 0;
bool           g_wifi_scanning = false;
static bool    g_wifi_inited = false;
bool           g_wifi_connected = false;
char           g_wifi_curr_ssid[33] = {0};
uint8_t        g_wifi_last_reason = 0;
int8_t         g_wifi_last_rssi = 0;
uint32_t       g_wifi_connect_started_ms = 0;

static lv_obj_t *g_ip_label = NULL;

static void ip_label_ensure(void)
{
    if (g_ip_label) return;
    g_ip_label = lv_label_create(lv_layer_top());
    lv_label_set_text(g_ip_label, "");
    lv_obj_set_style_text_color(g_ip_label, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(g_ip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(g_ip_label, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(g_ip_label, LV_OPA_50, 0);
    lv_obj_set_style_pad_left(g_ip_label, 4, 0);
    lv_obj_set_style_pad_right(g_ip_label, 4, 0);
    lv_obj_set_style_pad_top(g_ip_label, 1, 0);
    lv_obj_set_style_pad_bottom(g_ip_label, 1, 0);
    lv_obj_set_style_radius(g_ip_label, 3, 0);
    lv_obj_align(g_ip_label, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
}

void wifi_ip_label_set(const char *text)
{
    if (!lvgl_lock(50)) return;
    ip_label_ensure();
    if (text && *text) {
        lv_label_set_text(g_ip_label, text);
        lv_obj_clear_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
}

static uint8_t g_wifi_fail_count = 0;
static bool    g_wifi_roaming_scan = false;
#define WIFI_FAILS_BEFORE_ROAM 3

static bool wifi_has_remembered(const char *ssid)
{
    if (!ssid || !*ssid) return false;
    char key[16] = {0};
    strncpy(key, ssid, 15);
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
                wifi_ip_label_set(NULL);
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
                strncpy(g_cfg.last_ssid, g_wifi_curr_ssid, sizeof(g_cfg.last_ssid) - 1);
                cfg_save();
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
                        cfg_get_ssid_pass(ssid, pass, sizeof(pass));
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
        char buf[40];
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ev->ip_info.ip));
        wifi_ip_label_set(buf);
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