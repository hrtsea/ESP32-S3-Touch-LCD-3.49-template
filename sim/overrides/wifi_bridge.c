/* sim/overrides/wifi_bridge.c - WiFi 事件桥 no-op
 * 原版订阅 esp_bus 的 WiFi 事件并转发到 event_bus。
 * PC 模拟器直接通过 wifi_cfg_get_status() 轮询状态，
 * 无需事件桥。
 */
#include "wifi_bridge.h"

void wifi_bridge_init(void) {
    /* no-op: 模拟器不订阅 esp_bus WiFi 事件 */
}
