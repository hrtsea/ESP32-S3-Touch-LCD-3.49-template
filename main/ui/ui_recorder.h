#ifndef UI_RECORDER_H
#define UI_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

/* ===== 1. 外部声明：tile 对象 ===== */
extern lv_obj_t *ui_Recorder;

/* ===== 2. 外部声明：所有命名控件 ===== */
extern lv_obj_t *ui_Recorder_label_status;
extern lv_obj_t *ui_Recorder_label_rec_btn;     /* "REC" / STOP glyph */
extern lv_obj_t *ui_Recorder_bar_vu_l;
extern lv_obj_t *ui_Recorder_bar_vu_r;

/* ===== 3. 事件回调函数声明 ===== */
void ui_event_Recorder_btn_rec(lv_event_t *e);
void ui_event_Recorder_btn_list_open(lv_event_t *e);
void ui_event_Recorder_btn_list_close(lv_event_t *e);
void ui_event_Recorder_item_play(lv_event_t *e);
void ui_event_Recorder_item_delete(lv_event_t *e);

/* ===== 4. tile 创建/清理函数 ===== */
void ui_Recorder_create(lv_obj_t *parent);
void ui_Recorder_cleanup(void);

/* ===== 业务 API ===== */
void recorder_refresh_list(void);
void recorder_tile_on_enter(void);
void recorder_tile_on_leave(void);

#ifdef __cplusplus
}
#endif

#endif
