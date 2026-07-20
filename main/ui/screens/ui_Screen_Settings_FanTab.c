#include "ui_Screen_Settings_FanTab.h"
#include "../ui.h"
#include "esp_log.h"
#include "theme.h"

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);

static const char *TAG = "FanTab";

/* 颜色定义 */
#define COLOR_BG          lv_color_hex(0x000000)
#define COLOR_PANEL       lv_color_hex(0x101010)
#define COLOR_GRID        lv_color_hex(0x303030)
#define COLOR_CURVE       lv_color_hex(0x40E0D0)
#define COLOR_POINT       lv_color_hex(0xFFFFFF)
#define COLOR_POINT_SEL   lv_color_hex(0xFFFF00)
#define COLOR_TEXT        lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DIM    lv_color_hex(0xA0A0A0)
#define COLOR_PRIMARY     lv_color_hex(0x40E0D0)
#define COLOR_INACTIVE    lv_color_hex(0x333333)

/* 画布尺寸（buffer = 280*84*2 = ~47KB） */
#define CANVAS_W          280
#define CANVAS_H          84
#define CANVAS_MARGIN_L   30   /* 左侧 PWM 标尺区 */
#define CANVAS_MARGIN_B   14   /* 底部 Temp 标尺区 */
#define PLOT_W            (CANVAS_W - CANVAS_MARGIN_L - 4)
#define PLOT_H            (CANVAS_H - CANVAS_MARGIN_B - 4)
#define TEMP_MIN          0
#define TEMP_MAX          90
#define PWM_MIN           0
#define PWM_MAX           100
#define POINT_HIT_RADIUS  8

/* 静态画布缓冲区 */
static uint8_t s_canvas_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H)];

static lv_obj_t *s_tab_page = NULL;
static lv_obj_t *s_canvas = NULL;
static lv_obj_t *s_mode_dd = NULL;
static lv_obj_t *s_source_dd = NULL;
static lv_obj_t *s_enable_sw = NULL;
static lv_obj_t *s_apply_btn = NULL;
static lv_obj_t *s_point_labels[FAN_CURVE_POINTS] = {0};
static lv_obj_t *s_hyst_dd = NULL;
static lv_obj_t *s_minpwm_dd = NULL;
static lv_obj_t *s_emerg_dd = NULL;

/* 编辑副本 */
static FanConfig s_editing_cfg;
static int8_t s_dragging_point_idx = -1;
static bool s_dirty = false;

/* ============================================================
 * 坐标换算：温度/PWM <-> 像素
 * ============================================================ */
static lv_point_t temp_pwm_to_pixel(int16_t temp, uint8_t pwm)
{
    lv_point_t p;
    int16_t t = temp;
    if (t < TEMP_MIN) t = TEMP_MIN;
    if (t > TEMP_MAX) t = TEMP_MAX;
    int pwm_i = pwm;
    if (pwm_i < PWM_MIN) pwm_i = PWM_MIN;
    if (pwm_i > PWM_MAX) pwm_i = PWM_MAX;
    p.x = (lv_coord_t)(CANVAS_MARGIN_L + (int32_t)(t - TEMP_MIN) * PLOT_W / (TEMP_MAX - TEMP_MIN));
    p.y = (lv_coord_t)(PLOT_H - (int32_t)(pwm_i - PWM_MIN) * PLOT_H / (PWM_MAX - PWM_MIN));
    return p;
}

static int16_t pixel_to_temp(lv_coord_t x)
{
    if (x < CANVAS_MARGIN_L) x = CANVAS_MARGIN_L;
    if (x > CANVAS_MARGIN_L + PLOT_W) x = CANVAS_MARGIN_L + PLOT_W;
    int32_t t = TEMP_MIN + (int32_t)(x - CANVAS_MARGIN_L) * (TEMP_MAX - TEMP_MIN) / PLOT_W;
    return (int16_t)t;
}

static int pixel_to_pwm(lv_coord_t y)
{
    if (y < 0) y = 0;
    if (y > PLOT_H) y = PLOT_H;
    int32_t p = PWM_MIN + (int32_t)(PLOT_H - y) * (PWM_MAX - PWM_MIN) / PLOT_H;
    return (int)p;
}

/* ============================================================
 * 绘制画布（网格 + 曲线 + 点）
 * ============================================================ */
