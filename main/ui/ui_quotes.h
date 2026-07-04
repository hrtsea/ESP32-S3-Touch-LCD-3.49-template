#ifndef UI_QUOTES_H
#define UI_QUOTES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_main.h"

/* ===== 1. 外部声明：tile 对象 ===== */
extern lv_obj_t *ui_Quotes;

/* ===== 2. 外部声明：所有命名控件 ===== */
extern lv_obj_t *ui_Quotes_label_name_l;
extern lv_obj_t *ui_Quotes_label_name_r;
extern lv_obj_t *ui_Quotes_label_sym_l;
extern lv_obj_t *ui_Quotes_label_sym_r;
extern lv_obj_t *ui_Quotes_label_price_l;
extern lv_obj_t *ui_Quotes_label_price_r;
extern lv_obj_t *ui_Quotes_label_chg_l;
extern lv_obj_t *ui_Quotes_label_chg_r;
extern lv_obj_t *ui_Quotes_label_status;
extern lv_obj_t *ui_Quotes_icon_wifi;
extern lv_obj_t *ui_Quotes_icon_bt;
extern lv_obj_t *ui_Quotes_label_clock;

/* ===== 3. 事件回调函数声明 ===== */
/* (quotes tile 当前无 LVGL 事件回调，状态更新通过定时器和外部 API 完成) */

/* ===== 4. tile 创建/清理函数 ===== */
void ui_Quotes_create(lv_obj_t *parent);
void ui_Quotes_cleanup(void);

/* ===== 业务 API ===== */
void quotes_ensure(void);
void quotes_kick(void);

#ifdef __cplusplus
}
#endif

#endif
