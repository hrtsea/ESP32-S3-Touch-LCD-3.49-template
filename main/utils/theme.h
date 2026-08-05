#ifndef THEME_H
#define THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef struct {
    lv_color_t bg;
    lv_color_t text;
    lv_color_t menu_surf;
    lv_color_t menu_hdr;
    lv_color_t menu_btn;
    lv_color_t sunmap_water_n;
    lv_color_t sunmap_water_d;
    lv_color_t sunmap_land_n;
    lv_color_t sunmap_land_d;
    lv_color_t ok;
    lv_color_t warn;
    lv_color_t danger;
    lv_color_t info;
    lv_color_t dim;

    lv_color_t accent;       /* 页面主强调色 (原 COLOR_PRIMARY / page_accent) */
    lv_color_t inactive;     /* 非激活/禁用色 */
    lv_color_t text_dim;     /* 次要文字 */
    lv_color_t card_bg;      /* 卡片背景 */
    lv_color_t panel;        /* 面板背景 */
    lv_color_t grid;         /* 网格线 */
    lv_color_t highlight;    /* 高亮/选中 */
    lv_color_t up_accent;    /* 上涨/上传 */
} theme_palette_t;

theme_palette_t theme_get(void);

#ifdef __cplusplus
}
#endif

#endif