/**
 * @file network_init.c
 * @brief 网络子系统初始化
 *
 * 从 main.cpp 迁出：esp_bus 总线 + WiFi 配置（wifi_cfg）+ 桥接 + WebUI 启动。
 * main.cpp 只保留 network_init() 一次调用。
 */
#include <stdio.h>

#include "esp_log.h"

#include "esp_bus.h"
#include "esp_wifi_config.h"
#include "esp_http_server.h"
#include "wifi_bridge.h"
#include "webui.h"

#include "user_config.h"
#include "app_cfg.h"

#include "network_init.h"

static const char *TAG = "network";

void network_init(void)
{
    esp_bus_init();

    wifi_cfg_config_t cfg = {
        .default_networks = (wifi_network_t[]){
            { DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, 10 },
        },
        .default_network_count = (DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0]) ? 1 : 0,
        .max_retry_per_network = 3,
        .retry_interval_ms = 5000,
        .retry_max_interval_ms = 30000,
        .max_reconnect_attempts = 5,
        .on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_RESTART,
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .http_post_prov_mode = WIFI_HTTP_API_ONLY,
        .default_ap = {
            .ssid = DEFAULT_AP_SSID,
            .password = DEFAULT_AP_PASSWORD,
        },
        .enable_ap = true,
        .http = {
            .api_base_path = "/api/wifi",
            .enable_auth = true,
            .auth_username = WEBUI_AUTH_USER,
            .auth_password = WEBUI_AUTH_PASSWORD,
        },
    };
    wifi_cfg_init(&cfg);

    wifi_bridge_init();

    webui_set_auth(WEBUI_AUTH_USER, WEBUI_AUTH_PASSWORD);

    httpd_handle_t srv = wifi_cfg_get_httpd();
    if (srv) {
        if (webui_start_with_httpd(srv) != ESP_OK) {
            ESP_LOGW(TAG, "webui_start_with_httpd failed");
        }
    } else {
        if (webui_start() != ESP_OK) {
            ESP_LOGW(TAG, "webui_start failed");
        }
    }
}
