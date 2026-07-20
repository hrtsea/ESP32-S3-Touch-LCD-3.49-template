#ifndef UI_SCREEN_SETTINGS_FANTAB_H
#define UI_SCREEN_SETTINGS_FANTAB_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_Screen_Settings_FanTab_init(lv_obj_t *parent);
void ui_Screen_Settings_FanTab_cleanup(void);
void fan_tab_save(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SETTINGS_FANTAB_H */
