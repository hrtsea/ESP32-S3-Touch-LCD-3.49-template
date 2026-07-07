#include "wifi_provision.h"
#include "wifi_provision_dns.h"
#include "wifi_provision_http.h"
#include "wifi_manager.h"
#include "event_bus.h"
#include "app_cfg.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "freertos/timers.h"

static const char *TAG = "wifi_provision";

static wifi_prov_state_t s_state = WIFI_PROV_IDLE;
static bool s_inited = false;
static TimerHandle_t s_timeout_timer = NULL;

static void s_on_wifi_event(const event_t *evt, void *user_data);
static void s_timeout_timer_cb(TimerHandle_t timer);

void wifi_provision_init(void)
{
    if (s_inited) return;

    wifi_provision_dns_init();
    wifi_provision_http_init();

    event_bus_subscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event, NULL);

    s_inited = true;
    ESP_LOGI(TAG, "provisioning module initialized");
}

static void s_start_ap(const char *ssid, const char *pass)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(ssid),
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        }
    };
    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid) - 1);

    if (pass && pass[0]) {
        strncpy((char *)ap_config.ap.password, pass, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }

    wifi_manager_set_mode(WIFI_OP_MODE_AP_STA, &ap_config);

    esp_netif_ip_info_t ap_ip_info;
    memset(&ap_ip_info, 0, sizeof(ap_ip_info));
    IP4_ADDR(&ap_ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ap_ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ap_ip_info.netmask, 255, 255, 255, 0);
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_set_ip_info(ap_netif, &ap_ip_info);
    }

    ESP_LOGI(TAG, "AP started: %s (auth=%d)", ssid, ap_config.ap.authmode);
}

void wifi_provision_start(const char *ap_ssid, const char *ap_pass)
{
    if (s_state != WIFI_PROV_IDLE) {
        ESP_LOGW(TAG, "already in state %d", s_state);
        return;
    }

    wifi_provision_init();

    const char *ssid = ap_ssid ? ap_ssid : WIFI_PROV_AP_SSID;
    const char *pass = ap_pass ? ap_pass : WIFI_PROV_AP_PASS;

    ESP_LOGI(TAG, "starting provisioning mode, AP=%s", ssid);

    s_state = WIFI_PROV_PROVISIONING;

    s_start_ap(ssid, pass);

    wifi_provision_dns_start();
    wifi_provision_http_start();

    event_bus_publish(EVENT_WIFI_PROVISION_START, NULL, 0);

    if (s_timeout_timer == NULL) {
        s_timeout_timer = xTimerCreate("prov_timeout", pdMS_TO_TICKS(WIFI_PROV_TIMEOUT_MS),
                                        pdFALSE, NULL, s_timeout_timer_cb);
    }
    if (s_timeout_timer) {
        xTimerStart(s_timeout_timer, 0);
        ESP_LOGI(TAG, "provisioning timeout timer started (5min)");
    }

    ESP_LOGI(TAG, "provisioning started: connect to %s and access http://192.168.4.1", ssid);
}

void wifi_provision_stop(void)
{
    if (s_state == WIFI_PROV_IDLE) return;

    ESP_LOGI(TAG, "stopping provisioning mode");

    if (s_timeout_timer) {
        xTimerStop(s_timeout_timer, 0);
    }

    wifi_provision_http_stop();
    wifi_provision_dns_stop();

    wifi_manager_set_mode(WIFI_OP_MODE_STA_ONLY, NULL);

    s_state = WIFI_PROV_IDLE;

    event_bus_publish(EVENT_WIFI_PROVISION_STOP, NULL, 0);

    ESP_LOGI(TAG, "provisioning stopped");
}

static void s_timeout_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    ESP_LOGW(TAG, "provisioning timeout, restarting...");
    wifi_provision_stop();
    wifi_provision_start(NULL, NULL);
}

wifi_prov_state_t wifi_provision_get_state(void)
{
    return s_state;
}

void wifi_provision_on_config(const char *ssid, const char *pass)
{
    if (s_state != WIFI_PROV_PROVISIONING) {
        ESP_LOGW(TAG, "not in provisioning state, ignoring config");
        return;
    }

    if (!ssid || !ssid[0]) {
        ESP_LOGW(TAG, "invalid SSID");
        return;
    }

    ESP_LOGI(TAG, "received wifi config: ssid=%s pass_len=%u", ssid, (unsigned)(pass ? strlen(pass) : 0));

    s_state = WIFI_PROV_CONNECTING;

    wifi_provision_http_stop();
    wifi_provision_dns_stop();

    app_cfg_wifi_pending_set(ssid, pass ? pass : "");

    wifi_manager_set_mode(WIFI_OP_MODE_STA_ONLY, NULL);

    wifi_connect(ssid, pass ? pass : "");

    static char config_buf[128];
    snprintf(config_buf, sizeof(config_buf), "{\"ssid\":\"%s\",\"pass\":\"%s\"}", ssid, pass ? pass : "");
    event_bus_publish(EVENT_WIFI_PROVISION_CONFIG_RECEIVED, config_buf, strlen(config_buf) + 1);

    ESP_LOGI(TAG, "connecting to %s...", ssid);
}

static void s_on_wifi_event(const event_t *evt, void *user_data)
{
    (void)user_data;

    if (s_state == WIFI_PROV_CONNECTING) {
        if (evt->id == EVENT_WIFI_CONNECTED) {
            ESP_LOGI(TAG, "wifi connected successfully!");
            s_state = WIFI_PROV_CONNECTED;
            app_cfg_wifi_pending_commit();
        } else if (evt->id == EVENT_WIFI_DISCONNECTED) {
            ESP_LOGW(TAG, "wifi connect failed, returning to provisioning");
            s_state = WIFI_PROV_PROVISIONING;
            wifi_provision_start(NULL, NULL);
        }
    } else if (s_state == WIFI_PROV_CONNECTED && evt->id == EVENT_WIFI_DISCONNECTED) {
        s_state = WIFI_PROV_IDLE;
    }
}
