/**
 * @file network_init.c
 * @brief 网络子系统初始化编排
 *
 * esp_bus 总线 → WiFi 管理层（wifi_manager_init，含 esp_wifi_config 库初始化）
 * → 时间服务（sntp_manager_start，SNTP 同步 + 时区应用）→ WebUI 启动。
 * main.cpp 只保留 network_init() 一次调用。
 */
#include <stdio.h>

#include "esp_log.h"

#include "esp_bus.h"
#include "esp_wifi_config.h"   /* wifi_cfg_get_httpd */
#include "esp_http_server.h"
#include "wifi_manager.h"
#include "sntp_manager.h"
#include "webui.h"

#include "user_config.h"

#include "network_init.h"

static const char *TAG = "network";

void network_init(void)
{
    esp_bus_init();

    wifi_manager_init();

    sntp_manager_start();

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
