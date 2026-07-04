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
void recorder_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
