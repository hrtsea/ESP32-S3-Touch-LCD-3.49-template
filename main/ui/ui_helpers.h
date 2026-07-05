#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t  *ui_helpers_get_tileview(void);
void       ui_helpers_set_tileview(lv_obj_t *tv);
void       ui_helpers_get_status_text(char *buf, size_t buf_len);
void       ui_helpers_set_status_text(const char *text);

uint32_t   ui_helpers_get_last_activity_ms(void);
void       ui_helpers_set_last_activity_ms(uint32_t t);

int        ui_helpers_get_dim_state(void);
void       ui_helpers_set_dim_state(int state);

uint32_t   ui_helpers_get_last_scroll_ms(void);
void       ui_helpers_set_last_scroll_ms(uint32_t t);

uint32_t   ui_helpers_get_menu_block_until_ms(void);
void       ui_helpers_set_menu_block_until_ms(uint32_t t);
bool       ui_helpers_menu_input_blocked(void);

void       ui_helpers_backlight_apply(uint8_t bri);
void       ui_helpers_notify_activity(void);

#ifdef __cplusplus
}
#endif

#endif
