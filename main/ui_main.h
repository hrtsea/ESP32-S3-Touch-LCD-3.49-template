#ifndef UI_MAIN_H
#define UI_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

#define N_TILES 6

extern lv_timer_t *g_status_timer;

/* Top-level UI builder: creates the tileview, all tiles, FPS overlay,
 * gesture handlers, and dim/status timers.  Called from app_main and
 * on rotation rebuild. */
void show_main_ui(const char *status_text);

/* FPS timer callback (defined here, registered in build_main_ui). */
void fps_timer_cb(lv_timer_t *t);

#ifdef __cplusplus
}
#endif

#endif /* UI_MAIN_H */
