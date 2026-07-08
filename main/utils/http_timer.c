#include "http_timer.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "event_bus.h"

static const char *TAG = "http_timer";

static esp_timer_handle_t s_http_timer = NULL;
static uint32_t s_interval_ms = 2000;
static bool s_running = false;

static void http_timer_cb(void *arg)
{
    (void)arg;
    event_bus_publish(EVENT_TRIGGER_HTTP_FETCH, NULL, 0);
}

void http_timer_init(void)
{
    if (s_http_timer) return;

    esp_timer_create_args_t args = {
        .callback = http_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "http_timer",
        .skip_unhandled_events = true,
    };
    esp_err_t err = esp_timer_create(&args, &s_http_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create HTTP timer: %s", esp_err_to_name(err));
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

    esp_err_t err = esp_timer_start_periodic(s_http_timer, (uint64_t)s_interval_ms * 1000);
    if (err == ESP_OK) {
        s_running = true;
        ESP_LOGI(TAG, "HTTP timer started");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP timer: %s", esp_err_to_name(err));
    }
}

void http_timer_stop(void)
{
    if (!s_running || s_http_timer == NULL) {
        return;
    }

    esp_err_t err = esp_timer_stop(s_http_timer);
    if (err == ESP_OK) {
        s_running = false;
        ESP_LOGI(TAG, "HTTP timer stopped");
    }
}

void http_timer_set_interval_ms(uint32_t ms)
{
    s_interval_ms = ms;

    if (s_http_timer != NULL && s_running) {
        esp_timer_stop(s_http_timer);
        esp_timer_start_periodic(s_http_timer, (uint64_t)ms * 1000);
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
