#ifndef UI_HELLO_H
#define UI_HELLO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ===== 1. 外部声明：tile 对象 ===== */
extern lv_obj_t *ui_Hello;

/* ===== 2. 外部声明：所有命名控件 ===== */
extern lv_obj_t *ui_Hello_label_title;   /* 顶部循环滚动文字 */
extern lv_obj_t *ui_Hello_label_status;  /* 中央状态文本 */
extern lv_obj_t *ui_Hello_btn_play;     /* 左下播放/停止按钮 */
extern lv_obj_t *ui_Hello_btn_rotate;    /* 右下旋转按钮 */

/* ===== 3. 事件回调函数声明 ===== */
void ui_event_Hello_btn_play(lv_event_t *e);
void ui_event_Hello_btn_rotate(lv_event_t *e);

/* ===== 4. tile 创建/清理函数 ===== */
void ui_Hello_create(lv_obj_t *parent, const char *status_text);
void ui_Hello_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
