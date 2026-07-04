#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVENT_NONE = 0,

    /* --- Display / Rotation --- */
    EVENT_ROTATION_CHANGED,       /* data: int *new_rot_state */

    /* --- WiFi --- */
    EVENT_WIFI_CONNECTED,         /* data: char *ip_addr (may be NULL) */
    EVENT_WIFI_DISCONNECTED,      /* data: uint8_t *reason */
    EVENT_WIFI_SCAN_DONE,         /* data: uint16_t *ap_count */
    EVENT_WIFI_SCAN_STARTED,      /* data: NULL */

    /* --- Config --- */
    EVENT_CFG_CHANGED,            /* data: cfg_change_info_t * */
    EVENT_CLOCK_LAYOUT_CHANGED,   /* data: NULL */
    EVENT_CLOCK_BG_CHANGED,       /* data: NULL */

    /* --- Audio --- */
    EVENT_AUDIO_PLAY_START,       /* data: NULL */
    EVENT_AUDIO_PLAY_STOP,        /* data: NULL */
    EVENT_AUDIO_RECORD_START,     /* data: NULL */
    EVENT_AUDIO_RECORD_STOP,      /* data: NULL */
    EVENT_AUDIO_VOLUME_CHANGED,   /* data: uint8_t *volume */

    /* --- UI --- */
    EVENT_USER_ACTIVITY,          /* data: NULL */
    EVENT_BACKLIGHT_CHANGED,      /* data: uint8_t *brightness */
    EVENT_TILE_CHANGED,           /* data: int *tile_index */

    /* --- System --- */
    EVENT_TICK_1HZ,               /* data: NULL */
    EVENT_TICK_10HZ,              /* data: NULL */

    EVENT_MAX
} event_id_t;

typedef struct {
    event_id_t id;
    void      *data;
    size_t     data_len;
} event_t;

typedef void (*event_handler_t)(const event_t *evt, void *user_data);

void event_bus_init(void);
void event_bus_publish(event_id_t id, void *data, size_t len);
void event_bus_subscribe(event_id_t id, event_handler_t handler, void *user_data);
void event_bus_unsubscribe(event_id_t id, event_handler_t handler);
const char *event_bus_name(event_id_t id);

#ifdef __cplusplus
}
#endif

#endif
