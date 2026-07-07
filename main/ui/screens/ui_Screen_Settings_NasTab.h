#ifndef UI_SCREEN_SETTINGS_NASTAB_H
#define UI_SCREEN_SETTINGS_NASTAB_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_Screen_Settings_NasTab_init(lv_obj_t *parent);
void ui_Screen_Settings_NasTab_cleanup(void);
void nas_tab_save(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SETTINGS_NASTAB_H */
