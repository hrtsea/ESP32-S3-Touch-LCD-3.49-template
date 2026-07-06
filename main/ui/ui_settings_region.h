#ifndef UI_SETTINGS_REGION_H
#define UI_SETTINGS_REGION_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * ui_Settings_Tabpage_region;
extern lv_obj_t * ui_Settings_Image_Globe;
extern lv_obj_t * ui_Settings_Checkbox_AutoIP;
extern lv_obj_t * ui_Settings_Textarea_Latitude;
extern lv_obj_t * ui_Settings_Textarea_Longitude;
extern lv_obj_t * ui_Settings_Image_timezone;
extern lv_obj_t * ui_Settings_Roller_Hour;
extern lv_obj_t * ui_Settings_Roller_Minute;
extern lv_obj_t * ui_Settings_Roller_Unit;
extern lv_obj_t * ui_Settings_Label_timezoneHint;

void ui_Screen_Settings_RegionTab_init(lv_obj_t *parent);
void ui_Screen_Settings_RegionTab_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETTINGS_REGION_H */