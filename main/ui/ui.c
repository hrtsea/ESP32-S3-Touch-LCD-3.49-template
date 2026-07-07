#include "ui.h"

#include "esp_log.h"
#include "utils/bg_fetcher.h"
#include "screens/ui_Screen_Boot.h"
#include "screens/ui_Screen_Overview.h"

static const char *TAG = "ui";

static bool s_fps_timer_created = false;

static void create_global_fps_label(void)
{
    static bool created = false;
    if (created) return;

    lv_obj_t *fps_lbl = lv_label_create(lv_layer_top());
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

    created = true;
}

void ui_init(void)
{
    wifi_manager_register_status_cb(ui_events_wifi_status_cb);

    if (!lvgl_lock(-1)) return;

    create_global_fps_label();

    if (!s_fps_timer_created) {
        lv_timer_create(disp_driver_fps_timer_cb, 3000, NULL);
        s_fps_timer_created = true;
    }

    ui_Screen_Boot_screen_init();
    lv_scr_load(ui_Screen_Boot);

    ui_Screen_Boot_start_progress();

    lvgl_unlock();

    bg_fetcher_ensure();
}




