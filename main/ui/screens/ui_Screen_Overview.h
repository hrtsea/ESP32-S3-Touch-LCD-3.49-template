#ifndef UI_SCREEN_OVERVIEW_H
#define UI_SCREEN_OVERVIEW_H

#include <lvgl.h>
#include "../config/app_info.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_Screen_Overview_screen_init(void);
extern void ui_Screen_Overview_screen_destroy(void);
extern lv_obj_t * ui_Screen_Overview;

extern lv_obj_t *s_meter_cpu;
extern lv_obj_t *s_meter_temp;
extern lv_obj_t *s_label_cpu_percent;
extern lv_obj_t *s_label_temp_val;
extern lv_obj_t *s_bar_mem;
extern lv_obj_t *s_label_mem_percent;
extern lv_obj_t *s_bar_disk;
extern lv_obj_t *s_label_disk_percent;

extern lv_meter_indicator_t *s_cpu_arc_val;
extern lv_meter_indicator_t *s_cpu_needle;
extern lv_meter_indicator_t *s_temp_arc_val;
extern lv_meter_indicator_t *s_temp_needle;

extern lv_obj_t *s_hdd_leds[MAX_DISKS];
extern lv_obj_t *s_hdd_labels[MAX_DISKS];

extern lv_obj_t *s_label_time;
extern lv_obj_t *s_label_up;
extern lv_obj_t *s_label_down;
extern lv_obj_t *s_label_ip;
extern lv_obj_t *s_icon_wifi;
extern lv_obj_t *s_icon_bt;

#ifdef __cplusplus
}
#endif

#endif