static void draw_canvas(void)
{
    lv_canvas_fill_bg(s_canvas, COLOR_PANEL, LV_OPA_COVER);

    /* 网格线描述符 */
    lv_draw_line_dsc_t grid_dsc;
    lv_draw_line_dsc_init(&grid_dsc);
    grid_dsc.color = COLOR_GRID;
    grid_dsc.width = 1;

    /* 水平网格 (PWM 0/25/50/75/100) */
    for (int pwm = 0; pwm <= 100; pwm += 25) {
        lv_point_t pts[2];
        pts[0] = temp_pwm_to_pixel(TEMP_MIN, pwm);
        pts[1] = temp_pwm_to_pixel(TEMP_MAX, pwm);
        lv_canvas_draw_line(s_canvas, pts, 2, &grid_dsc);
    }
    /* 垂直网格 (Temp 0/30/60/90) */
    for (int t = 0; t <= 90; t += 30) {
        lv_point_t pts[2];
        pts[0] = temp_pwm_to_pixel(t, PWM_MIN);
        pts[1] = temp_pwm_to_pixel(t, PWM_MAX);
        lv_canvas_draw_line(s_canvas, pts, 2, &grid_dsc);
    }

    /* 标尺文字 */
    lv_draw_label_dsc_t lbl_dsc;
    lv_draw_label_dsc_init(&lbl_dsc);
    lbl_dsc.color = COLOR_TEXT_DIM;
    lbl_dsc.font = &lv_font_montserrat_12;
    lbl_dsc.align = LV_TEXT_ALIGN_RIGHT;

    char buf[8];
    for (int pwm = 0; pwm <= 100; pwm += 50) {
        lv_point_t p = temp_pwm_to_pixel(TEMP_MIN, pwm);
        snprintf(buf, sizeof(buf), "%d", pwm);
        lv_canvas_draw_text(s_canvas, 0, p.y - 6, CANVAS_MARGIN_L - 2, &lbl_dsc, buf);
    }

    lbl_dsc.align = LV_TEXT_ALIGN_CENTER;
    for (int t = 30; t <= 90; t += 30) {
        lv_point_t p = temp_pwm_to_pixel(t, PWM_MIN);
        snprintf(buf, sizeof(buf), "%d", t);
        lv_canvas_draw_text(s_canvas, p.x - 12, CANVAS_H - CANVAS_MARGIN_B + 2, 24, &lbl_dsc, buf);
    }

    /* 曲线折线 */
    lv_draw_line_dsc_t curve_dsc;
    lv_draw_line_dsc_init(&curve_dsc);
    curve_dsc.color = COLOR_CURVE;
    curve_dsc.width = 2;
    for (int i = 0; i < FAN_CURVE_POINTS - 1; i++) {
        lv_point_t pts[2];
        pts[0] = temp_pwm_to_pixel(s_editing_cfg.curve[i].temp, s_editing_cfg.curve[i].pwm_pct);
        pts[1] = temp_pwm_to_pixel(s_editing_cfg.curve[i+1].temp, s_editing_cfg.curve[i+1].pwm_pct);
        lv_canvas_draw_line(s_canvas, pts, 2, &curve_dsc);
    }

    /* 5 个控制点（实心圆） */
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.radius = LV_RADIUS_CIRCLE;
    rect_dsc.border_width = 0;
    for (int i = 0; i < FAN_CURVE_POINTS; i++) {
        lv_point_t p = temp_pwm_to_pixel(s_editing_cfg.curve[i].temp, s_editing_cfg.curve[i].pwm_pct);
        rect_dsc.bg_color = (i == s_dragging_point_idx) ? COLOR_POINT_SEL : COLOR_POINT;
        lv_canvas_draw_rect(s_canvas, p.x - 4, p.y - 4, 8, 8, &rect_dsc);
    }
}

/* ============================================================
 * 更新右侧点列表显示
 * ============================================================ */
static void update_point_labels(void)
{
    for (int i = 0; i < FAN_CURVE_POINTS; i++) {
        if (!s_point_labels[i]) continue;
        char buf[24];
        snprintf(buf, sizeof(buf), "P%d: %d°C / %d%%",
                 i + 1, s_editing_cfg.curve[i].temp, s_editing_cfg.curve[i].pwm_pct);
        lv_label_set_text(s_point_labels[i], buf);
    }
}

static void update_dirty_state(void)
{
    s_dirty = true;
    if (s_apply_btn) {
        lv_obj_set_style_bg_color(s_apply_btn, COLOR_PRIMARY, 0);
    }
}

/* ============================================================
 * 事件回调
 * ============================================================ */
