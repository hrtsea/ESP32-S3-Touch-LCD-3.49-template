#include "nas_event_loop.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "event_bus.h"
#include "app_cfg.h"
#include "data_source.h"

static const char *TAG = "nas_event_loop";

static TaskHandle_t s_nas_task_hdl = NULL;
static bool s_running = false;
static bool s_fetch_enabled = false;
static SemaphoreHandle_t s_fetch_mutex = NULL;

/* NAS 拉取周期定时器（原独立 http_timer 模块，已并入本模块）。
 * 回调仅发布 EVENT_TRIGGER_HTTP_FETCH，实际抓数由 task_nas_data_loop 执行，
 * 避免网络阻塞 esp_timer 任务。 */
static esp_timer_handle_t s_fetch_timer = NULL;
static uint32_t s_fetch_interval_ms = 2000;
static bool s_fetch_timer_running = false;

static void fetch_timer_cb(void *arg)
{
    (void)arg;
    event_bus_publish(EVENT_TRIGGER_HTTP_FETCH, NULL, 0);
}

void nas_event_loop_timer_init(void)
{
    if (s_fetch_timer) return;

    esp_timer_create_args_t args = {
        .callback = fetch_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "nas_fetch_timer",
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&args, &s_fetch_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create fetch timer: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Fetch timer initialized (interval=%ums)", s_fetch_interval_ms);
}

void nas_event_loop_timer_start(void)
{
    if (s_fetch_timer == NULL) {
        nas_event_loop_timer_init();
    }

    if (s_fetch_timer_running) {
        ESP_LOGW(TAG, "Fetch timer already running");
        return;
    }

    esp_err_t err = esp_timer_start_periodic(s_fetch_timer, (uint64_t)s_fetch_interval_ms * 1000);
    if (err == ESP_OK) {
        s_fetch_timer_running = true;
        ESP_LOGI(TAG, "Fetch timer started");
    } else {
        ESP_LOGE(TAG, "Failed to start fetch timer: %s", esp_err_to_name(err));
    }
}

void nas_event_loop_timer_stop(void)
{
    if (!s_fetch_timer_running || s_fetch_timer == NULL) {
        return;
    }

    esp_err_t err = esp_timer_stop(s_fetch_timer);
    if (err == ESP_OK) {
        s_fetch_timer_running = false;
        ESP_LOGI(TAG, "Fetch timer stopped");
    }
}

void nas_event_loop_timer_set_interval_ms(uint32_t ms)
{
    s_fetch_interval_ms = ms;

    if (s_fetch_timer != NULL && s_fetch_timer_running) {
        esp_timer_stop(s_fetch_timer);
        esp_timer_start_periodic(s_fetch_timer, (uint64_t)ms * 1000);
        ESP_LOGI(TAG, "Fetch timer interval changed to %ums", ms);
    }
}

uint32_t nas_event_loop_timer_get_interval_ms(void)
{
    return s_fetch_interval_ms;
}

bool nas_event_loop_timer_is_running(void)
{
    return s_fetch_timer_running;
}

static bool data_source_fetch_and_publish(void)
{
    if (!data_source_is_connected()) {
        ESP_LOGD(TAG, "Fetch skipped - data source not connected");
        return false;
    }

    if (data_source_poll()) {
        const NasData *data = data_source_get_data();
        if (data && data->is_online) {
            ESP_LOGI(TAG, "Data source fetched, publishing NasData (cpu=%.1f%%, mem=%.1f%%)",
                     data->system.cpu_pct, data->system.ram_pct);
            event_bus_publish_nas_data(data);
            return true;
        }
    }

    return false;
}

static void task_nas_data_loop(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "NAS data loop task started (event-driven)");

    while (s_running) {
        event_t evt;
        if (!event_bus_receive(&evt, portMAX_DELAY)) {
            continue;
        }

        xSemaphoreTake(s_fetch_mutex, portMAX_DELAY);

        switch (evt.id) {
            case EVENT_TRIGGER_HTTP_FETCH:
                if (s_fetch_enabled) {
                    data_source_fetch_and_publish();
                }
                break;

            case EVENT_WIFI_CONNECTED:
                s_fetch_enabled = true;
                ESP_LOGI(TAG, "Data fetch enabled (WiFi connected)");
                break;

            case EVENT_WIFI_DISCONNECTED:
            case EVENT_HTTP_STOP:
                if (nas_type_from_string(app_cfg_get_nas_type()) == NAS_MOCK) {
                    // mock 数据源为本地模拟，WiFi 状态不影响数据抓取
                    ESP_LOGD(TAG, "Fetch keep enabled (mock source, no WiFi needed)");
                } else {
                    s_fetch_enabled = false;
                    ESP_LOGI(TAG, "Data fetch disabled");
                }
                break;

            default:
                break;
        }

        xSemaphoreGive(s_fetch_mutex);
    }

    vTaskDelete(NULL);
}

