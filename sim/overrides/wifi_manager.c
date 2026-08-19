/* sim/overrides/wifi_manager.c - WiFi 管理层（PC 模拟版）
 * 原版订阅 esp_bus 的 WiFi 事件并转发到 event_bus，
 * 模拟器直接通过 wifi_cfg_get_status() 轮询状态，无需事件桥；
 * 扫描缓存等共享状态直接复用 stubs 中的 wifi_cfg_scan mock。
 */
#include "wifi_manager.h"

#include <time.h>

#define WIFI_SCAN_MAX_APS 32u

static wifi_scan_result_t s_scan_results[WIFI_SCAN_MAX_APS];
static size_t s_scan_count = 0;
static uint32_t s_connect_started_ms = 0;
static uint8_t s_last_reason = 0;

void wifi_manager_init(void) {
    /* no-op: 模拟器不订阅 esp_bus WiFi 事件 */
}

void wifi_scan_start(void)
{
    size_t count = 0;
    if (wifi_cfg_scan(s_scan_results, WIFI_SCAN_MAX_APS, &count) == ESP_OK) {
        s_scan_count = count;
    } else {
        s_scan_count = 0;
    }
}

uint16_t wifi_scan_count(void)
{
    return (uint16_t)s_scan_count;
}

const wifi_scan_result_t *wifi_scan_ap(uint16_t idx)
{
    if (idx >= s_scan_count) return NULL;
    return &s_scan_results[idx];
}

void wifi_mark_connect_attempted(void)
{
    s_connect_started_ms = (uint32_t)((clock() * 1000) / CLOCKS_PER_SEC);
}

uint32_t wifi_connect_started_ms(void)
{
    return s_connect_started_ms;
}

void wifi_set_last_reason(uint8_t reason)
{
    s_last_reason = reason;
}

uint8_t wifi_get_last_reason(void)
{
    return s_last_reason;
}
