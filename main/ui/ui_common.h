#ifndef UI_COMMON_H
#define UI_COMMON_H

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

extern lv_obj_t *g_tileview;
extern char g_status_text[256];
extern uint32_t g_last_scroll_ms;
extern uint32_t g_menu_input_block_until_ms;

#define canvas_w         g_canvas_w
#define canvas_h         g_canvas_h
#define rot_state        g_rot_state
#define fps_label        g_fps_label
#define fps_frame_count  g_fps_frame_count

#define menu_input_blocked() ui_state_menu_input_blocked()

#ifdef __cplusplus
}
#endif

#endif
