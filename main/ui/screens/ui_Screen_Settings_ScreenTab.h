#ifndef UI_SCREEN_SETTINGS_SCREENTAB_H
#define UI_SCREEN_SETTINGS_SCREENTAB_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * ui_Settings_Tabpage_screen;
extern lv_obj_t * ui_Settings_Dropdown_Brightness;
extern lv_obj_t * ui_Settings_Dropdown_SleepTimer;
extern lv_obj_t * ui_Settings_Dropdown_Wallpaper;
extern lv_obj_t * ui_Settings_Label_screenHints;

void ui_Screen_Settings_ScreenTab_init(lv_obj_t *parent);
void ui_Screen_Settings_ScreenTab_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SETTINGS_SCREENTAB_H */