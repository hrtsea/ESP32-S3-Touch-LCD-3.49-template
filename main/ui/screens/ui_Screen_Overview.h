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

void overview_screen_update_time(const char *time_str);
void overview_screen_update_network(int upload_kbps, int download_kbps);
void overview_screen_update_ip(const char *ip_str);
void overview_screen_update_wifi(bool connected);
void overview_screen_update_cpu(int cpu_pct);
void overview_screen_update_temp(int temp);
void overview_screen_update_mem(int mem_pct);
void overview_screen_update_disk(int disk_pct);
void overview_screen_update_hdd_led(int index, bool online, int health);
void overview_screen_update_hdd_name(int index, const char *name);

#ifdef __cplusplus
}
#endif

#endif
