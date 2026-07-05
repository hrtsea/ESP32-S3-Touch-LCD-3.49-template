#include "ui_events.h"

#include "esp_log.h"
#include "event_bus.h"
#include "disp_driver.h"
#include "ui_clock.h"
#include "wifi_manager.h"
#include "app_cfg.h"
#include "ui_hello.h"
#include "ui_settings.h"
#include "ui_quotes.h"
#include "ui_helpers.h"

static const char *TAG = "ui_events";

static lv_timer_t *s_dim_timer = NULL;
static lv_timer_t *s_status_timer = NULL;
static lv_obj_t *s_ip_label = NULL;

static int s_current_tile_idx = 0;

static void ip_label_ensure(void)
{
    if (s_ip_label) return;
    s_ip_label = lv_label_create(lv_layer_top());
    lv_label_set_text(s_ip_label, "");
    lv_obj_set_style_text_color(s_ip_label, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(s_ip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(s_ip_label, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(s_ip_label, LV_OPA_50, 0);
    lv_obj_set_style_pad_left(s_ip_label, 4, 0);
    lv_obj_set_style_pad_right(s_ip_label, 4, 0);
    lv_obj_set_style_pad_top(s_ip_label, 1, 0);
    lv_obj_set_style_pad_bottom(s_ip_label, 1, 0);
    lv_obj_set_style_radius(s_ip_label, 3, 0);
    lv_obj_align(s_ip_label, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_add_flag(s_ip_label, LV_OBJ_FLAG_HIDDEN);
}

void ui_events_wifi_status_cb(bool connected, const char *ip_addr)
{
    if (!lvgl_lock(50)) return;
    ip_label_ensure();
    if (connected && ip_addr && *ip_addr) {
        lv_label_set_text(s_ip_label, ip_addr);
        lv_obj_clear_flag(s_ip_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ip_label, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
}

static void dim_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t idle_ms = lv_tick_elaps(ui_helpers_get_last_activity_ms());
    int want = 0;

    if (g_cfg.off_s > 0 && idle_ms >= (uint32_t)g_cfg.off_s * 1000) {
        want = 2;
    } else if (g_cfg.dim_s > 0 && idle_ms >= (uint32_t)g_cfg.dim_s * 1000) {
        want = 1;
    }

    if (want != ui_helpers_get_dim_state()) {
        ui_helpers_set_dim_state(want);
        if (want == 0) {
            ui_helpers_backlight_apply(g_cfg.brightness);
        } else if (want == 1) {
            ui_helpers_backlight_apply(g_cfg.brightness / 8 + 4);
        } else {
            ui_helpers_backlight_apply(0);
        }
    }
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;

    char ssid_buf[33];
    wifi_get_curr_ssid(ssid_buf, sizeof(ssid_buf));

    if (wifi_is_connected()) {
        clock_set_wifi_icon_color(lv_color_make(0x80, 0xff, 0x80));
    } else if (ssid_buf[0]) {
        clock_set_wifi_icon_color(lv_color_make(0xff, 0xa0, 0x40));
    } else {
        clock_set_wifi_icon_color(lv_color_make(0x40, 0x40, 0x40));
    }

    clock_set_bt_icon_color(lv_color_make(0x40, 0x40, 0x40));
}

void ui_events_start_dim_timer(void)
{
    if (s_dim_timer) return;
    s_dim_timer = lv_timer_create(dim_timer_cb, 1000, NULL);
}

void ui_events_stop_dim_timer(void)
{
    if (s_dim_timer) {
        lv_timer_del(s_dim_timer);
        s_dim_timer = NULL;
    }
}

void ui_events_start_status_timer(void)
{
    if (s_status_timer) return;
    s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
}

void ui_events_stop_status_timer(void)
{
    if (s_status_timer) {
        lv_timer_del(s_status_timer);
        s_status_timer = NULL;
    }
}

static void on_backlight_changed_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!evt || !evt->data || evt->data_len < sizeof(uint8_t)) return;
    uint8_t bri = *(uint8_t *)evt->data;
    if (ui_helpers_get_dim_state() == 0) {
        ui_helpers_backlight_apply(bri);
    }
}

static void on_show_fps_changed_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!evt || !evt->data || evt->data_len < sizeof(uint8_t)) return;
    uint8_t show = *(uint8_t *)evt->data;
    lv_obj_t *fps_lbl = disp_driver_get_fps_label();
    if (fps_lbl) {
        if (show) lv_obj_clear_flag(fps_lbl, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(fps_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_tile_changed_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!evt || !evt->data || evt->data_len < sizeof(int)) return;
    int idx = *(int *)evt->data;
    lv_obj_t *tv = ui_helpers_get_tileview();
    if (!tv) return;
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    lv_obj_set_tile_id(tv, idx, 0, LV_ANIM_OFF);
}

void ui_events_subscribe_events(void)
{
    event_bus_subscribe(EVENT_BACKLIGHT_CHANGED, on_backlight_changed_evt, NULL);
    event_bus_subscribe(EVENT_SHOW_FPS_CHANGED, on_show_fps_changed_evt, NULL);
    event_bus_subscribe(EVENT_TILE_CHANGED, on_tile_changed_evt, NULL);
}

void ui_events_unsubscribe_events(void)
{
    event_bus_unsubscribe(EVENT_BACKLIGHT_CHANGED, on_backlight_changed_evt);
    event_bus_unsubscribe(EVENT_SHOW_FPS_CHANGED, on_show_fps_changed_evt);
    event_bus_unsubscribe(EVENT_TILE_CHANGED, on_tile_changed_evt);
}

static void tileview_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    lv_obj_t *tv = lv_event_get_target(e);
    lv_coord_t x = lv_obj_get_scroll_x(tv);
    lv_coord_t w = lv_obj_get_width(tv);
    int idx = (w > 0) ? (x + w / 2) / w : 0;
    const int last = 3;

    if (dir == LV_DIR_LEFT && idx == last) {
        lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
    } else if (dir == LV_DIR_RIGHT && idx == 0) {
        lv_obj_set_tile_id(tv, last, 0, LV_ANIM_OFF);
    }
}

static void tileview_commit_cb(lv_event_t *e)
{
    static lv_coord_t s_press_x = 0;
    static lv_coord_t s_locked_x = 0;
    static bool s_committed = false;

    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *tv = lv_event_get_target(e);

    if (c == LV_EVENT_PRESSED) {
        lv_indev_t *id = lv_indev_get_act();
        lv_point_t p; lv_indev_get_point(id, &p);
        s_press_x = p.x;
        s_locked_x = lv_obj_get_scroll_x(tv);
        s_committed = false;
    } else if (c == LV_EVENT_SCROLL && !s_committed) {
        lv_indev_t *id = lv_indev_get_act();
        if (!id) return;
        lv_point_t p; lv_indev_get_point(id, &p);
        int dx = (int)p.x - (int)s_press_x;
        if (dx > 20 || dx < -20) {
            s_committed = true;
        } else {
            lv_obj_scroll_to_x(tv, s_locked_x, LV_ANIM_OFF);
        }
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        s_committed = false;
    }
}

static void screen_scroll_stamp_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_SCROLL) {
        ui_helpers_set_last_scroll_ms(lv_tick_get());
    }
}