static void canvas_pressed_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    /* 获取屏幕坐标点 */
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    /* 转换为画布内坐标：减去画布在屏幕中的左上角坐标 */
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    lv_coord_t cx = p.x - obj_coords.x1;
    lv_coord_t cy = p.y - obj_coords.y1;

    if (code == LV_EVENT_PRESSED) {
        s_dragging_point_idx = -1;
        for (int i = 0; i < FAN_CURVE_POINTS; i++) {
            lv_point_t pt = temp_pwm_to_pixel(s_editing_cfg.curve[i].temp, s_editing_cfg.curve[i].pwm_pct);
            int32_t dx = cx - pt.x;
            int32_t dy = cy - pt.y;
            if (dx * dx + dy * dy <= POINT_HIT_RADIUS * POINT_HIT_RADIUS) {
                s_dragging_point_idx = (int8_t)i;
                break;
            }
        }
    } else if (code == LV_EVENT_PRESSING || code == LV_EVENT_PRESS_LOST) {
        if (s_dragging_point_idx < 0) return;
        int i = s_dragging_point_idx;

        /* 限幅：温度在 [前一点+1, 后一点-1] */
        int16_t min_t = (i == 0) ? TEMP_MIN : s_editing_cfg.curve[i-1].temp + 1;
        int16_t max_t = (i == FAN_CURVE_POINTS - 1) ? TEMP_MAX : s_editing_cfg.curve[i+1].temp - 1;

        int16_t new_temp = pixel_to_temp(cx);
        int new_pwm = pixel_to_pwm(cy);
        if (new_temp < min_t) new_temp = min_t;
        if (new_temp > max_t) new_temp = max_t;
        if (new_pwm < 0) new_pwm = 0;
        if (new_pwm > 100) new_pwm = 100;

        if (s_editing_cfg.curve[i].temp != new_temp || s_editing_cfg.curve[i].pwm_pct != new_pwm) {
            s_editing_cfg.curve[i].temp = new_temp;
            s_editing_cfg.curve[i].pwm_pct = (uint8_t)new_pwm;
            draw_canvas();
            update_point_labels();
            update_dirty_state();
        }
    } else if (code == LV_EVENT_RELEASED) {
        s_dragging_point_idx = -1;
        draw_canvas();
    }
}

static void mode_dd_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    s_editing_cfg.mode = (FanMode)lv_dropdown_get_selected(s_mode_dd);
    update_dirty_state();
    ESP_LOGI(TAG, "mode=%d", s_editing_cfg.mode);
}

static void source_dd_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    s_editing_cfg.temp_source = (TempSource)lv_dropdown_get_selected(s_source_dd);
    update_dirty_state();
}

static void enable_sw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    s_editing_cfg.enabled = lv_obj_has_state(s_enable_sw, LV_STATE_CHECKED);
    update_dirty_state();
}

static void hyst_dd_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    s_editing_cfg.hysteresis = (uint8_t)lv_dropdown_get_selected(s_hyst_dd);
    update_dirty_state();
}

static void minpwm_dd_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    s_editing_cfg.min_pwm_pct = (uint8_t)lv_dropdown_get_selected(s_minpwm_dd);
    update_dirty_state();
}

static void emerg_dd_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    int sel = lv_dropdown_get_selected(s_emerg_dd);
    s_editing_cfg.emergency_temp = (int16_t)(50 + sel * 5);
    update_dirty_state();
}

void fan_tab_save(lv_event_t *e)
{
    (void)e;
    config_save_fan(&s_editing_cfg);
    s_dirty = false;
    if (s_apply_btn) {
        lv_obj_set_style_bg_color(s_apply_btn, COLOR_INACTIVE, 0);
    }
    ESP_LOGI(TAG, "fan config saved: mode=%d enabled=%d",
             s_editing_cfg.mode, s_editing_cfg.enabled);
}

/* ============================================================
 * 创建顶部控制条
 * ============================================================ */
