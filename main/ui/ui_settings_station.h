#ifndef UI_SETTINGS_STATION_H
#define UI_SETTINGS_STATION_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * ui_Settings_Tabpage_station;
extern lv_obj_t * ui_Settings_Textarea_stationList;
extern lv_obj_t * ui_Settings_Label_stationHint;
extern lv_obj_t * ui_Settings_Button_LoadStation;
extern lv_obj_t * ui_Settings_Label_loadStationBtnText;

void ui_Screen_Settings_StationTab_init(lv_obj_t *parent);
void ui_Screen_Settings_StationTab_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETTINGS_STATION_H */