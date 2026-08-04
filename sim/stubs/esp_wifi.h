/* sim/stubs/esp_wifi.h - WiFi 接口 stub（空实现） */
#pragma once
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* stub: 不实际使用 WiFi */
static inline int esp_wifi_init(void *cfg) { (void)cfg; return 0; }
static inline int esp_wifi_set_mode(int mode) { (void)mode; return 0; }
static inline int esp_wifi_start(void) { return 0; }
static inline int esp_wifi_stop(void) { return 0; }
static inline int esp_wifi_connect(void) { return 0; }
static inline int esp_wifi_disconnect(void) { return 0; }
static inline int esp_wifi_scan_start(void *conf, bool block) { (void)conf; (void)block; return 0; }
static inline int esp_wifi_scan_get_ap_num(uint16_t *n) { if (n) *n = 0; return 0; }

#ifdef __cplusplus
}
#endif
