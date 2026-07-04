#ifndef UI_QUOTES_H
#define UI_QUOTES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

void build_quotes_tile(lv_obj_t *parent);
void quotes_ensure(void);
void quotes_kick(void);
void quotes_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
