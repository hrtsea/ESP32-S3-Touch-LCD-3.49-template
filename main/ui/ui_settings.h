#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

void build_settings_tile(lv_obj_t *parent);
void settings_cleanup(void);
void settings_set_wifi_status_text(const char *text);

#ifdef __cplusplus
}
#endif

#endif
