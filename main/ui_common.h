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

#define WIFI_MAX_SCAN_AP 16
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t auth;
} wifi_scan_ap_t;

extern bool     g_wifi_connected;
extern char     g_wifi_curr_ssid[33];
extern uint8_t  g_wifi_last_reason;
extern int8_t   g_wifi_last_rssi;
extern uint32_t g_wifi_connect_started_ms;
extern bool     g_wifi_scanning;
extern uint16_t g_wifi_scan_n;
extern wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];

void wifi_start_scan(void);
void wifi_connect(const char *ssid, const char *pass);

extern int      g_dim_state;
extern uint32_t g_last_activity_ms;
void backlight_apply(uint8_t bri);

typedef struct {
    lv_color_t bg;
    lv_color_t text;
    lv_color_t menu_surf;
    lv_color_t menu_hdr;
    lv_color_t menu_btn;
    lv_color_t sunmap_water_n;
    lv_color_t sunmap_water_d;
    lv_color_t sunmap_land_n;
    lv_color_t sunmap_land_d;
} theme_palette_t;
theme_palette_t theme_get(void);

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
