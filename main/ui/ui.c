/**
 * @file ui.c
 * @brief UI主模块 - 负责顶层UI构建和TileView管理
 * 
 * 本模块是整个UI系统的入口，负责：
 * - 创建和管理TileView（4个页面的滑动容器）
 * - 协调各子UI模块（时钟、行情、设置、演示页）
 * 
 * 业务逻辑已拆分到 ui_helpers.c（状态管理）和 ui_events.c（事件处理）
 */

#include "ui.h"

#include "esp_log.h"
#include "ui_clock.h"
#include "ui_quotes.h"
#include "ui_settings.h"
#include "ui_hello.h"

static const char *TAG = "ui";

static void build_main_ui(const char *status_text);

static void build_main_ui(const char *status_text)
{
    static bool fps_timer_created = false;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *tv = lv_tileview_create(scr);
    ui_helpers_set_tileview(tv);
    lv_obj_set_size(tv, disp_driver_get_canvas_w(), disp_driver_get_canvas_h());
    lv_obj_set_style_bg_color(tv, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *t_clock = lv_tileview_add_tile(tv, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_quotes = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_set = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_hello = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT | LV_DIR_RIGHT);

    (void)status_text;

    ui_Clock_create(t_clock);
    ui_Quotes_create(t_quotes);
    ui_Settings_create(t_set);
    ui_Hello_create(t_hello, status_text);

    ui_events_register_tileview_events(tv);

    lv_obj_t *fps_lbl = lv_label_create(scr);
    disp_driver_set_fps_label(fps_lbl);
    lv_label_set_text(fps_lbl, "FPS --");
    lv_obj_set_style_text_color(fps_lbl, lv_color_make(0x00, 0xff, 0x80), 0);
    lv_obj_set_style_text_font(fps_lbl, i18n_font(), 0);
    lv_obj_set_style_bg_color(fps_lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fps_lbl, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(fps_lbl, 3, 0);
    lv_obj_align(fps_lbl, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_clear_flag(fps_lbl, LV_OBJ_FLAG_CLICKABLE);
    if (!g_cfg.show_fps) lv_obj_add_flag(fps_lbl, LV_OBJ_FLAG_HIDDEN);

    ui_events_subscribe_events();

    if (!fps_timer_created) {
        lv_timer_create(disp_driver_fps_timer_cb, 3000, NULL);
        fps_timer_created = true;
    }

    ui_events_start_tile_monitor();

    ui_events_register_screen_events(scr);

    ui_helpers_set_last_activity_ms(lv_tick_get());
    ui_events_start_dim_timer();
    ui_events_start_status_timer();

    lv_obj_set_tile_id(ui_helpers_get_tileview(), 0, 0, LV_ANIM_OFF);
}

void show_main_ui(const char *status_text)
{
    if (status_text) {
        ui_helpers_set_status_text(status_text);
    }

    wifi_manager_register_status_cb(ui_events_wifi_status_cb);

    if (!lvgl_lock(-1)) return;
    build_main_ui(status_text ? status_text : "");
    lvgl_unlock();
}

void rotate_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ui_events_rotate_screen();
    char st[256];
    ui_helpers_get_status_text(st, sizeof(st));
    build_main_ui(st);
    ESP_LOGI(TAG, "rotate -> %d deg  canvas=%dx%d",
             disp_driver_get_rot_state() * 90,
             disp_driver_get_canvas_w(), disp_driver_get_canvas_h());
}
