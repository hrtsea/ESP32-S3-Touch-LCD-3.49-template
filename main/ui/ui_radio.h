#ifndef UI_RADIO_H
#define UI_RADIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

void build_radio_tile(lv_obj_t *parent);
void radio_engine_warm_at_boot(void);
void radio_ui_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
