#ifndef UI_SCREEN_SETTINGS_H
#define UI_SCREEN_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

extern void ui_Screen_Settings_screen_init(void);
extern void ui_Screen_Settings_screen_destroy(void);
extern lv_obj_t * ui_Screen_Settings;
extern void ui_event_MainMenu_Tabview_ConfigPanel(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Tabview_ConfigPanel;
extern void ui_event_MainMenu_Tabpage_network(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Tabpage_network;
extern lv_obj_t * ui_MainMenu_Label_connectStatus;
extern lv_obj_t * ui_MainMenu_Label_Label3;
extern lv_obj_t * ui_MainMenu_Dropdown_NetworkList;
extern void ui_event_MainMenu_Textarea_Password(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Textarea_Password;
extern void ui_event_MainMenu_Button_NetworkSave(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Button_NetworkSave;
extern lv_obj_t * ui_MainMenu_Label_Label12;
extern void ui_event_MainMenu_Button_NetworkScan(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Button_NetworkScan;
extern lv_obj_t * ui_MainMenu_Label_Label13;
extern void ui_event_MainMenu_Switch_Wifi(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Switch_Wifi;
extern void ui_event_MainMenu_Tabpage_screen(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Tabpage_screen;
extern void ui_event_MainMenu_Dropdown_Brightness(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Dropdown_Brightness;
extern void ui_event_MainMenu_Dropdown_SleepTimer(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Dropdown_SleepTimer;
extern void ui_event_MainMenu_Dropdown_Wallpaper(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Dropdown_Wallpaper;
extern lv_obj_t * ui_MainMenu_Label_Label8;
extern void ui_event_MainMenu_Tabpage_station(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Tabpage_station;
extern lv_obj_t * ui_MainMenu_Textarea_stationList;
extern lv_obj_t * ui_MainMenu_Label_Label10;
extern void ui_event_MainMenu_Button_LoadStation(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Button_LoadStation;
extern lv_obj_t * ui_MainMenu_Label_Label20;
extern void ui_event_MainMenu_Tabpage_music(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Tabpage_music;
extern void ui_event_MainMenu_Button_scanMusic(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Button_scanMusic;
extern lv_obj_t * ui_MainMenu_Label_Label14;
extern lv_obj_t * ui_MainMenu_Label_Label15;
extern lv_obj_t * ui_MainMenu_Label_trackCount;
extern void ui_event_MainMenu_Tabpage_region(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Tabpage_region;
extern lv_obj_t * ui_MainMenu_Image_Globe;
extern void ui_event_MainMenu_Checkbox_AutoIP(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Checkbox_AutoIP;
extern void ui_event_MainMenu_Textarea_Latitude(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Textarea_Latitude;
extern void ui_event_MainMenu_Textarea_Longitude(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Textarea_Longitude;
extern lv_obj_t * ui_MainMenu_Image_timezone;
extern void ui_event_MainMenu_Roller_Hour(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Roller_Hour;
extern void ui_event_MainMenu_Roller_Minute(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Roller_Minute;
extern void ui_event_MainMenu_Roller_Unit(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Roller_Unit;
extern lv_obj_t * ui_MainMenu_Label_Label25;
extern void ui_event_MainMenu_Tabpage_guide(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Tabpage_guide;
extern void ui_event_MainMenu_Textarea_UserGuide(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Textarea_UserGuide;
extern lv_obj_t * ui_MainMenu_Image_buttons;
extern void ui_event_MainMenu_Button_closeConfig(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Button_closeConfig;
extern lv_obj_t * ui_MainMenu_Label_Label9;
extern void ui_event_MainMenu_Keyboard_Keyboard1(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Keyboard_Keyboard1;
extern void ui_event_MainMenu_Keyboard_Number(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Keyboard_Number;
extern void ui_event_MainMenu_Panel_blindPanel(lv_event_t * e);
extern lv_obj_t * ui_MainMenu_Panel_blindPanel;

#ifdef __cplusplus
}
#endif

#endif