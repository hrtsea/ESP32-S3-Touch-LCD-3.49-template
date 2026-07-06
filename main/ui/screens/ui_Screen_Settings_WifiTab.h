#ifndef UI_SCREEN_SETTINGS_WIFITAB_H
#define UI_SCREEN_SETTINGS_WIFITAB_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * ui_Settings_Tabpage_network;
extern lv_obj_t * ui_Settings_Label_connectStatus;
extern lv_obj_t * ui_Settings_Label_wifi_hints;
extern lv_obj_t * ui_Settings_Dropdown_NetworkList;
extern lv_obj_t * ui_Settings_Textarea_Password;
extern lv_obj_t * ui_Settings_Button_NetworkSave;
extern lv_obj_t * ui_Settings_Label_saveBtnText;
extern lv_obj_t * ui_Settings_Button_NetworkScan;
extern lv_obj_t * ui_Settings_Label_scanBtnText;
extern lv_obj_t * ui_Settings_Switch_Wifi;

void ui_Screen_Settings_WifiTab_init(lv_obj_t *parent);
void ui_Screen_Settings_WifiTab_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SETTINGS_WIFITAB_H */