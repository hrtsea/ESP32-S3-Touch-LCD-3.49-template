#include "ui_main.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "ui_clock.h"
#include "ui_quotes.h"
#include "ui_radio.h"
#include "ui_settings.h"
#include "ui_recorder.h"
#include "audio_min.h"
#include "wifi_manager.h"

static const char *TAG = "ui_main";

/* ---------------------- Globals owned by ui_main ---------------------- */

lv_obj_t      *g_tileview       = NULL;
lv_timer_t    *g_status_timer   = NULL;
char           g_status_text[256];

/* Auto-dim timer (owned here, reads g_cfg / backlight from main.cpp). */
static lv_timer_t *g_dim_timer = NULL;

/* Play button label on the hello tile. */
static lv_obj_t   *play_btn_label  = NULL;

/* IP label (displays Wi-Fi connection status). */
static lv_obj_t   *g_ip_label = NULL;

/* FPS pill (parented to screen, floats above every tile).
 * fps_label / fps_frame_count are declared in ui_common.h and defined
 * in main.cpp (future disp_driver). */

/* ---------------------- IP Label UI ---------------------- */

static void ip_label_ensure(void)
{
    if (g_ip_label) return;
    g_ip_label = lv_label_create(lv_layer_top());
    lv_label_set_text(g_ip_label, "");
    lv_obj_set_style_text_color(g_ip_label, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(g_ip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(g_ip_label, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(g_ip_label, LV_OPA_50, 0);
    lv_obj_set_style_pad_left(g_ip_label, 4, 0);
    lv_obj_set_style_pad_right(g_ip_label, 4, 0);
    lv_obj_set_style_pad_top(g_ip_label, 1, 0);
    lv_obj_set_style_pad_bottom(g_ip_label, 1, 0);
    lv_obj_set_style_radius(g_ip_label, 3, 0);
    lv_obj_align(g_ip_label, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_status_cb(bool connected, const char *ip_addr)
{
    if (!lvgl_lock(50)) return;
    ip_label_ensure();
    if (connected && ip_addr && *ip_addr) {
        lv_label_set_text(g_ip_label, ip_addr);
        lv_obj_clear_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
}

/* ---------------------- Wi-Fi reason strings ---------------------- */

static const char *wifi_reason_str(uint8_t r)
{
    switch (r) {
        case 0:                              return "";
        case WIFI_REASON_AUTH_EXPIRE:        return "auth expired";
        case WIFI_REASON_AUTH_LEAVE:         return "auth leave";
        case WIFI_REASON_ASSOC_EXPIRE:       return "assoc expired";
        case WIFI_REASON_ASSOC_TOOMANY:      return "AP full";
        case WIFI_REASON_NOT_AUTHED:         return "not authed";
        case WIFI_REASON_NOT_ASSOCED:        return "not assoced";
        case WIFI_REASON_ASSOC_LEAVE:        return "assoc leave";
        case WIFI_REASON_ASSOC_NOT_AUTHED:   return "assoc not authed";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD:return "pwrcap bad";
        case WIFI_REASON_BEACON_TIMEOUT:     return "beacon timeout";
        case WIFI_REASON_NO_AP_FOUND:        return "AP not found";
        case WIFI_REASON_AUTH_FAIL:          return "auth fail (wrong pass?)";
        case WIFI_REASON_ASSOC_FAIL:         return "assoc fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:  return "handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL:    return "connection fail";
        case WIFI_REASON_AP_TSF_RESET:       return "AP tsf reset";
        case WIFI_REASON_ROAMING:            return "roaming";
        default:                             return "disconnected";
    }
}

/* ---------------------- Status timer ---------------------- */

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    /* Wi-Fi status icon on the clock tile. */
    if (g_clock_wifi_icon) {
        if (g_wifi_connected) {
            lv_obj_set_style_text_color(g_clock_wifi_icon,
                                         lv_color_make(0x80, 0xff, 0x80), 0);
        } else if (g_wifi_curr_ssid[0]) {
            /* trying or failed */
            lv_obj_set_style_text_color(g_clock_wifi_icon,
                                         lv_color_make(0xff, 0xa0, 0x40), 0);
        } else {
            lv_obj_set_style_text_color(g_clock_wifi_icon,
                                         lv_color_make(0x40, 0x40, 0x40), 0);
        }
    }
    /* BT icon: we don't have BT enabled yet, dim it. */
    if (g_clock_bt_icon) {
        lv_obj_set_style_text_color(g_clock_bt_icon,
                                     lv_color_make(0x40, 0x40, 0x40), 0);
    }
    /* Settings tile: live Wi-Fi status text + connect timeout/reason. */
    if (g_set_wifi_status) {
        if (g_wifi_connected) {
            lv_label_set_text_fmt(g_set_wifi_status, LV_SYMBOL_OK " %s",
                                  g_wifi_curr_ssid);
        } else if (g_wifi_curr_ssid[0]) {
            uint32_t elapsed = lv_tick_elaps(g_wifi_connect_started_ms);
            if (g_wifi_last_reason) {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      LV_SYMBOL_WARNING " %s: %s",
                                      g_wifi_curr_ssid,
                                      wifi_reason_str(g_wifi_last_reason));
            } else if (elapsed > 15000) {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      LV_SYMBOL_WARNING " %s: timed out",
                                      g_wifi_curr_ssid);
            } else {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      tr(I18N_WIFI_CONNECTING_N),
                                      g_wifi_curr_ssid,
                                      (unsigned)(elapsed / 1000));
            }
        } else {
            lv_label_set_text(g_set_wifi_status, tr(I18N_WIFI_NOT_CONN));
        }
    }
}

/* ---------------------- Backlight + auto-dim ---------------------- */

static void activity_kick(lv_event_t *e)
{
    (void)e;
    g_last_activity_ms = lv_tick_get();
    if (g_dim_state != 0) {
        g_dim_state = 0;
        backlight_apply(g_cfg.brightness);
    }
}

static void dim_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t idle_ms = lv_tick_elaps(g_last_activity_ms);
    int want = 0;
    if (g_cfg.off_s > 0 && idle_ms >= (uint32_t)g_cfg.off_s * 1000) {
        want = 2;
    } else if (g_cfg.dim_s > 0 && idle_ms >= (uint32_t)g_cfg.dim_s * 1000) {
        want = 1;
    }
    if (want != g_dim_state) {
        g_dim_state = want;
        if (want == 0) backlight_apply(g_cfg.brightness);
        else if (want == 1) backlight_apply(g_cfg.brightness / 8 + 4);
        else backlight_apply(0);
    }
}

/* ---------------------- FPS timer ---------------------- */

void fps_timer_cb(lv_timer_t *t)
{
    (void)t;
    static uint32_t last_tick = 0;
    uint32_t now = lv_tick_get();
    uint32_t dt  = now - last_tick;
    if (dt == 0) return;
    uint32_t frames = fps_frame_count;
    fps_frame_count = 0;
    last_tick = now;
    uint32_t fps_x10 = (frames * 10000U) / dt;
    if (fps_label) {
        lv_label_set_text_fmt(fps_label, "FPS %lu.%lu",
                              (unsigned long)(fps_x10 / 10),
                              (unsigned long)(fps_x10 % 10));
    }
    static uint32_t print_div = 0;
    if ((print_div++ & 3) == 0) {  /* ~every 2s */
        ESP_LOGI(TAG, "fps=%lu.%lu  (frames=%lu in %lu ms)",
                 (unsigned long)(fps_x10 / 10), (unsigned long)(fps_x10 % 10),
                 (unsigned long)frames, (unsigned long)dt);
    }
}

/* ---------------------- Hello tile (legacy demo) ---------------------- */

static void play_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (!g_cfg.audio_enable) {
        ESP_LOGI(TAG, "play ignored: audio disabled in settings");
        return;
    }
    bool now = !audio_min_is_playing();
    audio_min_play_midi(now);
    if (play_btn_label) {
        lv_label_set_text(play_btn_label, now ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }
    ESP_LOGI(TAG, "play toggled -> %s", now ? "PLAY" : "STOP");
}

/* Forward declaration: build_main_ui is defined below but called from
   rotate_btn_event_cb (which runs inside the LVGL task, so the lock
   is already held and we must bypass show_main_ui's lock attempt). */
static void build_main_ui(const char *status_text);

static void rotate_btn_event_cb(lv_event_t *e)
{
    (void)e;
    /* Cycle 0 -> 90 -> 180 -> 270 -> 0. The canvas dimensions swap
       between portrait (172x640) and landscape (640x172) so the UI
       fills the panel at every rotation. The framebuffer is reused
       (172*640 == 640*172 pixels) so no realloc is needed. */
    rot_state = (rot_state + 1) & 3;
    if (rot_state == 0 || rot_state == 2) {
        canvas_w = 172;   /* EXAMPLE_LCD_H_RES */
        canvas_h = 640;   /* EXAMPLE_LCD_V_RES */
    } else {
        canvas_w = 640;   /* UI_CANVAS_W */
        canvas_h = 172;   /* UI_CANVAS_H */
    }
    /* g_disp_drv update is handled by main.cpp's disp_driver code
       via the extern g_disp_drv pointer. For now we call a hook. */
    extern void disp_driver_update_resolution(void);
    disp_driver_update_resolution();
    /* Wipe the active screen and rebuild widgets sized for the new canvas. */
    lv_obj_clean(lv_scr_act());
    fps_label = NULL;
    play_btn_label = NULL;
    g_tileview = NULL;
    /* Reset clock tile pointers. */
    g_clock_time_label = NULL;
    g_clock_ms_label = NULL;
    g_clock_date_label = NULL;
    g_clock_tz_label = NULL;
    g_clock_wifi_icon = NULL;
    g_clock_bt_icon = NULL;
    g_sunmap_canvas = NULL;
    /* Reset settings tile pointers. */
    g_set_wifi_status = NULL;
    g_set_wifi_list   = NULL;
    g_set_kb_overlay  = NULL;
    g_set_kb_ta       = NULL;
    /* Reset quotes tile pointers. */
    g_quotes_name_l = NULL; g_quotes_name_r = NULL;
    g_quotes_sym_l_lbl = NULL; g_quotes_sym_r_lbl = NULL;
    g_quotes_price_l = NULL; g_quotes_price_r = NULL;
    g_quotes_chg_l = NULL; g_quotes_chg_r = NULL;
    g_quotes_status = NULL;
    g_quotes_wifi_icon = NULL; g_quotes_bt_icon = NULL;
    g_quotes_clock_lbl = NULL;
    /* Reset radio tile pointers. */
    g_radio_status_lbl = NULL; g_radio_now_lbl = NULL;
    g_radio_btn_lbl = NULL; g_radio_list = NULL; g_radio_vol_lbl = NULL;
    /* Reset recorder tile pointers. */
    g_rec_status = NULL; g_rec_btn_lbl = NULL;
    g_rec_vu_l = NULL; g_rec_vu_r = NULL;
    g_rec_list_overlay = NULL; g_rec_list = NULL; g_rec_overlay_status = NULL;
    /* Delete timers that reference tile widgets. */
    if (g_clock_timer)    { lv_timer_del(g_clock_timer);    g_clock_timer    = NULL; }
    if (g_clock_ms_timer) { lv_timer_del(g_clock_ms_timer); g_clock_ms_timer = NULL; }
    if (g_sunmap_timer)   { lv_timer_del(g_sunmap_timer);   g_sunmap_timer   = NULL; }
    if (g_dim_timer)      { lv_timer_del(g_dim_timer);      g_dim_timer      = NULL; }
    if (g_status_timer)   { lv_timer_del(g_status_timer);   g_status_timer   = NULL; }
    if (g_radio_poll_timer) { lv_timer_del(g_radio_poll_timer); g_radio_poll_timer = NULL; }
    if (g_rec_poll) { lv_timer_del(g_rec_poll); g_rec_poll = NULL; }
    /* We're already inside the LVGL task (event callback), so the lock
       is already held.  Call build_main_ui directly instead of
       show_main_ui, which would try to re-acquire the mutex and deadlock. */
    build_main_ui(g_status_text);
    ESP_LOGI(TAG, "rotate -> %d deg  canvas=%dx%d", rot_state * 90, canvas_w, canvas_h);
}

static void build_hello_tile(lv_obj_t *parent, const char *status_text)
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

    /* Play / Stop button */
    lv_obj_t *play_btn = lv_btn_create(parent);
    lv_obj_set_size(play_btn, 50, 50);
    lv_obj_align(play_btn, LV_ALIGN_BOTTOM_LEFT, 16, -8);
    lv_obj_set_style_radius(play_btn, 25, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_make(0x20, 0x80, 0x40), 0);
    lv_obj_add_event_cb(play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
    play_btn_label = lv_label_create(play_btn);
    lv_label_set_text(play_btn_label, audio_min_is_playing() ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_btn_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(play_btn_label, i18n_font(), 0);
    lv_obj_center(play_btn_label);

    /* Rotate button */
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

/* ---------------------- Tileview gesture handlers ---------------------- */

/* Wrap-around: swipe left on last tile -> jump to tile 0; swipe right on
   tile 0 -> jump to last tile.  LVGL tileview doesn't natively wrap. */
static void tileview_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    lv_obj_t *tv  = lv_event_get_target(e);
    lv_coord_t x = lv_obj_get_scroll_x(tv);
    lv_coord_t w = lv_obj_get_width(tv);
    int idx = (w > 0) ? (x + w / 2) / w : 0;
    const int last = N_TILES - 1;
    if (dir == LV_DIR_LEFT && idx == last) {
        lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
    } else if (dir == LV_DIR_RIGHT && idx == 0) {
        lv_obj_set_tile_id(tv, last, 0, LV_ANIM_OFF);
    }
}

/* iOS-style commit threshold: hold the tileview at its current snap
   position until the X drift exceeds 50 px. */
static void tileview_commit_cb(lv_event_t *e)
{
    static lv_coord_t s_press_x = 0;
    static lv_coord_t s_locked_x = 0;
    static bool       s_committed = false;
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
        if (dx > 50 || dx < -50) {
            s_committed = true;
        } else {
            lv_obj_scroll_to_x(tv, s_locked_x, LV_ANIM_OFF);
        }
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        s_committed = false;
    }
}

/* Scroll-then-click suppression: stamp the time on SCROLL events. */
static void screen_scroll_stamp_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_SCROLL) {
        g_last_scroll_ms = lv_tick_get();
    }
}

