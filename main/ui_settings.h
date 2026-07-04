#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

void build_settings_tile(lv_obj_t *parent);

/* Settings tile widget pointers (accessed by ui_main for rotate cleanup) */
extern lv_obj_t *g_set_wifi_status;
extern lv_obj_t *g_set_wifi_list;
extern lv_obj_t *g_set_kb_overlay;
extern lv_obj_t *g_set_kb_ta;

/* Menu shield (accessed by ui_main for rotate cleanup) */
extern lv_obj_t *g_menu_shield;

#ifdef __cplusplus
}
#endif

#endif
