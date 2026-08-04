#ifndef UI_SCREEN_SYSTEMDETAIL_H
#define UI_SCREEN_SYSTEMDETAIL_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "../data/nas_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYS_DETAIL_CPU = 0,
    SYS_DETAIL_MEM = 1
} SystemDetailMode;

extern lv_obj_t *ui_Screen_SystemDetail;

void ui_Screen_SystemDetail_screen_init(SystemDetailMode mode);
void ui_Screen_SystemDetail_screen_destroy(void);
void ui_Screen_SystemDetail_update_data(const NasData *data);
void ui_Screen_SystemDetail_update_time(const char *time_str);
void ui_Screen_SystemDetail_update_network(int upload_kbps, int download_kbps);
void ui_Screen_SystemDetail_update_ip(const char *ip_str);
void ui_Screen_SystemDetail_update_wifi(bool connected);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SYSTEMDETAIL_H */
