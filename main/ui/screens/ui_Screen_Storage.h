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

void storage_screen_update_time(const char *time_str);
void storage_screen_update_network(int upload_kbps, int download_kbps);
void storage_screen_update_ip(const char *ip_str);
void storage_screen_update_hdd_name(int index, const char *name);
void storage_screen_update_hdd_bar(int index, int used_pct, int health);
void storage_screen_update_hdd_temp(int index, int temp);
void storage_screen_update_hdd_online(int index, bool online);

#ifdef __cplusplus
}
#endif

#endif