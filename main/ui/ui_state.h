#ifndef UI_STATE_H
#define UI_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t  *ui_state_get_tileview(void);
void       ui_state_set_tileview(lv_obj_t *tv);
void       ui_state_get_status_text(char *buf, size_t buf_len);
void       ui_state_set_status_text(const char *text);

uint32_t   ui_state_get_last_activity_ms(void);
void       ui_state_set_last_activity_ms(uint32_t t);
void       ui_state_notify_activity(void);

int        ui_state_get_dim_state(void);
void       ui_state_set_dim_state(int state);

uint32_t   ui_state_get_last_scroll_ms(void);
void       ui_state_set_last_scroll_ms(uint32_t t);

uint32_t   ui_state_get_menu_block_until_ms(void);
void       ui_state_set_menu_block_until_ms(uint32_t t);
bool       ui_state_menu_input_blocked(void);

#ifdef __cplusplus
}
#endif

#endif
