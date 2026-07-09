#include "wifi_bridge.h"

#include "esp_log.h"
#include "esp_bus.h"           /* esp_bus_sub */
#include "esp_wifi_config.h"   /* WIFI_EVT / WIFI_CFG_EVT_* 宏 */
#include "event_bus.h"
#include "wifi_adapter.h"      /* wifi_set_last_reason */

static const char *TAG = "wifi_bridge";

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
