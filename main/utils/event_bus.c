#include "event_bus.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

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

static const char *s_event_names[EVENT_MAX] = {
    [EVENT_NONE]                = "NONE",
    [EVENT_ROTATION_CHANGED]    = "ROTATION_CHANGED",
    [EVENT_WIFI_CONNECTED]      = "WIFI_CONNECTED",
    [EVENT_WIFI_DISCONNECTED]   = "WIFI_DISCONNECTED",
    [EVENT_WIFI_SCAN_DONE]      = "WIFI_SCAN_DONE",
    [EVENT_WIFI_SCAN_STARTED]   = "WIFI_SCAN_STARTED",
    [EVENT_CFG_CHANGED]         = "CFG_CHANGED",
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
};

void event_bus_init(void)
{
    if (s_inited) return;
    memset(s_slots, 0, sizeof(s_slots));
    s_mux = xSemaphoreCreateMutex();
    s_inited = true;
    ESP_LOGI(TAG, "event bus initialized (%d event slots)", EVENT_MAX);
}

void event_bus_subscribe(event_id_t id, event_handler_t handler, void *user_data)
{
    if (!s_inited) event_bus_init();
    if (id <= EVENT_NONE || id >= EVENT_MAX || !handler) return;

    xSemaphoreTake(s_mux, portMAX_DELAY);
    event_slot_t *slot = &s_slots[id];
    for (int i = 0; i < slot->count; i++) {
        if (slot->handlers[i].handler == handler) {
            slot->handlers[i].user_data = user_data;
            xSemaphoreGive(s_mux);
            return;
        }
    }
    if (slot->count < MAX_HANDLERS_PER_EVENT) {
        slot->handlers[slot->count].handler = handler;
        slot->handlers[slot->count].user_data = user_data;
        slot->count++;
        ESP_LOGD(TAG, "subscribed handler %p to %s (slot=%d/%d)",
                 (void *)handler, s_event_names[id],
                 slot->count, MAX_HANDLERS_PER_EVENT);
    } else {
        ESP_LOGW(TAG, "handler list full for %s (max=%d)",
                 s_event_names[id], MAX_HANDLERS_PER_EVENT);
    }
    xSemaphoreGive(s_mux);
}

void event_bus_unsubscribe(event_id_t id, event_handler_t handler)
{
    if (!s_inited) return;
    if (id <= EVENT_NONE || id >= EVENT_MAX || !handler) return;

    xSemaphoreTake(s_mux, portMAX_DELAY);
    event_slot_t *slot = &s_slots[id];
    for (int i = 0; i < slot->count; i++) {
        if (slot->handlers[i].handler == handler) {
            for (int j = i; j < slot->count - 1; j++) {
                slot->handlers[j] = slot->handlers[j + 1];
            }
            slot->count--;
            memset(&slot->handlers[slot->count], 0, sizeof(handler_entry_t));
            ESP_LOGD(TAG, "unsubscribed handler %p from %s",
                     (void *)handler, s_event_names[id]);
            break;
        }
    }
    xSemaphoreGive(s_mux);
}

void event_bus_publish(event_id_t id, void *data, size_t len)
{
    if (!s_inited) event_bus_init();
    if (id <= EVENT_NONE || id >= EVENT_MAX) return;

    event_t evt = {
        .id = id,
        .data = data,
        .data_len = len,
    };

    ESP_LOGD(TAG, "publish %s", s_event_names[id]);

    xSemaphoreTake(s_mux, portMAX_DELAY);
    event_slot_t slot_copy = s_slots[id];
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
