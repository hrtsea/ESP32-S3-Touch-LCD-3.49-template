#ifndef UI_CLOCK_H
#define UI_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui.h"

/* ===== 1. 外部声明：tile 对象 ===== */
extern lv_obj_t *ui_Clock;

/* ===== 2. 外部声明：所有命名控件 ===== */
extern lv_obj_t *ui_Clock_label_time;
extern lv_obj_t *ui_Clock_label_ms;
extern lv_obj_t *ui_Clock_label_date;
extern lv_obj_t *ui_Clock_label_tz;
extern lv_obj_t *ui_Clock_canvas_sunmap;
extern lv_obj_t *ui_Clock_icon_wifi;
extern lv_obj_t *ui_Clock_icon_bt;

/* ===== 3. 事件回调函数声明 ===== */
/* (clock tile 当前无 LVGL 事件回调，状态更新通过定时器和外部 API 完成) */

/* ===== 4. tile 创建/清理函数 ===== */
void ui_Clock_create(lv_obj_t *parent);
void ui_Clock_cleanup(void);

/* ===== 业务 API ===== */
void clock_bg_apply(void);
void clock_apply_layout(void);
void clock_update_cb(lv_timer_t *t);
void sunmap_redraw(void);
void tz_apply_current(void);
const char *tz_current_city_name(void);
void clock_ms_timer_pause(void);
void clock_ms_timer_resume(void);
void clock_update_tz_label(void);
void clock_set_wifi_icon_color(lv_color_t color);
void clock_set_bt_icon_color(lv_color_t color);

#ifdef __cplusplus
}
#endif

#endif
