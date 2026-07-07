#ifndef UI_EVENTS_H
#define UI_EVENTS_H

#include "lvgl.h"
#include "screens/ui_Screen_Boot.h"
#include "screens/ui_Screen_Overview.h"
#include "screens/ui_Screen_Settings.h"
#include "screens/ui_Screen_Storage.h"

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


void ui_Screen_Boot_event_handler(lv_event_t* e);
void ui_event_Screen_Overview_hdd_clicked(lv_event_t* e);
void ui_event_Screen_Overview_gesture(lv_event_t* e);
void ui_event_Screen_Settings_gesture(lv_event_t* e);
void ui_event_Screen_Storage_gesture(lv_event_t* e);

void resetScreenOffTimer(lv_event_t * e);
void saveWiFiCredential(lv_event_t * e);
void scanNetwork(lv_event_t * e);
void toggleWiFi(lv_event_t * e);
void setBrightness(lv_event_t * e);
void setTimer(lv_event_t * e);
void setWallpaper(lv_event_t * e);
void loadStationFromSDCARD(lv_event_t * e);
void loadMusicFromSDCARD(lv_event_t * e);
void set_query_para_autoip(lv_event_t * e);
void setOffsetHour(lv_event_t * e);
void setOffsetMinute(lv_event_t * e);
void setTempUnit(lv_event_t * e);
void saveConfig(lv_event_t * e);
void turnonScreen(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif
