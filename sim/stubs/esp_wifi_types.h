/* sim/stubs/esp_wifi_types.h - WiFi 类型 stub */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi 认证模式（esp_wifi_config.h wifi_scan_result_t 使用） */
typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_ENTERPRISE,
    WIFI_AUTH_WPA2_ENTERPRISE,
    WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
    WIFI_AUTH_WAPI_PSK,
    WIFI_AUTH_MAX
} wifi_auth_mode_t;

#ifdef __cplusplus
}
#endif
