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

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SYSTEMDETAIL_H */
