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

extern lv_obj_t *g_tileview;
extern char g_status_text[256];

bool menu_input_blocked(void);
extern uint32_t g_last_scroll_ms;
extern uint32_t g_menu_input_block_until_ms;

#ifdef __cplusplus
}
#endif

#endif