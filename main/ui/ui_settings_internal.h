#ifndef UI_SETTINGS_INTERNAL_H
#define UI_SETTINGS_INTERNAL_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * ui_Screen_Settings;
extern lv_obj_t * ui_Screen_Overview;

extern lv_obj_t * ui_Settings_Keyboard_Keyboard1;
extern lv_obj_t * ui_Settings_Keyboard_Number;

extern lv_style_t style_btn_border;
extern lv_style_t style_tab_bg;

extern void settings_activity_kick(lv_event_t *e);
extern void init_styles(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETTINGS_INTERNAL_H */