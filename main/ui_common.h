#ifndef UI_COMMON_H
#define UI_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* =====================================================================
 * ui_common.h — Shared declarations across all UI modules.
 *
 * During the UI extraction from main.cpp, shared globals and helpers
 * are declared here so each ui_*.c file can compile independently.
 * Variables are still *defined* in main.cpp (or disp_driver/app_cfg
 * once those are extracted).  This header is the glue layer.
 * ===================================================================== */

/* ---- Forward declarations of external modules ---- */
#include "i18n.h"
#include "tz_cities.h"
#include "landmask.h"

/* ---- App configuration struct (defined in main.cpp for now) ---- */
typedef struct {
    uint16_t tz_idx;
    uint8_t  brightness;
    uint16_t dim_s;
    uint16_t off_s;
    char     last_ssid[33];
    uint8_t  hour24;
    uint8_t  date_fmt;
    uint8_t  show_seconds;
    uint8_t  show_ms;
    uint8_t  audio_enable;
    uint8_t  audio_volume;
    uint8_t  theme;
    uint8_t  show_fps;
    uint8_t  wifi_autoconnect;
    uint8_t  lang;
    int16_t  clock_x;
    int16_t  clock_y;
    uint8_t  clock_size;
    uint32_t clock_rgba;
    uint8_t  show_clock;
    char     clock_text[40];
    uint8_t  bg_mode;
    uint16_t bg_refresh_s;
    char     bg_url[160];
    uint32_t bg_color;
    char     quotes_sym_l[16];
    char     quotes_sym_r[16];
    uint16_t quotes_refresh_s;
    uint32_t quotes_up_rgba;
    uint32_t quotes_down_rgba;
} app_cfg_t;

extern app_cfg_t g_cfg;

/* ---- NVS config helpers (defined in main.cpp) ---- */
void cfg_save(void);
void cfg_save_ssid_pass(const char *ssid, const char *pass);
bool cfg_get_ssid_pass(const char *ssid, char *pass, size_t pass_len);

/* ---- Display geometry (defined in main.cpp / future disp_driver) ---- */
extern int canvas_w;
extern int canvas_h;
extern int rot_state;

/* ---- LVGL mutex (defined in main.cpp / future disp_driver) ---- */
bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);

/* ---- FPS (defined in main.cpp / future disp_driver) ---- */
extern volatile uint32_t fps_frame_count;
extern lv_obj_t *fps_label;

/* ---- Wi-Fi state (defined in main.cpp / future wifi_manager) ---- */
extern bool     g_wifi_connected;
extern char     g_wifi_curr_ssid[33];
extern uint8_t  g_wifi_last_reason;
extern int8_t   g_wifi_last_rssi;
extern uint32_t g_wifi_connect_started_ms;
extern bool     g_wifi_scanning;
extern uint16_t g_wifi_scan_n;

/* Wi-Fi scan AP record (defined in main.cpp) */
#define WIFI_MAX_SCAN_AP 16
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t auth;
} wifi_scan_ap_t;
extern wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];

void wifi_start_scan(void);
void wifi_connect(const char *ssid, const char *pass);

/* ---- Backlight + auto-dim (defined in main.cpp / future backlight) ---- */
extern int      g_dim_state;
extern uint32_t g_last_activity_ms;
void backlight_apply(uint8_t bri);

/* ---- Theme palette (defined in main.cpp / future theme) ---- */
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

/* ---- Input debounce (defined in ui_main.c) ---- */
bool menu_input_blocked(void);
extern uint32_t g_last_scroll_ms;
extern uint32_t g_menu_input_block_until_ms;

/* ---- Tileview (defined in ui_main.c) ---- */
extern lv_obj_t *g_tileview;

/* ---- Clock timers (defined in ui_clock.c, accessed by settings) ---- */
extern lv_timer_t *g_clock_ms_timer;

/* ---- Status timer (defined in ui_main.c) ---- */
extern lv_timer_t *g_status_timer;

/* ---- Tile builder prototypes ---- */
void build_clock_tile(lv_obj_t *parent);
void build_quotes_tile(lv_obj_t *parent);
void build_settings_tile(lv_obj_t *parent);
void build_radio_tile(lv_obj_t *parent);
void build_recorder_tile(lv_obj_t *parent);

/* ---- Cross-module function prototypes ---- */

/* ui_clock.c */
void clock_bg_apply(void);
void clock_apply_layout(void);
void clock_update_cb(lv_timer_t *t);
void sunmap_redraw(void);

/* ui_bg_fetcher.c */
void bg_fetcher_ensure(void);

/* ui_quotes.c */
void quotes_ensure(void);
void quotes_kick(void);

/* ui_recorder.c */
void recorder_refresh_list(void);

/* ui_main.c */
void show_main_ui(const char *status_text);

/* status text buffer (defined in main.cpp, read by ui_main) */
extern char g_status_text[256];

/* ---- NVS namespaces (defined in main.cpp) ---- */
#define NVS_NS_CFG  "cfg"
#define NVS_NS_WIFI "wifi"

#ifdef __cplusplus
}
#endif

#endif /* UI_COMMON_H */
