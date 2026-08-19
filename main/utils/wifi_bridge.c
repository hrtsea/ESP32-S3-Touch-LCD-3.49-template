/**
 * @file wifi_bridge.c
 * @brief WiFi 事件桥 + 共享状态
 *
 * WiFi 的配置/连接/扫描/状态操作由调用方直接使用 esp_wifi_config 库 API，
 * 本文件只负责：
 *  1) esp_bus WiFi 事件 → event_bus 转发；
 *  2) UI 所需的共享状态：扫描缓存、连接发起时间、断开原因。
 */
#include "wifi_bridge.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_bus.h"           /* esp_bus_sub */
#include "esp_wifi_config.h"   /* WIFI_EVT / WIFI_CFG_EVT_* */
#include "event_bus.h"

static const char *TAG = "wifi";

/* ---- 共享状态 ---- */
#define WIFI_SCAN_MAX_APS 32u

static wifi_scan_result_t s_scan_results[WIFI_SCAN_MAX_APS];
static size_t s_scan_count = 0;
static uint32_t s_connect_started_ms = 0;
static uint8_t s_last_reason = 0;

/* ============================================================
 * 扫描结果缓存（UI 列表显示共享同一份）
 * ============================================================ */

void wifi_scan_start(void)
{
    s_scan_count = 0;
    wifi_cfg_scan(s_scan_results, WIFI_SCAN_MAX_APS, &s_scan_count);
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

/* ============================================================
 * 连接发起时间（UI 超时判断）
 * ============================================================ */

void wifi_mark_connect_attempted(void)
{
    s_connect_started_ms = esp_timer_get_time() / 1000;
}

uint32_t wifi_connect_started_ms(void)
{
    return s_connect_started_ms;
}

/* ============================================================
 * 断开原因（wifi:disconnected 事件提取）
 * ============================================================ */

void wifi_set_last_reason(uint8_t reason)
{
    s_last_reason = reason;
}

uint8_t wifi_get_last_reason(void)
{
    return s_last_reason;
}

/* ============================================================
 * esp_bus WiFi 事件 → event_bus 转发
 * ============================================================ */

/* event_bus_publish 第二参为 void*，esp_bus 回调给的是 const void*，需去 const。
 * 事件数据本身为只读（库内部生命周期），UI 仅读取不修改，安全。 */
static void bridge_forward(event_id_t id, const void *data, size_t len)
{
    event_bus_publish(id, (void *)data, len);
}

static void on_connected(const char *event, const void *data, size_t len, void *ctx)
{
    (void)event;
    (void)ctx;
    ESP_LOGI(TAG, "wifi:connected -> EVENT_WIFI_CONNECTED");
    bridge_forward(EVENT_WIFI_CONNECTED, data, len);
}

static void on_disconnected(const char *event, const void *data, size_t len, void *ctx)
{
    (void)event;
    (void)ctx;
    if (data && len >= sizeof(wifi_disconnected_t)) {
        const wifi_disconnected_t *d = (const wifi_disconnected_t *)data;
        wifi_set_last_reason(d->reason);
    }
    ESP_LOGI(TAG, "wifi:disconnected -> EVENT_WIFI_DISCONNECTED");
    bridge_forward(EVENT_WIFI_DISCONNECTED, data, len);
}

static void on_scan_done(const char *event, const void *data, size_t len, void *ctx)
{
    (void)event;
    (void)ctx;
    ESP_LOGI(TAG, "wifi:scan_done -> EVENT_WIFI_SCAN_DONE");
    bridge_forward(EVENT_WIFI_SCAN_DONE, data, len);
}

void wifi_bridge_init(void)
{
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), on_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), on_disconnected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_SCAN_DONE), on_scan_done, NULL);
    ESP_LOGI(TAG, "esp_bus -> event_bus bridge initialized");
}
