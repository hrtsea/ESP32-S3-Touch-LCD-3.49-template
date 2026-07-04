#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_common.h"

/* ===== 1. 外部声明：tile 对象 ===== */
extern lv_obj_t *ui_Settings;

/* ===== 2. 外部声明：命名控件 ===== */
/* (ui_settings 内部 widget 数量多且仅在文件内访问，保留为 static，
   不暴露全局声明；如需跨模块访问，未来再提取。) */

/* ===== 3. 事件回调函数声明 ===== */
/* (subpage 内的事件回调均为 static，不对外暴露) */

/* ===== 4. tile 创建/清理函数 ===== */
void ui_Settings_create(lv_obj_t *parent);
void ui_Settings_cleanup(void);

/* ===== 业务 API ===== */
void settings_set_wifi_status_text(const char *text);
void storage_info_refresh(void);
void sd_format_worker(void *arg);

#ifdef __cplusplus
}
#endif

#endif
