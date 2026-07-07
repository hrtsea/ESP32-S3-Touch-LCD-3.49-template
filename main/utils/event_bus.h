#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "nas_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVENT_NONE = 0,

    EVENT_ROTATION_CHANGED,
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_WIFI_SCAN_DONE,
    EVENT_WIFI_SCAN_STARTED,
    EVENT_CFG_CHANGED,
    EVENT_CLOCK_LAYOUT_CHANGED,
    EVENT_CLOCK_BG_CHANGED,
    EVENT_CLOCK_TIME_FORMAT_CHANGED,
    EVENT_QUOTES_CHANGED,
    EVENT_SHOW_FPS_CHANGED,
    EVENT_STORAGE_CHANGED,
    EVENT_AUDIO_PLAY_START,
    EVENT_AUDIO_PLAY_STOP,
    EVENT_AUDIO_RECORD_START,
    EVENT_AUDIO_RECORD_STOP,
    EVENT_AUDIO_VOLUME_CHANGED,
    EVENT_USER_ACTIVITY,
    EVENT_BACKLIGHT_CHANGED,
    EVENT_TILE_CHANGED,
    EVENT_TICK_1HZ,
    EVENT_TICK_10HZ,

    EVENT_NAS_DATA_UPDATE,
    EVENT_TRIGGER_HTTP_FETCH,
    EVENT_HTTP_STOP,

    EVENT_WIFI_PROVISION_START,
    EVENT_WIFI_PROVISION_STOP,
    EVENT_WIFI_PROVISION_CONFIG_RECEIVED,

    EVENT_MAX
} event_id_t;

typedef struct {
    event_id_t id;
    void *data;
    size_t data_len;
} event_t;

typedef void (*event_handler_t)(const event_t *evt, void *user_data);

#define EVENT_QUEUE_LEN 16

void event_bus_init(void);
void event_bus_publish(event_id_t id, void *data, size_t len);
void event_bus_publish_nas_data(const NasData *data);
void event_bus_subscribe(event_id_t id, event_handler_t handler, void *user_data);
void event_bus_unsubscribe(event_id_t id, event_handler_t handler);
const char *event_bus_name(event_id_t id);
bool event_bus_receive(event_t *evt, TickType_t timeout);

#ifdef __cplusplus
}
#endif

#endif
