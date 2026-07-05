#include "ui_hello.h"

#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"

#include "ui.h"
#include "audio_min.h"
#include "i18n.h"

static const char *TAG = "ui_hello";

/* ===== 1. 对象定义 ===== */
lv_obj_t *ui_Hello              = NULL;
lv_obj_t *ui_Hello_label_title  = NULL;
lv_obj_t *ui_Hello_label_status = NULL;
lv_obj_t *ui_Hello_btn_play    = NULL;
lv_obj_t *ui_Hello_btn_rotate  = NULL;

/* ===== 2. 静态样式变量 ===== */
static lv_style_t style_tile_bg;
static lv_style_t style_title;
static lv_style_t style_status;
static lv_style_t style_btn_play;
static lv_style_t style_btn_rotate;
static bool styles_inited = false;

static void init_styles(void)
{
    if (styles_inited) return;

    lv_style_init(&style_tile_bg);
    lv_style_set_bg_color(&style_tile_bg, lv_color_black());
    lv_style_set_bg_opa(&style_tile_bg, LV_OPA_COVER);

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_anim_speed(&style_title, 40);
    lv_style_set_text_align(&style_title, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&style_status);
    lv_style_set_text_color(&style_status, lv_color_make(0xa0, 0xa0, 0xa0));
    lv_style_set_text_align(&style_status, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&style_btn_play);
    lv_style_set_radius(&style_btn_play, 25);
    lv_style_set_bg_color(&style_btn_play, lv_color_make(0x20, 0x80, 0x40));

    lv_style_init(&style_btn_rotate);
    lv_style_set_radius(&style_btn_rotate, 30);
    lv_style_set_bg_color(&style_btn_rotate, lv_color_make(0x40, 0x40, 0x80));

    styles_inited = true;
}

/* ===== 3. 事件回调函数 ===== */
void ui_event_Hello_btn_play(lv_event_t *e)
{
    (void)e;
    if (!g_cfg.audio_enable) {
        ESP_LOGI(TAG, "play ignored: audio disabled in settings");
        return;
    }

    bool now = !audio_min_is_playing();
    audio_min_play_midi(now);

    /* 按钮内 label 是 btn 的第一个子对象 */
    if (ui_Hello_btn_play) {
        lv_obj_t *lbl = lv_obj_get_child(ui_Hello_btn_play, 0);
        if (lbl) lv_label_set_text(lbl, now ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }

    ESP_LOGI(TAG, "play toggled -> %s", now ? "PLAY" : "STOP");
}

void ui_event_Hello_btn_rotate(lv_event_t *e)
{
    rotate_btn_event_cb(e);
}

/* ===== 4. tile 创建函数 ===== */
void ui_Hello_create(lv_obj_t *parent, const char *status_text)
{
    init_styles();

    /* tile 容器 */
    ui_Hello = parent;
    lv_obj_clear_flag(ui_Hello, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(ui_Hello, &style_tile_bg, 0);

    /* 顶部循环滚动文字 */
    ui_Hello_label_title = lv_label_create(ui_Hello);
    lv_label_set_long_mode(ui_Hello_label_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(ui_Hello_label_title, disp_driver_get_canvas_w() / 3);
    lv_label_set_text(ui_Hello_label_title, "Hello World  *  Hello World  *  Hello World  *  ");
    lv_obj_add_style(ui_Hello_label_title, &style_title, 0);
    lv_obj_set_style_text_font(ui_Hello_label_title, i18n_font(), 0);
    lv_obj_align(ui_Hello_label_title, LV_ALIGN_TOP_MID, 0, 8);

    /* 中央状态文本 */
    ui_Hello_label_status = lv_label_create(ui_Hello);
    lv_label_set_long_mode(ui_Hello_label_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui_Hello_label_status, disp_driver_get_canvas_w() - 20);
    lv_label_set_text(ui_Hello_label_status, status_text);
    lv_obj_add_style(ui_Hello_label_status, &style_status, 0);
    lv_obj_set_style_text_font(ui_Hello_label_status, i18n_font(), 0);
    lv_obj_align(ui_Hello_label_status, LV_ALIGN_CENTER, 0, 0);

    /* 左下播放按钮 */
    ui_Hello_btn_play = lv_btn_create(ui_Hello);
    lv_obj_set_size(ui_Hello_btn_play, 50, 50);
    lv_obj_align(ui_Hello_btn_play, LV_ALIGN_BOTTOM_LEFT, 16, -8);
    lv_obj_add_style(ui_Hello_btn_play, &style_btn_play, 0);
    lv_obj_add_event_cb(ui_Hello_btn_play, ui_event_Hello_btn_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t *play_label = lv_label_create(ui_Hello_btn_play);
    lv_label_set_text(play_label, audio_min_is_playing() ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(play_label, i18n_font(), 0);
    lv_obj_center(play_label);

    /* 右下旋转按钮 */
    ui_Hello_btn_rotate = lv_btn_create(ui_Hello);
    lv_obj_set_size(ui_Hello_btn_rotate, 50, 50);
    lv_obj_align(ui_Hello_btn_rotate, LV_ALIGN_BOTTOM_RIGHT, -16, -8);
    lv_obj_add_style(ui_Hello_btn_rotate, &style_btn_rotate, 0);
    lv_obj_add_event_cb(ui_Hello_btn_rotate, ui_event_Hello_btn_rotate, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rot_label = lv_label_create(ui_Hello_btn_rotate);
    lv_label_set_text(rot_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rot_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(rot_label, i18n_font(), 0);
    lv_obj_center(rot_label);
}

/* ===== 5. tile 清理函数 ===== */
void ui_Hello_cleanup(void)
{
    ui_Hello              = NULL;
    ui_Hello_label_title  = NULL;
    ui_Hello_label_status = NULL;
    ui_Hello_btn_play    = NULL;
    ui_Hello_btn_rotate  = NULL;
}
