#ifndef UI_RECORDER_H
#define UI_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

void build_recorder_tile(lv_obj_t *parent);
void recorder_refresh_list(void);

/* Called by ui_main when the recorder tile becomes visible / hidden.
   Starts / stops the VU monitor so we don't waste CPU contending the
   I2S bus while other tiles (e.g. radio) are active. */
void recorder_tile_on_enter(void);
void recorder_tile_on_leave(void);

/* Recorder tile widget pointers (accessed by ui_main for rotate cleanup) */
extern lv_obj_t *g_rec_tile;
extern lv_obj_t *g_rec_status;
extern lv_obj_t *g_rec_btn_lbl;
extern lv_obj_t *g_rec_vu_l;
extern lv_obj_t *g_rec_vu_r;
extern lv_obj_t *g_rec_list_overlay;
extern lv_obj_t *g_rec_list;
extern lv_obj_t *g_rec_overlay_status;
extern lv_timer_t *g_rec_poll;

#ifdef __cplusplus
}
#endif

#endif
