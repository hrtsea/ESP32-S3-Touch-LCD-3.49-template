#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <lvgl.h>
#include "../data/nas_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 页面主题色系统 ========== */

typedef enum {
    PAGE_IDX_SETTINGS = 0,
    PAGE_IDX_OVERVIEW = 1,
    PAGE_IDX_STORAGE  = 2,
    PAGE_IDX_SDCOPY   = 3,
    PAGE_IDX_COUNT
} page_idx_t;

/* 获取页面强调色 */
lv_color_t page_accent_color(page_idx_t idx);

/* 获取页面名称 */
const char *page_accent_name(page_idx_t idx);

/* ========== 健康状态颜色 ========== */

lv_color_t health_color(HealthStatus health);

/* ========== UI 组件原语 ========== */

/* 创建带边框的圆角卡片 */
lv_obj_t *ui_widget_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h,
                          lv_color_t border_color);

/* 创建标签 */
lv_obj_t *ui_widget_label(lv_obj_t *parent, const char *text,
                           lv_color_t color, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w);

/* 创建带字体的标签 */
lv_obj_t *ui_widget_label_font(lv_obj_t *parent, const char *text,
                                lv_color_t color, const lv_font_t *font,
                                lv_coord_t x, lv_coord_t y, lv_coord_t w);

/* 创建键值对（标签 + 值水平排列） */
lv_obj_t *ui_widget_metric(lv_obj_t *parent, const char *label,
                            const char *value, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_color_t value_color);

/* 创建彩色圆角标签 (chip) */
lv_obj_t *ui_widget_chip(lv_obj_t *parent, const char *text,
                          lv_color_t color, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w);

/* 创建进度条 */
lv_obj_t *ui_widget_bar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, int pct, lv_color_t color);

/* 创建迷你趋势线 (sparkline)
 * data: 历史数据数组 (float)
 * count: 数据点数
 * max_val: 数据最大值（用于缩放），<=0 时自动计算 */
lv_obj_t *ui_widget_mini_wave(lv_obj_t *parent, const float *data,
                                int count, float max_val,
                                lv_coord_t x, lv_coord_t y,
                                lv_coord_t w, lv_coord_t h,
                                lv_color_t color);

/* 创建环形进度图
 * size: 直径
 * pct: 百分比 0-100
 * text: 中央文字 */
lv_obj_t *ui_widget_donut(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t size, int pct,
                           lv_color_t color, const char *text);

#ifdef __cplusplus
}
#endif
#endif
