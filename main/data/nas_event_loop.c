#include "nas_event_loop.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "event_bus.h"
#include "config.h"
#include "wifi_manager.h"
#include "data_source.h"

static const char *TAG = "nas_event_loop";

static TaskHandle_t s_nas_task_hdl = NULL;
static bool s_running = false;
static bool s_fetch_enabled = false;
static SemaphoreHandle_t s_fetch_mutex = NULL;

static bool data_source_fetch_and_publish(void)
{
    if (!data_source_is_connected()) {
        ESP_LOGD(TAG, "Fetch skipped - data source not connected");
        return false;
    }

    if (data_source_poll()) {
        const NasData *data = data_source_get_data();
        if (data && data->is_online) {
            ESP_LOGD(TAG, "Data source fetched, publishing NasData (cpu=%.1f%%, mem=%.1f%%)",
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
                s_fetch_enabled = false;
                ESP_LOGI(TAG, "Data fetch disabled");
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

    if (strlen(g_config.nas_type) > 0) {
        ESP_LOGI(TAG, "Creating data source for type: %s", g_config.nas_type);
        if (!data_source_init(g_config.nas_type)) {
            ESP_LOGE(TAG, "Failed to init data source");
        } else {
            data_source_connect();
        }
    } else {
        ESP_LOGW(TAG, "No NAS type configured, using mock");
        if (!data_source_init("mock")) {
            ESP_LOGE(TAG, "Failed to init mock data source");
        } else {
            data_source_connect();
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

    xSemaphoreGive(s_fetch_mutex);

    return true;
}
