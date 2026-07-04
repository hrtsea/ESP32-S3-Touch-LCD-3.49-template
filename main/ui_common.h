#ifndef UI_COMMON_H
#define UI_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "i18n.h"
#include "tz_cities.h"
#include "landmask.h"

#include "app_cfg.h"
#include "disp_driver.h"
#include "wifi_manager.h"
#include "backlight.h"
#include "theme.h"

#define WIFI_MAX_SCAN_AP 16
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t auth;
} wifi_scan_ap_t;

extern wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];

bool menu_input_blocked(void);
extern uint32_t g_last_scroll_ms;
extern uint32_t g_menu_input_block_until_ms;

extern lv_obj_t *g_tileview;

extern lv_timer_t *g_clock_ms_timer;

extern lv_timer_t *g_status_timer;

void build_clock_tile(lv_obj_t *parent);
void build_quotes_tile(lv_obj_t *parent);
void build_settings_tile(lv_obj_t *parent);
void build_radio_tile(lv_obj_t *parent);
void build_recorder_tile(lv_obj_t *parent);

void clock_bg_apply(void);
void clock_apply_layout(void);
void clock_update_cb(lv_timer_t *t);
void sunmap_redraw(void);

void bg_fetcher_ensure(void);

void quotes_ensure(void);
void quotes_kick(void);

void recorder_refresh_list(void);

void show_main_ui(const char *status_text);

extern char g_status_text[256];

#ifdef __cplusplus
}
#endif

#endif
