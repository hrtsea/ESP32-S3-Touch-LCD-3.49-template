#ifndef UI_EVENTS_H
#define UI_EVENTS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void       ui_events_wifi_status_cb(bool connected, const char *ip_addr);
void       ui_events_start_dim_timer(void);
void       ui_events_stop_dim_timer(void);
void       ui_events_start_status_timer(void);
void       ui_events_stop_status_timer(void);
void       ui_events_subscribe_events(void);
void       ui_events_unsubscribe_events(void);

void       ui_events_activity_kick(lv_event_t *e);
void       ui_events_register_tileview_events(lv_obj_t *tv);
void       ui_events_register_screen_events(lv_obj_t *scr);
void       ui_events_start_tile_monitor(void);
void       ui_events_rotate_screen(void);

#ifdef __cplusplus
}
#endif

#endif
