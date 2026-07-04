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

/* Quotes tile widget pointers (accessed by ui_main for rotate cleanup) */
extern lv_obj_t *g_quotes_tile;
extern lv_obj_t *g_quotes_name_l;
extern lv_obj_t *g_quotes_name_r;
extern lv_obj_t *g_quotes_sym_l_lbl;
extern lv_obj_t *g_quotes_sym_r_lbl;
extern lv_obj_t *g_quotes_price_l;
extern lv_obj_t *g_quotes_price_r;
extern lv_obj_t *g_quotes_chg_l;
extern lv_obj_t *g_quotes_chg_r;
extern lv_obj_t *g_quotes_status;
extern lv_obj_t *g_quotes_wifi_icon;
extern lv_obj_t *g_quotes_bt_icon;
extern lv_obj_t *g_quotes_clock_lbl;

#ifdef __cplusplus
}
#endif

#endif
