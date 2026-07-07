#ifndef UI_SCREEN_WIFICONFIG_H
#define UI_SCREEN_WIFICONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

extern lv_obj_t* ui_Screen_WifiConfig;

void ui_Screen_WifiConfig_screen_init(void);
void ui_Screen_WifiConfig_screen_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
