#include "rgb_nas_monitor.h"
#include "rgb_led.h"
#include "event_bus.h"
#include "nas_data.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "RGB_NAS";

// 监控任务句柄
static TaskHandle_t s_monitor_task_hdl = NULL;
static bool s_monitoring_enabled = false;

// 配置参数
#define MONITOR_INTERVAL_MS  5000  // 5 秒检查一次

// 事件处理函数
static void nas_data_handler(const event_t *evt, void *user_data)
{
    if (!s_monitoring_enabled) {
        return;
    }

    if (evt->id == EVENT_NAS_DATA_UPDATE) {
        const NasData *data = (const NasData *)evt->data;

        // 获取温度
        float temp;
        if (s_config.use_cpu_temp) {
            temp = data->system.temp_cpu;
        } else {
            temp = data->system.temp_sys;
        }

        // 更新 RGB LED
        rgb_led_update_temp(temp);

        ESP_LOGD(TAG, "NAS temp updated: %.1f°C (source=%s)",
                 temp, s_config.use_cpu_temp ? "CPU" : "SYS");
    }
}

// 监控任务
static void monitor_task(void *arg)
{
    ESP_LOGI(TAG, "RGB NAS monitor started");

    while (s_monitoring_enabled) {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "RGB NAS monitor stopped");
    vTaskDelete(NULL);
}

// 初始化
esp_err_t rgb_nas_monitor_init(void)
{
    // 初始化 RGB LED 硬件
    esp_err_t ret = rgb_led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGB LED init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 订阅 NAS 数据更新事件
    event_bus_subscribe(EVENT_NAS_DATA_UPDATE, nas_data_handler, NULL);

    ESP_LOGI(TAG, "RGB NAS monitor initialized");
    return ESP_OK;
}

// 启动监控
void rgb_nas_monitor_start(void)
{
    if (s_monitoring_enabled) {
        return;
    }

    s_monitoring_enabled = true;

    // 设置温度映射模式
    RgbLedConfig config = {
        .mode = RGB_MODE_TEMP_MAP,
        .brightness = 0.25f,
        .temp_min = 35.0f,
        .temp_max = 85.0f,
        .use_cpu_temp = true,
        .enabled = true
    };
    rgb_led_apply_config(&config);

    // 创建监控任务
    xTaskCreate(monitor_task, "rgb_mon", 2048, NULL, 5, &s_monitor_task_hdl);

    ESP_LOGI(TAG, "Monitoring started (temp range: %.1f-%.1f°C)",
             config.temp_min, config.temp_max);
}

// 停止监控
void rgb_nas_monitor_stop(void)
{
    s_monitoring_enabled = false;
    rgb_led_stop();

    if (s_monitor_task_hdl) {
        vTaskDelete(s_monitor_task_hdl);
        s_monitor_task_hdl = NULL;
    }

    ESP_LOGI(TAG, "Monitoring stopped");
}