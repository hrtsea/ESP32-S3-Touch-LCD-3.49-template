#ifndef WIFI_BRIDGE_H
#define WIFI_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将 esp_wifi_config 的 esp_bus 事件桥接到项目自研 event_bus。
 *
 * 必须在 wifi_cfg_init() 之后、ui_init() 之前调用，确保订阅早于任何
 * WiFi 事件发出。当前桥接 UI 已订阅的三个事件：
 *   wifi:connected    -> EVENT_WIFI_CONNECTED
 *   wifi:disconnected -> EVENT_WIFI_DISCONNECTED
 *   wifi:scan_done    -> EVENT_WIFI_SCAN_DONE
 */
void wifi_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_BRIDGE_H */