static void create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 620, 28);
    lv_obj_set_style_bg_color(bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    /* Mode 下拉 */
    lv_obj_t *lbl_mode = lv_label_create(bar);
    lv_label_set_text(lbl_mode, "Mode:");
    lv_obj_set_style_text_color(lbl_mode, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_mode, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_mode, LV_ALIGN_LEFT_MID, 2, 0);

    s_mode_dd = lv_dropdown_create(bar);
    lv_dropdown_set_options(s_mode_dd, "AUTO\nMANUAL");
    lv_obj_set_size(s_mode_dd, 70, 22);
    lv_obj_align(s_mode_dd, LV_ALIGN_LEFT_MID, 38, 0);
    lv_obj_set_style_text_font(s_mode_dd, &lv_font_montserrat_12, 0);
    lv_dropdown_set_selected(s_mode_dd, (uint32_t)s_editing_cfg.mode);
    lv_obj_add_event_cb(s_mode_dd, mode_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Source 下拉 */
    lv_obj_t *lbl_src = lv_label_create(bar);
    lv_label_set_text(lbl_src, "Src:");
    lv_obj_set_style_text_color(lbl_src, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_src, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_src, LV_ALIGN_LEFT_MID, 118, 0);

    s_source_dd = lv_dropdown_create(bar);
    lv_dropdown_set_options(s_source_dd, "MAX CPU/SYS\nAVG CPU/SYS\nCPU ONLY\nSYS ONLY");
    lv_obj_set_size(s_source_dd, 100, 22);
    lv_obj_align(s_source_dd, LV_ALIGN_LEFT_MID, 152, 0);
    lv_obj_set_style_text_font(s_source_dd, &lv_font_montserrat_12, 0);
    lv_dropdown_set_selected(s_source_dd, (uint32_t)s_editing_cfg.temp_source);
    lv_obj_add_event_cb(s_source_dd, source_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Enable 开关 */
    lv_obj_t *lbl_en = lv_label_create(bar);
    lv_label_set_text(lbl_en, "On:");
    lv_obj_set_style_text_color(lbl_en, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_en, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_en, LV_ALIGN_LEFT_MID, 262, 0);

    s_enable_sw = lv_switch_create(bar);
    lv_obj_set_size(s_enable_sw, 36, 18);
    lv_obj_align(s_enable_sw, LV_ALIGN_LEFT_MID, 288, 0);
    if (s_editing_cfg.enabled) {
        lv_obj_add_state(s_enable_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_enable_sw, enable_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Apply 按钮 */
    s_apply_btn = lv_btn_create(bar);
    lv_obj_set_size(s_apply_btn, 70, 22);
    lv_obj_set_style_bg_color(s_apply_btn, COLOR_INACTIVE, 0);
    lv_obj_set_style_radius(s_apply_btn, 3, 0);
    lv_obj_align(s_apply_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_add_event_cb(s_apply_btn, fan_tab_save, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_apply = lv_label_create(s_apply_btn);
    lv_label_set_text(lbl_apply, "Apply");
    lv_obj_set_style_text_color(lbl_apply, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_apply, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_apply);
}

/* ============================================================
 * 创建中间画布 + 右侧点列表
 * ============================================================ */
static void create_canvas_area(lv_obj_t *parent)
{
    lv_obj_t *area = lv_obj_create(parent);
    lv_obj_set_size(area, 620, 90);
    lv_obj_set_style_bg_color(area, COLOR_BG, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_radius(area, 0, 0);
    lv_obj_set_style_pad_all(area, 0, 0);
    lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(area, LV_ALIGN_TOP_MID, 0, 32);

    /* 左侧画布 */
    s_canvas = lv_canvas_create(area);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_canvas, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_canvas, canvas_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_canvas, canvas_pressed_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_canvas, canvas_pressed_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_canvas, canvas_pressed_cb, LV_EVENT_PRESS_LOST, NULL);

    /* 右侧点列表 */
    lv_obj_t *points_panel = lv_obj_create(area);
    lv_obj_set_size(points_panel, 320, 88);
    lv_obj_set_style_bg_color(points_panel, COLOR_PANEL, 0);
    lv_obj_set_style_border_width(points_panel, 0, 0);
    lv_obj_set_style_radius(points_panel, 0, 0);
    lv_obj_set_style_pad_all(points_panel, 2, 0);
    lv_obj_set_flex_flow(points_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(points_panel, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *title = lv_label_create(points_panel);
    lv_label_set_text(title, "Curve Points:");
    lv_obj_set_style_text_color(title, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);

    for (int i = 0; i < FAN_CURVE_POINTS; i++) {
        s_point_labels[i] = lv_label_create(points_panel);
        lv_obj_set_style_text_color(s_point_labels[i], COLOR_TEXT, 0);
        lv_obj_set_style_text_font(s_point_labels[i], &lv_font_montserrat_12, 0);
    }
    update_point_labels();

    draw_canvas();
}

/* ============================================================
 * 创建底部参数行
 * ============================================================ */
static void create_params_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 620, 24);
    lv_obj_set_style_bg_color(row, COLOR_BG, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 124);

    /* Hysteresis */
    lv_obj_t *lbl_h = lv_label_create(row);
    lv_label_set_text(lbl_h, "Hyst:");
    lv_obj_set_style_text_color(lbl_h, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_h, &lv_font_montserrat_12, 0);

    s_hyst_dd = lv_dropdown_create(row);
    lv_dropdown_set_options(s_hyst_dd, "0\n1\n2\n3\n4\n5\n6\n8\n10");
    lv_obj_set_size(s_hyst_dd, 40, 20);
    lv_obj_set_style_text_font(s_hyst_dd, &lv_font_montserrat_12, 0);
    {
        const int opts[] = {0, 1, 2, 3, 4, 5, 6, 8, 10};
        int sel = 0;
        for (int i = 0; i < 9; i++) if (opts[i] == s_editing_cfg.hysteresis) { sel = i; break; }
        lv_dropdown_set_selected(s_hyst_dd, sel);
    }
    lv_obj_add_event_cb(s_hyst_dd, hyst_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Min PWM */
    lv_obj_t *lbl_m = lv_label_create(row);
    lv_label_set_text(lbl_m, "Min:");
    lv_obj_set_style_text_color(lbl_m, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_12, 0);

    s_minpwm_dd = lv_dropdown_create(row);
    lv_dropdown_set_options(s_minpwm_dd, "0\n10\n15\n20\n25\n30\n40\n50");
    lv_obj_set_size(s_minpwm_dd, 40, 20);
    lv_obj_set_style_text_font(s_minpwm_dd, &lv_font_montserrat_12, 0);
    {
        const int opts[] = {0, 10, 15, 20, 25, 30, 40, 50};
        int sel = 0;
        for (int i = 0; i < 8; i++) if (opts[i] == s_editing_cfg.min_pwm_pct) { sel = i; break; }
        lv_dropdown_set_selected(s_minpwm_dd, sel);
    }
    lv_obj_add_event_cb(s_minpwm_dd, minpwm_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Emergency Temp */
    lv_obj_t *lbl_e = lv_label_create(row);
    lv_label_set_text(lbl_e, "Emerg:");
    lv_obj_set_style_text_color(lbl_e, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_e, &lv_font_montserrat_12, 0);

    s_emerg_dd = lv_dropdown_create(row);
    lv_dropdown_set_options(s_emerg_dd,
        "50\n55\n60\n65\n70\n75\n80\n85\n90");
    lv_obj_set_size(s_emerg_dd, 45, 20);
    lv_obj_set_style_text_font(s_emerg_dd, &lv_font_montserrat_12, 0);
    {
        int sel = 0;
        for (int i = 0; i < 9; i++) if (50 + i * 5 == s_editing_cfg.emergency_temp) { sel = i; break; }
        lv_dropdown_set_selected(s_emerg_dd, sel);
    }
    lv_obj_add_event_cb(s_emerg_dd, emerg_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 状态标签 */
    lv_obj_t *lbl_status = lv_label_create(row);
    lv_label_set_text(lbl_status, "°C  Drag points to edit");
    lv_obj_set_style_text_color(lbl_status, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
}

/* ============================================================
 * 公开 API
 * ============================================================ */
void ui_Screen_Settings_FanTab_init(lv_obj_t *parent)
{
    if (s_tab_page) return;

    /* 从全局配置加载编辑副本 */
    memcpy(&s_editing_cfg, &g_config.fan, sizeof(FanConfig));

    s_tab_page = lv_tabview_add_tab(parent, "Fan");
    lv_obj_set_style_bg_color(s_tab_page, COLOR_BG, 0);
    lv_obj_set_style_border_width(s_tab_page, 0, 0);
    lv_obj_set_style_pad_all(s_tab_page, 2, 0);
    lv_obj_clear_flag(s_tab_page, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar(s_tab_page);
    create_canvas_area(s_tab_page);
    create_params_row(s_tab_page);

    ESP_LOGI(TAG, "FanTab initialized (mode=%d enabled=%d)",
             s_editing_cfg.mode, s_editing_cfg.enabled);
}

void ui_Screen_Settings_FanTab_cleanup(void)
{
    s_tab_page = NULL;
    s_canvas = NULL;
    s_mode_dd = NULL;
    s_source_dd = NULL;
    s_enable_sw = NULL;
    s_apply_btn = NULL;
    s_hyst_dd = NULL;
    s_minpwm_dd = NULL;
    s_emerg_dd = NULL;
    s_dragging_point_idx = -1;
    for (int i = 0; i < FAN_CURVE_POINTS; i++) {
        s_point_labels[i] = NULL;
    }
}
