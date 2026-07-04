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
void clock_cleanup(void);
void clock_ms_timer_pause(void);
void clock_ms_timer_resume(void);
void clock_update_tz_label(void);
void clock_set_wifi_icon_color(lv_color_t color);
void clock_set_bt_icon_color(lv_color_t color);

#ifdef __cplusplus
}
#endif

#endif
