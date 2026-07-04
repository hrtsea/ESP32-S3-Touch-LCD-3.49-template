#ifndef UI_CLOCK_H
#define UI_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

void build_clock_tile(lv_obj_t *parent);
void clock_bg_apply(void);
void clock_apply_layout(void);
void clock_update_cb(lv_timer_t *t);
void sunmap_redraw(void);
void tz_apply_current(void);
const char *tz_current_city_name(void);

/* Clock tile widget pointers (accessed by ui_main for rotate cleanup) */
extern lv_obj_t *g_clock_time_label;
extern lv_obj_t *g_clock_ms_label;
extern lv_obj_t *g_clock_date_label;
extern lv_obj_t *g_clock_tz_label;
extern lv_obj_t *g_clock_wifi_icon;
extern lv_obj_t *g_clock_bt_icon;
extern lv_obj_t *g_sunmap_canvas;
extern lv_color_t *g_sunmap_buf;
extern int g_sunmap_w;
extern int g_sunmap_h;
extern lv_timer_t *g_clock_timer;
extern lv_timer_t *g_clock_ms_timer;
extern lv_timer_t *g_sunmap_timer;

#ifdef __cplusplus
}
#endif

#endif
