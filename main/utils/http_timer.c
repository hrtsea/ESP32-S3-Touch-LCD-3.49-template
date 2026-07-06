#include "http_timer.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "event_bus.h"

static const char *TAG = "http_timer";

static TimerHandle_t s_http_timer = NULL;
static uint32_t s_interval_ms = 2000;
static bool s_running = false;

static void http_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    event_bus_publish(EVENT_TRIGGER_HTTP_FETCH, NULL, 0);
}

void http_timer_init(void)
{
    s_http_timer = xTimerCreate("http_timer", pdMS_TO_TICKS(s_interval_ms), pdTRUE, NULL, http_timer_cb);
    if (s_http_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP timer");
        return;
    }

    ESP_LOGI(TAG, "HTTP timer initialized (interval=%ums)", s_interval_ms);
}

void http_timer_start(void)
{
    if (s_http_timer == NULL) {
        http_timer_init();
    }

    if (s_running) {
        ESP_LOGW(TAG, "HTTP timer already running");
        return;
    }

    if (xTimerStart(s_http_timer, 0) == pdPASS) {
        s_running = true;
        ESP_LOGI(TAG, "HTTP timer started");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP timer");
    }
}

void http_timer_stop(void)
{
    if (!s_running || s_http_timer == NULL) {
        return;
    }

    if (xTimerStop(s_http_timer, 0) == pdPASS) {
        s_running = false;
        ESP_LOGI(TAG, "HTTP timer stopped");
    }
}

void http_timer_set_interval_ms(uint32_t ms)
{
    s_interval_ms = ms;

    if (s_http_timer != NULL) {
        xTimerChangePeriod(s_http_timer, pdMS_TO_TICKS(ms), 0);
        ESP_LOGI(TAG, "HTTP timer interval changed to %ums", ms);
    }
}

uint32_t http_timer_get_interval_ms(void)
{
    return s_interval_ms;
}

bool http_timer_is_running(void)
{
    return s_running;
}
