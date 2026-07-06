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
} theme_palette_t;

theme_palette_t theme_get(void);

#ifdef __cplusplus
}
#endif

#endif