#ifndef UI_AUDIO_TEST_H
#define UI_AUDIO_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ===== 1. 外部声明：tile 对象 ===== */
extern lv_obj_t *ui_AudioTest;

/* ===== 2. 外部声明：所有命名控件 ===== */
extern lv_obj_t *ui_AudioTest_label_title;
extern lv_obj_t *ui_AudioTest_label_status;
extern lv_obj_t *ui_AudioTest_label_status_en;
extern lv_obj_t *ui_AudioTest_btn_record;
extern lv_obj_t *ui_AudioTest_btn_play;
extern lv_obj_t *ui_AudioTest_btn_music;
extern lv_obj_t *ui_AudioTest_btn_stop;

/* ===== 3. 事件回调函数声明 ===== */
void ui_event_AudioTest_btn_record(lv_event_t *e);
void ui_event_AudioTest_btn_play(lv_event_t *e);
void ui_event_AudioTest_btn_music(lv_event_t *e);
void ui_event_AudioTest_btn_stop(lv_event_t *e);

/* ===== 4. tile 创建/清理函数 ===== */
void ui_AudioTest_create(lv_obj_t *parent);
void ui_AudioTest_cleanup(void);

/* 业务初始化（worker 任务） */
void audio_test_ui_init(void);

#ifdef __cplusplus
}
#endif

#endif
