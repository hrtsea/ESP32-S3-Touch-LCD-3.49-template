#ifndef UI_SETTINGS_GUIDE_H
#define UI_SETTINGS_GUIDE_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * ui_Settings_Tabpage_guide;
extern lv_obj_t * ui_Settings_Textarea_UserGuide;
extern lv_obj_t * ui_Settings_Image_buttons;

void ui_Screen_Settings_GuideTab_init(lv_obj_t *parent);
void ui_Screen_Settings_GuideTab_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETTINGS_GUIDE_H */