static void tile_monitor_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_t *tv = ui_helpers_get_tileview();
    if (!tv) return;
    lv_coord_t x = lv_obj_get_scroll_x(tv);
    lv_coord_t w = lv_obj_get_width(tv);
    int idx = (w > 0) ? (x + w / 2) / w : 0;
    if (idx < 0) idx = 0;
    if (idx >= 4) idx = 3;
    if (idx != s_current_tile_idx) {
        s_current_tile_idx = idx;
    }
}

void ui_events_activity_kick(lv_event_t *e)
{
    (void)e;
    ui_helpers_notify_activity();
}

void ui_events_register_tileview_events(lv_obj_t *tv)
{
    lv_obj_add_event_cb(tv, tileview_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(tv, tileview_commit_cb, LV_EVENT_ALL, NULL);
}

void ui_events_register_screen_events(lv_obj_t *scr)
{
    lv_obj_add_event_cb(scr, ui_events_activity_kick, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, ui_events_activity_kick, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, screen_scroll_stamp_cb, LV_EVENT_ALL, NULL);
}

void ui_events_start_tile_monitor(void)
{
    static bool created = false;
    if (!created) {
        lv_timer_create(tile_monitor_cb, 100, NULL);
        created = true;
    }
}

void ui_events_rotate_screen(void)
{
    int new_rot = (disp_driver_get_rot_state() + 1) & 3;
    disp_driver_set_rot_state(new_rot);

    if (new_rot == 0 || new_rot == 2) {
        disp_driver_set_canvas_size(172, 640);
    } else {
        disp_driver_set_canvas_size(640, 172);
    }

    extern void disp_driver_update_resolution(void);
    disp_driver_update_resolution();

    lv_obj_clean(lv_scr_act());
    disp_driver_set_fps_label(NULL);
    ui_Hello_cleanup();
    ui_helpers_set_tileview(NULL);

    ui_Clock_cleanup();
    ui_Settings_cleanup();
    ui_Quotes_cleanup();

    ui_events_stop_dim_timer();
    ui_events_stop_status_timer();
    ui_events_unsubscribe_events();
}
