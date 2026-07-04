#ifndef UI_RADIO_H
#define UI_RADIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

/* ===== 1. 外部声明：tile 对象 ===== */
extern lv_obj_t *ui_Radio;

/* ===== 2. 外部声明：所有命名控件 ===== */
extern lv_obj_t *ui_Radio_label_status;
extern lv_obj_t *ui_Radio_label_now;
extern lv_obj_t *ui_Radio_label_play_btn;   /* play/stop glyph */
extern lv_obj_t *ui_Radio_list;              /* scrollable station list */
extern lv_obj_t *ui_Radio_label_vol;         /* "Vol N" indicator */

/* ===== 3. 事件回调函数声明 ===== */
void ui_event_Radio_btn_vol_dn(lv_event_t *e);
void ui_event_Radio_btn_vol_up(lv_event_t *e);
void ui_event_Radio_btn_play(lv_event_t *e);
void ui_event_Radio_station_pick(lv_event_t *e);

/* ===== 4. tile 创建/清理函数 ===== */
void ui_Radio_create(lv_obj_t *parent);
void ui_Radio_cleanup(void);

/* ===== 业务 API ===== */
void radio_engine_warm_at_boot(void);

#ifdef __cplusplus
}
#endif

#endif
