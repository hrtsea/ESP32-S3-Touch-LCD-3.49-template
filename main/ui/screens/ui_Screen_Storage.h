#ifndef UI_SCREEN_STORAGE_H
#define UI_SCREEN_STORAGE_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void ui_Screen_Storage_screen_init(void);
extern void ui_Screen_Storage_screen_destroy(void);
extern lv_obj_t * ui_Screen_Storage;

#ifdef __cplusplus
}
#endif

#endif