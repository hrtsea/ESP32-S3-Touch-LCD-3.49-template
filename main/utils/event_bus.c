#include "event_bus.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nas_data.h"

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
static QueueHandle_t s_event_queue = NULL;
static NasData s_nas_data_buffer = {0};
static SemaphoreHandle_t s_nas_data_mux = NULL;

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
};

void event_bus_init(void)
{
    if (s_inited) return;
    memset(s_slots, 0, sizeof(s_slots));
    s_mux = xSemaphoreCreateMutex();
    s_nas_data_mux = xSemaphoreCreateMutex();
    s_event_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(event_t));
    s_inited = true;
    ESP_LOGI(TAG, "event bus initialized (%d event slots, queue len=%d)", EVENT_MAX, EVENT_QUEUE_LEN);
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

    if (s_event_queue) {
        xQueueSend(s_event_queue, &evt, 0);
    }
}

void event_bus_publish_nas_data(const NasData *data)
{
    if (!s_inited) event_bus_init();
    if (!data) return;

    xSemaphoreTake(s_nas_data_mux, portMAX_DELAY);
    memcpy(&s_nas_data_buffer, data, sizeof(NasData));
    xSemaphoreGive(s_nas_data_mux);

    event_t evt = {
        .id = EVENT_NAS_DATA_UPDATE,
        .data = &s_nas_data_buffer,
        .data_len = sizeof(NasData),
    };

    ESP_LOGD(TAG, "publish %s", s_event_names[EVENT_NAS_DATA_UPDATE]);

    xSemaphoreTake(s_mux, portMAX_DELAY);
    event_slot_t slot_copy = s_slots[EVENT_NAS_DATA_UPDATE];
    xSemaphoreGive(s_mux);

    for (int i = 0; i < slot_copy.count; i++) {
        if (slot_copy.handlers[i].handler) {
            slot_copy.handlers[i].handler(&evt, slot_copy.handlers[i].user_data);
        }
    }

    if (s_event_queue) {
        xQueueSend(s_event_queue, &evt, 0);
    }
}

const char *event_bus_name(event_id_t id)
{
    if (id < 0 || id >= EVENT_MAX) return "UNKNOWN";
    return s_event_names[id];
}

bool event_bus_receive(event_t *evt, TickType_t timeout)
{
    if (!s_inited || !evt || !s_event_queue) return false;
    return xQueueReceive(s_event_queue, evt, timeout) == pdTRUE;
}