/* ---------------------- Top-level UI builder ---------------------- */

static void build_main_ui(const char *status_text)
{
    static bool fps_timer_created = false;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    g_tileview = lv_tileview_create(scr);
    lv_obj_set_size(g_tileview, canvas_w, canvas_h);
    lv_obj_set_style_bg_color(g_tileview, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_tileview, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(g_tileview, LV_SCROLLBAR_MODE_OFF);

    /* N_TILES loop: Clock <-> Quotes <-> Settings <-> Radio <-> Recorder <-> Hello <-> Clock.
       LVGL tileview doesn't natively wrap; the wrap-around between tile 0
       and the last tile is handled by the gesture cb installed below. */
    lv_obj_t *t_clock  = lv_tileview_add_tile(g_tileview, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_quotes = lv_tileview_add_tile(g_tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_set    = lv_tileview_add_tile(g_tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_radio  = lv_tileview_add_tile(g_tileview, 3, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_record = lv_tileview_add_tile(g_tileview, 4, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_hello  = lv_tileview_add_tile(g_tileview, 5, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    (void)status_text;

    build_clock_tile(t_clock);
    build_quotes_tile(t_quotes);
    build_settings_tile(t_set);
    build_radio_tile(t_radio);
    build_recorder_tile(t_record);
    build_hello_tile(t_hello, g_status_text);

    /* Wrap-around gesture: swipe left on last tile -> tile 0, etc. */
    lv_obj_add_event_cb(g_tileview, tileview_gesture_cb, LV_EVENT_GESTURE, NULL);

    /* iOS-style commit threshold: small drift snaps back, >50px commits. */
    lv_obj_add_event_cb(g_tileview, tileview_commit_cb, LV_EVENT_ALL, NULL);

    /* FPS overlay: parented to the screen (not the tileview) so it
       floats above every tile. */
    fps_label = lv_label_create(scr);
    lv_label_set_text(fps_label, "FPS --");
    lv_obj_set_style_text_color(fps_label, lv_color_make(0x00, 0xff, 0x80), 0);
    lv_obj_set_style_text_font(fps_label, i18n_font(), 0);
    lv_obj_set_style_bg_color(fps_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fps_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(fps_label, 3, 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_clear_flag(fps_label, LV_OBJ_FLAG_CLICKABLE);
    if (!g_cfg.show_fps) lv_obj_add_flag(fps_label, LV_OBJ_FLAG_HIDDEN);

    if (!fps_timer_created) {
        lv_timer_create(fps_timer_cb, 3000, NULL);
        fps_timer_created = true;
    }

    /* Wi-Fi/BT status icons + settings page status text -- 1 Hz. */
    if (!g_status_timer) {
        g_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    }

    /* Activity wake: any touch on the screen kicks the dim timer. */
    lv_obj_add_event_cb(scr, activity_kick, LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(scr, activity_kick, LV_EVENT_RELEASED, NULL);

    /* iOS/Android-style scroll-then-click suppression. Any scroll on any
       descendant bubbles SCROLL_BEGIN / SCROLL / SCROLL_END up to the
       screen. We stamp the time and consult it from menu_input_blocked()
       and the action callbacks so a click that lands within 250 ms of a
       scroll motion is ignored. */
    lv_obj_add_event_cb(scr, screen_scroll_stamp_cb, LV_EVENT_ALL, NULL);
    g_last_activity_ms = lv_tick_get();
    if (!g_dim_timer) {
        g_dim_timer = lv_timer_create(dim_timer_cb, 1000, NULL);
    }

    /* Start on the clock. */
    lv_obj_set_tile_id(g_tileview, 0, 0, LV_ANIM_OFF);
}

void show_main_ui(const char *status_text)
{
    /* Cache for redraw on rotation. */
    if (status_text != g_status_text) {
        strncpy(g_status_text, status_text, sizeof(g_status_text) - 1);
        g_status_text[sizeof(g_status_text) - 1] = '\0';
    }
    wifi_manager_register_status_cb(wifi_status_cb);
    if (!lvgl_lock(-1)) return;
    build_main_ui(g_status_text);
    lvgl_unlock();
}
