#ifndef UI_SCREEN_DISKDETAIL_H
#define UI_SCREEN_DISKDETAIL_H

#include <lvgl.h>
#include <stdint.h>
#include "../data/nas_data.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *ui_Screen_DiskDetail;

void ui_Screen_DiskDetail_screen_init(uint8_t disk_index);
void ui_Screen_DiskDetail_screen_destroy(void);
void ui_Screen_DiskDetail_update_data(const NasData *data);
void ui_Screen_DiskDetail_update_time(const char *time_str);
void ui_Screen_DiskDetail_update_network(int upload_kbps, int download_kbps);
void ui_Screen_DiskDetail_update_ip(const char *ip_str);
void ui_Screen_DiskDetail_update_wifi(bool connected);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_DISKDETAIL_H */
