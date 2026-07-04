#ifndef UI_MAIN_H
#define UI_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "i18n.h"
#include "tz_cities.h"
#include "landmask.h"

#include "app_cfg.h"
#include "disp_driver.h"
#include "wifi_manager.h"
#include "theme.h"
#include "ui_state.h"

#define N_TILES 7

extern lv_timer_t *g_status_timer;
extern uint32_t    g_last_activity_ms;
extern int         g_dim_state;

/* 短名宏:转发到 ui_state 模块 */
#define menu_input_blocked() ui_state_menu_input_blocked()

void show_main_ui(const char *status_text);
void fps_timer_cb(lv_timer_t *t);
void backlight_apply(uint8_t bri);
void rotate_btn_event_cb(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* UI_MAIN_H */
