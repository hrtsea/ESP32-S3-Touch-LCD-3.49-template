#include "ui_widgets.h"
#include "theme.h"
#include <math.h>
#include <stdio.h>

/* ========== 页面主题色系统 ========== */

lv_color_t page_accent_color(page_idx_t idx)
{
    (void)idx;
    return theme_get().accent;
}

const char *page_accent_name(page_idx_t idx)
{
    switch (idx) {
    case PAGE_IDX_SETTINGS: return "设置";
    case PAGE_IDX_OVERVIEW: return "总览";
    case PAGE_IDX_STORAGE:  return "存储";
    case PAGE_IDX_SDCOPY:   return "SD拷贝";
    default:                return "";
    }
}

/* ========== 健康状态颜色 ========== */

lv_color_t health_color(HealthStatus health)
{
    theme_palette_t t = theme_get();
    switch (health) {
    case HEALTH_OK:       return t.ok;
    case HEALTH_WARNING:  return t.warn;
    case HEALTH_CRITICAL: return t.danger;
    default:              return t.dim;
    }
}

/* ========== UI 组件原语 ========== */

LV_FONT_DECLARE(lv_font_montserrat_12);

lv_obj_t *ui_widget_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h,
                          lv_color_t border_color)
{
    theme_palette_t t = theme_get();
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, t.card_bg, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, border_color, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 4, 0);
    lv_obj_set_style_shadow_ofs_x(card, 1, 0);
    lv_obj_set_style_shadow_ofs_y(card, 2, 0);
    lv_obj_set_style_shadow_color(card, t.dim, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return card;
}

lv_obj_t *ui_widget_label(lv_obj_t *parent, const char *text,
                           lv_color_t color, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl, x, y);
    if (w > 0) lv_obj_set_width(lbl, w);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return lbl;
}

lv_obj_t *ui_widget_label_font(lv_obj_t *parent, const char *text,
                                lv_color_t color, const lv_font_t *font,
                                lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
    lv_obj_t *lbl = ui_widget_label(parent, text, color, x, y, w);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

lv_obj_t *ui_widget_metric(lv_obj_t *parent, const char *label,
                            const char *value, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_color_t value_color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    if (w > 0) lv_obj_set_width(lbl, w);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* 用不同颜色渲染标签和值 */
    lv_label_set_recolor(lbl, true);
    char buf[128];
    snprintf(buf, sizeof(buf), "#808080 %s# #FFFFFF %s", label, value);
    /* recolor 格式：标签灰色，值白色 */
    char recolor_buf[128];
    snprintf(recolor_buf, sizeof(recolor_buf), "#808080 %s#", label);
    lv_label_set_text(lbl, recolor_buf);

    /* 追加值部分 */
    lv_obj_t *val_lbl = lv_label_create(parent);
    lv_label_set_text(val_lbl, value);
    lv_obj_set_style_text_color(val_lbl, value_color, 0);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(val_lbl, LV_ALIGN_TOP_LEFT, x + 60, y);
    lv_obj_add_flag(val_lbl, LV_OBJ_FLAG_GESTURE_BUBBLE);

    return lbl;
}

lv_obj_t *ui_widget_chip(lv_obj_t *parent, const char *text,
                          lv_color_t color, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_pos(chip, x, y);
    lv_obj_set_size(chip, w, 18);
    lv_obj_set_style_bg_color(chip, color, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_30, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, color, 0);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *lbl = lv_label_create(chip);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);

    return chip;
}

lv_obj_t *ui_widget_bar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, int pct, lv_color_t color)
{
    theme_palette_t t = theme_get();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, 8);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, t.inactive, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);

    return bar;
}

lv_obj_t *ui_widget_mini_wave(lv_obj_t *parent, const float *data,
                                int count, float max_val,
                                lv_coord_t x, lv_coord_t y,
                                lv_coord_t w, lv_coord_t h,
                                lv_color_t color)
{
    if (count <= 0 || w <= 0 || h <= 0) return NULL;

    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_obj_set_pos(canvas, x, y);

    /* 创建画布缓冲区 */
    static uint8_t buf[256 * 32 * 2];  /* 最大 256x32 RGB565 */
    int cw = w > 256 ? 256 : w;
    int ch = h > 32 ? 32 : h;
    lv_canvas_set_buffer(canvas, buf, cw, ch, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(canvas, cw, ch);

    /* 清空为透明 */
    lv_canvas_fill_bg(canvas, theme_get().bg, LV_OPA_TRANSP);

    /* 自动缩放 */
    if (max_val <= 0) {
        max_val = 0;
        for (int i = 0; i < count; i++) {
            if (data[i] > max_val) max_val = data[i];
        }
        max_val *= 1.1f;
        if (max_val < 1.0f) max_val = 1.0f;
    }

    /* 画趋势线 */
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = 1;
    line_dsc.opa = LV_OPA_COVER;

    for (int i = 0; i < count - 1; i++) {
        lv_point_t p1, p2;
        p1.x = (int)((float)i * cw / count);
        p2.x = (int)((float)(i + 1) * cw / count);
        p1.y = ch - 1 - (int)(data[i] / max_val * (ch - 1));
        p2.y = ch - 1 - (int)(data[i + 1] / max_val * (ch - 1));
        if (p1.y < 0) p1.y = 0;
        if (p1.y >= ch) p1.y = ch - 1;
        if (p2.y < 0) p2.y = 0;
        if (p2.y >= ch) p2.y = ch - 1;
        lv_point_t pts[2] = { p1, p2 };
        lv_canvas_draw_line(canvas, pts, 2, &line_dsc);
    }

    lv_obj_add_flag(canvas, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return canvas;
}

lv_obj_t *ui_widget_donut(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t size, int pct,
                           lv_color_t color, const char *text)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    lv_obj_t *meter = lv_meter_create(parent);
    lv_obj_set_pos(meter, x, y);
    lv_obj_set_size(meter, size, size);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_range(meter, scale, 0, 100, 270, 135);
    lv_meter_set_scale_ticks(meter, scale, 0, 0, 0, theme_get().bg);

    theme_palette_t t = theme_get();
    lv_meter_add_arc(meter, scale, size / 6, t.inactive, 0);
    lv_meter_indicator_t *indic = lv_meter_add_arc(meter, scale, size / 6, color, 0);
    lv_meter_set_indicator_start_value(meter, indic, 0);
    lv_meter_set_indicator_end_value(meter, indic, pct);

    /* 中央文字 */
    lv_obj_t *lbl = lv_label_create(meter);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);

    lv_obj_add_flag(meter, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return meter;
}
