#ifndef UI_SCREEN_STORAGE_H
#define UI_SCREEN_STORAGE_H

#include <lvgl.h>
#include "../config/app_info.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_Screen_Storage_screen_init(void);
extern void ui_Screen_Storage_screen_destroy(void);
extern lv_obj_t * ui_Screen_Storage;

extern lv_obj_t *s_hdd_names[MAX_DISKS];
extern lv_obj_t *s_hdd_bars[MAX_DISKS];
extern lv_obj_t *s_hdd_percents[MAX_DISKS];
extern lv_obj_t *s_hdd_temps[MAX_DISKS];
extern lv_obj_t *s_storage_label_time;
extern lv_obj_t *s_storage_label_up;
extern lv_obj_t *s_storage_label_down;
extern lv_obj_t *s_storage_label_ip;

#ifdef __cplusplus
}
#endif

#endif