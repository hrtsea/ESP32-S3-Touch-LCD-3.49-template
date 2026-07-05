#ifndef UI_SCREEN_OVERVIEW_H
#define UI_SCREEN_OVERVIEW_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_Screen_Overview_screen_init(void);
extern void ui_Screen_Overview_screen_destroy(void);
extern lv_obj_t * ui_Screen_Overview;

#ifdef __cplusplus
}
#endif

#endif