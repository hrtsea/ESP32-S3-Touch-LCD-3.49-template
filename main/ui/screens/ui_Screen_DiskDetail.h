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

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_DISKDETAIL_H */
