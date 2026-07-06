#ifndef UI_SCREEN_SETTINGS_H
#define UI_SCREEN_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

extern void ui_Screen_Settings_screen_init(void);
extern void ui_Screen_Settings_screen_destroy(void);
extern lv_obj_t * ui_Screen_Settings;
extern void ui_event_Settings_Tabview_ConfigPanel(lv_event_t * e);
extern lv_obj_t * ui_Settings_Tabview_ConfigPanel;

#include "../ui_settings_wifi.h"
#include "../ui_settings_screen.h"
#include "../ui_settings_station.h"
#include "../ui_settings_music.h"
#include "../ui_settings_region.h"
#include "../ui_settings_guide.h"

extern void ui_event_Settings_Button_closeConfig(lv_event_t * e);
extern lv_obj_t * ui_Settings_Button_closeConfig;
extern lv_obj_t * ui_Settings_Label_closeBtnText;
extern void ui_event_Settings_Keyboard_Keyboard1(lv_event_t * e);
extern lv_obj_t * ui_Settings_Keyboard_Keyboard1;
extern void ui_event_Settings_Keyboard_Number(lv_event_t * e);
extern lv_obj_t * ui_Settings_Keyboard_Number;

#ifdef __cplusplus
}
#endif

#endif