void nas_event_loop_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "NAS event loop already running");
        return;
    }

    s_running = true;
    s_fetch_enabled = false;

    s_fetch_mutex = xSemaphoreCreateMutex();
    if (s_fetch_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create fetch mutex");
        return;
    }

    const char *type_id;
    if (strlen(app_cfg_get_nas_type()) > 0) {
        type_id = app_cfg_get_nas_type();
    } else {
        ESP_LOGW(TAG, "No NAS type configured, using mock");
        type_id = "mock";
    }

    ESP_LOGI(TAG, "Creating data source for type: %s", type_id);
    if (!data_source_init(type_id)) {
        ESP_LOGE(TAG, "Failed to init data source");
    } else {
        data_source_connect();

        // mock 数据源为本地模拟数据，无需 WiFi 连接即可轮询更新
        // 注意：nas_type_from_string("") 不会返回 NAS_MOCK，故必须基于实际使用的 type_id 判断
        if (nas_type_from_string(type_id) == NAS_MOCK) {
            s_fetch_enabled = true;
            ESP_LOGI(TAG, "Data fetch enabled (mock source, no WiFi needed)");
        }
    }

    xTaskCreate(task_nas_data_loop, "nas_data_loop", 8192, NULL, 1, &s_nas_task_hdl);

    ESP_LOGI(TAG, "NAS event loop started");
}

void nas_event_loop_stop(void)
{
    if (!s_running) {
        return;
    }

    s_running = false;

    if (s_nas_task_hdl != NULL) {
        vTaskDelete(s_nas_task_hdl);
        s_nas_task_hdl = NULL;
    }

    data_source_disconnect();

    if (s_fetch_mutex != NULL) {
        vSemaphoreDelete(s_fetch_mutex);
        s_fetch_mutex = NULL;
    }

    ESP_LOGI(TAG, "NAS event loop stopped");
}

bool nas_event_loop_is_running(void)
{
    return s_running;
}

bool nas_event_loop_switch_source(const char *nas_type_id)
{
    if (!s_running) {
        ESP_LOGW(TAG, "NAS event loop not running");
        return false;
    }

    xSemaphoreTake(s_fetch_mutex, portMAX_DELAY);

    if (!data_source_switch(nas_type_id)) {
        xSemaphoreGive(s_fetch_mutex);
        ESP_LOGE(TAG, "Failed to switch data source");
        return false;
    }

    ESP_LOGI(TAG, "Data source switched to: %s", nas_type_id);

    // 切换后同步抓取开关：mock 数据源为本地模拟，无需 WiFi 即可轮询
    bool is_mock = (nas_type_id != NULL && nas_type_from_string(nas_type_id) == NAS_MOCK);
    if (is_mock) {
        s_fetch_enabled = true;
        ESP_LOGI(TAG, "Data fetch enabled (mock source, no WiFi needed)");
    } else {
        s_fetch_enabled = false;
        ESP_LOGI(TAG, "Data fetch disabled, waiting for WiFi");
    }

    xSemaphoreGive(s_fetch_mutex);

    return true;
}
