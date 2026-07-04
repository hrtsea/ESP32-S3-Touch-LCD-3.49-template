#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void build_hello_tile(lv_obj_t *parent, const char *status_text);
extern lv_obj_t *g_hello_play_btn_label;

#ifdef __cplusplus
}
#endif
