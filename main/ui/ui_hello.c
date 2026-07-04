#include "ui_hello.h"

#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"

#include "ui_common.h"
#include "ui_main.h"
#include "audio_min.h"
#include "i18n.h"

static const char *TAG = "ui_hello";

lv_obj_t *g_hello_play_btn_label = NULL;

static void play_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (!g_cfg.audio_enable) {
        ESP_LOGI(TAG, "play ignored: audio disabled in settings");
        return;
    }

    bool now = !audio_min_is_playing();
    audio_min_play_midi(now);

    if (g_hello_play_btn_label) {
        lv_label_set_text(g_hello_play_btn_label, now ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }

    ESP_LOGI(TAG, "play toggled -> %s", now ? "PLAY" : "STOP");
}

void build_hello_tile(lv_obj_t *parent, const char *status_text)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hello = lv_label_create(parent);
    lv_label_set_long_mode(hello, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(hello, canvas_w / 3);
    lv_label_set_text(hello, "Hello World  *  Hello World  *  Hello World  *  ");
    lv_obj_set_style_text_color(hello, lv_color_white(), 0);
    lv_obj_set_style_text_font(hello, i18n_font(), 0);
    lv_obj_set_style_anim_speed(hello, 40, 0);
    lv_obj_set_style_text_align(hello, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(hello, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *status = lv_label_create(parent);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status, canvas_w - 20);
    lv_label_set_text(status, status_text);
    lv_obj_set_style_text_color(status, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(status, i18n_font(), 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *play_btn = lv_btn_create(parent);
    lv_obj_set_size(play_btn, 50, 50);
    lv_obj_align(play_btn, LV_ALIGN_BOTTOM_LEFT, 16, -8);
    lv_obj_set_style_radius(play_btn, 25, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_make(0x20, 0x80, 0x40), 0);
    lv_obj_add_event_cb(play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
    g_hello_play_btn_label = lv_label_create(play_btn);
    lv_label_set_text(g_hello_play_btn_label, audio_min_is_playing() ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(g_hello_play_btn_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_hello_play_btn_label, i18n_font(), 0);
    lv_obj_center(g_hello_play_btn_label);

    lv_obj_t *rot_btn = lv_btn_create(parent);
    lv_obj_set_size(rot_btn, 50, 50);
    lv_obj_align(rot_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -8);
    lv_obj_set_style_radius(rot_btn, 30, 0);
    lv_obj_set_style_bg_color(rot_btn, lv_color_make(0x40, 0x40, 0x80), 0);
    lv_obj_add_event_cb(rot_btn, rotate_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rot_label = lv_label_create(rot_btn);
    lv_label_set_text(rot_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rot_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(rot_label, i18n_font(), 0);
    lv_obj_center(rot_label);
}