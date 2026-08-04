#ifndef UI_SCREEN_SDCOPY_H
#define UI_SCREEN_SDCOPY_H

#include <lvgl.h>
#include "../config/app_info.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_Screen_SDCopy_screen_init(void);
extern void ui_Screen_SDCopy_screen_destroy(void);
extern lv_obj_t *ui_Screen_SDCopy;

void sdcopy_screen_update_time(const char *time_str);
void sdcopy_screen_update_network(int upload_kbps, int download_kbps);
void sdcopy_screen_update_ip(const char *ip_str);
void sdcopy_screen_update_wifi(bool connected);
void sdcopy_screen_update_hdd_led(int index, bool online, int health);
void sdcopy_screen_update_hdd_name(int index, const char *name);

void sdcopy_screen_update_progress(int pct, const char *speed_str);

#ifdef __cplusplus
}
#endif

#endif