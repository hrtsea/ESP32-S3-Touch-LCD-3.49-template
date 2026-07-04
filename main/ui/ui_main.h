#ifndef UI_MAIN_H
#define UI_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

#define N_TILES 6

extern lv_timer_t *g_status_timer;
extern uint32_t    g_last_activity_ms;
extern int         g_dim_state;

void show_main_ui(const char *status_text);
void fps_timer_cb(lv_timer_t *t);
void backlight_apply(uint8_t bri);

#ifdef __cplusplus
}
#endif

#endif /* UI_MAIN_H */
