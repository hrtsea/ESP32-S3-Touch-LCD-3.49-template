#ifndef UI_SCREEN_BOOT_H
#define UI_SCREEN_BOOT_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t* ui_Screen_Boot;

void ui_Screen_Boot_screen_init(void);
void ui_Screen_Boot_start_progress(void);
void ui_Screen_Boot_screen_cleanup(void);
void ui_Screen_Boot_update_progress(uint8_t stage, const char* message);
void ui_Screen_Boot_stop_timeout(void);
bool ui_Screen_Boot_is_active(void);

#ifdef __cplusplus
}
#endif

#endif