/* sim/overrides/event_bus.c - 单线程同步事件总线
 *
 * 原版基于 FreeRTOS 队列 + esp_timer 1Hz/10Hz 心跳。
 * PC 单线程模拟器中：
 *  - publish 直接同步调用订阅者（无队列延迟）
 *  - receive 永远返回 false（主循环主动驱动屏幕更新）
 *  - 不创建 1Hz/10Hz 定时器（避免 esp_timer stub 静默失败）
 *
 * 由于 ui_events.c 的事件循环任务在 sim 中不启动（xTaskCreate 失败），
 * sim/main.c 直接调用 sim_update_screens() 推送数据到屏幕，
 * event_bus 此处仅保留 API 兼容性。
 */
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nas_data.h"
#include "config.h"
#include <string.h>

static const char *TAG = "event_bus";

#define MAX_HANDLERS_PER_EVENT 8

typedef struct {
    event_handler_t handler;
    void           *user_data;
} handler_entry_t;

typedef struct {
    handler_entry_t handlers[MAX_HANDLERS_PER_EVENT];
    int             count;
} event_slot_t;

static event_slot_t s_slots[EVENT_MAX];
static SemaphoreHandle_t s_mux = NULL;
static bool s_inited = false;
static NasData s_nas_data_buffer = {0};

static const char *s_event_names[EVENT_MAX] = {
    [EVENT_NONE]                = "NONE",
    [EVENT_ROTATION_CHANGED]    = "ROTATION_CHANGED",
    [EVENT_WIFI_CONNECTED]      = "WIFI_CONNECTED",
    [EVENT_WIFI_DISCONNECTED]   = "WIFI_DISCONNECTED",
    [EVENT_WIFI_SCAN_DONE]      = "WIFI_SCAN_DONE",
    [EVENT_WIFI_SCAN_STARTED]   = "WIFI_SCAN_STARTED",
    [EVENT_CFG_CHANGED]         = "CFG_CHANGED",
    [EVENT_CLOCK_LAYOUT_CHANGED]= "CLOCK_LAYOUT_CHANGED",
    [EVENT_CLOCK_BG_CHANGED]    = "CLOCK_BG_CHANGED",
    [EVENT_CLOCK_TIME_FORMAT_CHANGED] = "CLOCK_TIME_FORMAT_CHANGED",
    [EVENT_QUOTES_CHANGED]      = "QUOTES_CHANGED",
    [EVENT_SHOW_FPS_CHANGED]    = "SHOW_FPS_CHANGED",
    [EVENT_STORAGE_CHANGED]     = "STORAGE_CHANGED",
    [EVENT_AUDIO_PLAY_START]    = "AUDIO_PLAY_START",
    [EVENT_AUDIO_PLAY_STOP]     = "AUDIO_PLAY_STOP",
    [EVENT_AUDIO_RECORD_START]  = "AUDIO_RECORD_START",
    [EVENT_AUDIO_RECORD_STOP]   = "AUDIO_RECORD_STOP",
    [EVENT_AUDIO_VOLUME_CHANGED]= "AUDIO_VOLUME_CHANGED",
    [EVENT_USER_ACTIVITY]       = "USER_ACTIVITY",
    [EVENT_BACKLIGHT_CHANGED]   = "BACKLIGHT_CHANGED",
    [EVENT_TILE_CHANGED]        = "TILE_CHANGED",
    [EVENT_TICK_1HZ]            = "TICK_1HZ",
    [EVENT_TICK_10HZ]           = "TICK_10HZ",
    [EVENT_NAS_DATA_UPDATE]     = "NAS_DATA_UPDATE",
    [EVENT_TRIGGER_HTTP_FETCH]  = "TRIGGER_HTTP_FETCH",
    [EVENT_HTTP_STOP]           = "HTTP_STOP",
    [EVENT_WIFI_PROVISION_START] = "WIFI_PROVISION_START",
    [EVENT_WIFI_PROVISION_STOP] = "WIFI_PROVISION_STOP",
    [EVENT_WIFI_PROVISION_CONFIG_RECEIVED] = "WIFI_PROVISION_CONFIG_RECEIVED",
    [EVENT_DISK_CONFIG_CHANGED] = "DISK_CONFIG_CHANGED",
    [EVENT_FAN_CONFIG_CHANGED]  = "FAN_CONFIG_CHANGED",
    [EVENT_FAN_STATUS_UPDATE]   = "FAN_STATUS_UPDATE",
};

void event_bus_init(void)
{
    if (s_inited) return;
    memset(s_slots, 0, sizeof(s_slots));
    s_mux = xSemaphoreCreateMutex();
    s_inited = true;
    ESP_LOGI(TAG, "sim event bus initialized (synchronous, %d slots)", EVENT_MAX);
}

void event_bus_subscribe(event_id_t id, event_handler_t handler, void *user_data)
{
    if (!s_inited) event_bus_init();
    if (id <= EVENT_NONE || id >= EVENT_MAX || !handler) return;
    if (s_slots[id].count >= MAX_HANDLERS_PER_EVENT) return;

    xSemaphoreTake(s_mux, portMAX_DELAY);
    s_slots[id].handlers[s_slots[id].count].handler = handler;
    s_slots[id].handlers[s_slots[id].count].user_data = user_data;
    s_slots[id].count++;
    xSemaphoreGive(s_mux);
}

void event_bus_unsubscribe(event_id_t id, event_handler_t handler)
{
    (void)id; (void)handler;
}

void event_bus_publish(event_id_t id, void *data, size_t len)
{
    if (!s_inited) event_bus_init();
    if (id <= EVENT_NONE || id >= EVENT_MAX) return;

    event_t evt = { .id = id, .data = data, .data_len = len };

    xSemaphoreTake(s_mux, portMAX_DELAY);
    event_slot_t slot_copy = s_slots[id];
    xSemaphoreGive(s_mux);

    for (int i = 0; i < slot_copy.count; i++) {
        if (slot_copy.handlers[i].handler) {
            slot_copy.handlers[i].handler(&evt, slot_copy.handlers[i].user_data);
        }
    }
}

void event_bus_publish_nas_data(const NasData *data)
{
    if (!s_inited) event_bus_init();
    if (!data) return;

    /* 拷贝到静态缓冲区（与原版一致） */
    memcpy(&s_nas_data_buffer, data, sizeof(NasData));
    s_nas_data_buffer.disk_slot_count = config_get_total_disk_slots();

    event_t evt = {
        .id = EVENT_NAS_DATA_UPDATE,
        .data = &s_nas_data_buffer,
        .data_len = sizeof(NasData),
    };

    xSemaphoreTake(s_mux, portMAX_DELAY);
    event_slot_t slot_copy = s_slots[EVENT_NAS_DATA_UPDATE];
    xSemaphoreGive(s_mux);

    for (int i = 0; i < slot_copy.count; i++) {
        if (slot_copy.handlers[i].handler) {
            slot_copy.handlers[i].handler(&evt, slot_copy.handlers[i].user_data);
        }
    }
}

const char *event_bus_name(event_id_t id)
{
    if (id < 0 || id >= EVENT_MAX) return "UNKNOWN";
    return s_event_names[id];
}

bool event_bus_receive(event_t *evt, TickType_t timeout)
{
    (void)evt; (void)timeout;
    /* sim 中无队列消费；主循环直接驱动屏幕 */
    return false;
}
