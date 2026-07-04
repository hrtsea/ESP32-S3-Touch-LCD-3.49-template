#ifndef UI_RADIO_H
#define UI_RADIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

void build_radio_tile(lv_obj_t *parent);
void radio_engine_warm_at_boot(void);

/* Radio tile widget pointers (accessed by ui_main for rotate cleanup) */
extern lv_obj_t *g_radio_status_lbl;
extern lv_obj_t *g_radio_now_lbl;
extern lv_obj_t *g_radio_btn_lbl;
extern lv_obj_t *g_radio_list;
extern lv_obj_t *g_radio_vol_lbl;
extern lv_timer_t *g_radio_poll_timer;

#ifdef __cplusplus
}
#endif

#endif
