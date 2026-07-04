#include "ui_settings.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_fat.h"
#include <sys/stat.h>   /* for mkdir() */
#include "lvgl.h"
#include "i18n.h"
#include "tz_cities.h"
#include "user_config.h"
#include "sdcard_bsp.h"
#include "recorder.h"
#include "radio.h"
#include "ui_recorder.h"
#include "audio_min.h"
#include "lcd_bl_pwm_bsp.h"
#include "esp_wifi.h"
#include "ui_clock.h"

static const char *TAG = "ui_settings";

/* Forward declaration of the local wifi-list renderer (defined below). */
static void set_render_wifi_list(void);

/* Block touch-driven menu actions for a short window after a back press,
   so the same gesture that pops a page doesn't also pick the item that
   slides in under the finger. Stamped by the back-button click handler
   below; tested by every action callback that mutates state. */
uint32_t g_menu_input_block_until_ms = 0;
uint32_t g_last_scroll_ms = 0;
#define MENU_BACK_DEBOUNCE_MS  350
#define SCROLL_CLICK_SUPPRESS_MS 250

/* Settings tile widgets (rebuilt on rotate). */
lv_obj_t  *g_set_wifi_status = NULL;
lv_obj_t  *g_set_wifi_list   = NULL;
static int g_set_wifi_sel    = -1;  /* index into g_wifi_scan or -1 */
lv_obj_t  *g_set_kb_overlay  = NULL;
lv_obj_t  *g_set_kb_ta       = NULL;
static char g_set_kb_ssid[33] = {0};

/* Transparent shield placed above the menu for MENU_BACK_DEBOUNCE_MS
   after a back-press. It eats every click that would otherwise reach
   the menu item that scrolled in under the finger. */
lv_obj_t *g_menu_shield = NULL;

/* Storage page widgets we update after a format finishes. */
static lv_obj_t *g_storage_info_lbl = NULL;
static lv_obj_t *g_storage_btn_lbl  = NULL;

/* ---------------------- Settings tile ---------------------- */

/* Keyboard overlay for password entry. */
static void kb_close(void)
{
    if (g_set_kb_overlay) {
        lv_obj_del_async(g_set_kb_overlay);  /* deferred to avoid use-
                                                after-free inside the
                                                lv_keyboard event chain */
        g_set_kb_overlay = NULL;
        g_set_kb_ta      = NULL;
    }
    if (g_clock_ms_timer) lv_timer_resume(g_clock_ms_timer);
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    if (code == LV_EVENT_READY) {
        /* Read the password BEFORE deleting the textarea. We snapshot
           the strings here because lv_keyboard_def_event_cb will keep
           dereferencing keyboard->ta after we return; we use
           lv_obj_del_async so the actual deletion happens after that
           function unwinds. */
        const char *pass = g_set_kb_ta ? lv_textarea_get_text(g_set_kb_ta) : "";
        char pass_copy[65] = {0};
        if (pass) {
            size_t pass_len = strlen(pass);
            if (pass_len >= sizeof(pass_copy)) pass_len = sizeof(pass_copy) - 1;
            memcpy(pass_copy, pass, pass_len);
            pass_copy[pass_len] = '\0';
        }
        char ssid[33] = {0};
        size_t ssid_len = strlen(g_set_kb_ssid);
        if (ssid_len >= sizeof(ssid)) ssid_len = sizeof(ssid) - 1;
        memcpy(ssid, g_set_kb_ssid, ssid_len);
        ssid[ssid_len] = '\0';
        ESP_LOGI(TAG, "kb: connect ssid=%s pass_len=%u", ssid,
                 (unsigned)strlen(pass_copy));
        app_cfg_save_ssid_pass(ssid, pass_copy);
        /* Promote this SSID to "last_ssid" so auto-connect picks it up on
           the next boot. Without this the password gets stored but the
           auto-connect path logs "no credentials yet". */
        size_t last_ssid_len = strlen(ssid);
        if (last_ssid_len >= sizeof(g_cfg.last_ssid)) last_ssid_len = sizeof(g_cfg.last_ssid) - 1;
        memcpy(g_cfg.last_ssid, ssid, last_ssid_len);
        g_cfg.last_ssid[last_ssid_len] = '\0';
        app_cfg_save();
        wifi_connect(ssid, pass_copy);
        if (g_set_wifi_status) lv_label_set_text_fmt(g_set_wifi_status, tr(I18N_WIFI_CONNECTING), ssid);
        kb_close();
    } else if (code == LV_EVENT_CANCEL) {
        kb_close();
    } else {
        (void)kb;
    }
}

/* Mac-style 5-row keymap: digit row always visible, QWERTY, ASDF, ZXCV with
   punctuation, then a compact bottom row with a narrower space bar so we
   have room for "1#" (-> symbols mode), arrows, "_", "-" and OK.
   Magic strings ("ABC", "abc", "1#") are recognised by lv_keyboard for
   mode toggles -- everything else types literally.
   Width units sum per row; LVGL renders each button proportionally. */
static const char *kKbMapLower[] = {
    /* row 1: 10 digits + backspace(2)  -> 12 */
    "1","2","3","4","5","6","7","8","9","0", LV_SYMBOL_BACKSPACE, "\n",
    /* row 2: 10 letters                 -> 10 */
    "q","w","e","r","t","y","u","i","o","p", "\n",
    /* row 3: 9 letters                  -> 9 */
    "a","s","d","f","g","h","j","k","l", "\n",
    /* row 4: shift(1.5) + 7 letters + , + .  -> 11.5 */
    "ABC", "z","x","c","v","b","n","m",",",".", "\n",
    /* row 5: 1#(2) + @ + . + left + space(4) + right + _ + - + OK(2) -> 13 */
    "1#", "@",".", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, "_","-", LV_SYMBOL_OK, ""
};
/* Widths (per button) for kKbMapLower. CTRL_BTN_FLAGS marks a control key
   that isn't inserted as text. CLICK_TRIG | NO_REPEAT on character keys
   makes them fire on release only and disables auto-repeat -- prevents
   "caaaattt" from a slightly-too-long press or a jittery touch. Backspace
   keeps default behaviour so hold-to-delete still works. */
#define KB_CHAR  (LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 1)
#define KB_CTRL2 (LV_KEYBOARD_CTRL_BTN_FLAGS | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 2)
#define KB_CTRL1 (LV_KEYBOARD_CTRL_BTN_FLAGS | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 1)
static const lv_btnmatrix_ctrl_t kKbCtrlLower[] = {
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,  /* backspace: auto-repeat allowed */
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    LV_BTNMATRIX_CTRL_CHECKED | KB_CTRL2,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    KB_CTRL2, KB_CHAR, KB_CHAR,
    KB_CTRL1, 4 | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT, KB_CTRL1,
    KB_CHAR, KB_CHAR, KB_CTRL2,
};
static const char *kKbMapUpper[] = {
    "1","2","3","4","5","6","7","8","9","0", LV_SYMBOL_BACKSPACE, "\n",
    "Q","W","E","R","T","Y","U","I","O","P", "\n",
    "A","S","D","F","G","H","J","K","L", "\n",
    "abc", "Z","X","C","V","B","N","M",",",".", "\n",
    "1#", "@",".", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, "_","-", LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t kKbCtrlUpper[] = {
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    LV_BTNMATRIX_CTRL_CHECKED | KB_CTRL2,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    KB_CTRL2, KB_CHAR, KB_CHAR,
    KB_CTRL1, 4 | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT, KB_CTRL1,
    KB_CHAR, KB_CHAR, KB_CTRL2,
};

/* Eye-toggle callback: tap to show/hide the password text. Was a C++
   lambda inside kb_open_for_ssid; lifted to a named static function so
   the file compiles as C. */
static void kb_eye_toggle_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (!g_set_kb_ta) return;
    bool was_pw = lv_textarea_get_password_mode(g_set_kb_ta);
    lv_textarea_set_password_mode(g_set_kb_ta, !was_pw);
    if (lbl) lv_label_set_text(lbl, was_pw ? LV_SYMBOL_EYE_OPEN
                                           : LV_SYMBOL_EYE_CLOSE);
}

static void kb_open_for_ssid(const char *ssid)
{
    strncpy(g_set_kb_ssid, ssid, sizeof(g_set_kb_ssid) - 1);
    g_set_kb_ssid[sizeof(g_set_kb_ssid) - 1] = 0;

    /* Pause the 60 Hz ms-clock while typing -- otherwise every
       keystroke contends with the ms label invalidation and the
       extra compositor passes can blow the LVGL task stack. */
    if (g_clock_ms_timer) lv_timer_pause(g_clock_ms_timer);

    lv_obj_t *scr = lv_scr_act();
    g_set_kb_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(g_set_kb_overlay);
    lv_obj_set_size(g_set_kb_overlay, canvas_w, canvas_h);
    lv_obj_set_style_bg_color(g_set_kb_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_set_kb_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(g_set_kb_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Single-line text area at the top + eye toggle on the right edge.
       Pad-tight so the keyboard below gets the rest of the canvas. */
    const int TA_H  = 20;
    const int EYE_W = 28;
    g_set_kb_ta = lv_textarea_create(g_set_kb_overlay);
    lv_textarea_set_one_line(g_set_kb_ta, true);
    lv_textarea_set_password_mode(g_set_kb_ta, true);
    char ph[64];
    snprintf(ph, sizeof(ph), tr(I18N_WIFI_PASS_FOR), ssid);
    lv_textarea_set_placeholder_text(g_set_kb_ta, ph);
    lv_obj_set_size(g_set_kb_ta, canvas_w - EYE_W, TA_H);
    lv_obj_align(g_set_kb_ta, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(g_set_kb_ta, i18n_font(), 0);
    lv_obj_set_style_pad_top(g_set_kb_ta, 1, 0);
    lv_obj_set_style_pad_bottom(g_set_kb_ta, 1, 0);
    lv_obj_set_style_pad_left(g_set_kb_ta, 6, 0);
    lv_obj_set_style_pad_right(g_set_kb_ta, 6, 0);
    lv_obj_set_style_border_width(g_set_kb_ta, 0, 0);
    lv_obj_set_style_radius(g_set_kb_ta, 0, 0);

    /* Eye toggle: tap to show/hide the password text. Defaults to hidden
       (eye-closed glyph), since the textarea boots in password mode. */
    lv_obj_t *eye = lv_btn_create(g_set_kb_overlay);
    lv_obj_set_size(eye, EYE_W, TA_H);
    lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(eye, 0, 0);
    lv_obj_set_style_pad_all(eye, 0, 0);
    lv_obj_set_style_bg_color(eye, lv_color_make(0x30, 0x30, 0x40), 0);
    lv_obj_t *eye_lbl = lv_label_create(eye);
    lv_label_set_text(eye_lbl, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(eye_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(eye_lbl, i18n_font(), 0);
    lv_obj_center(eye_lbl);
    lv_obj_add_event_cb(eye, kb_eye_toggle_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(g_set_kb_overlay);
    lv_obj_set_width(kb, canvas_w);
    lv_obj_set_height(kb, canvas_h - TA_H);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    /* Tight padding inside the keyboard so each row gets more vertical
       space; gives ~30 px tall keys on the 172 px canvas. */
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_row(kb, 2, 0);
    lv_obj_set_style_pad_column(kb, 2, 0);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER,
                        (const char **)kKbMapLower, kKbCtrlLower);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER,
                        (const char **)kKbMapUpper, kKbCtrlUpper);
    lv_keyboard_set_textarea(kb, g_set_kb_ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
}

/* Tap-to-select: tapping an AP row just highlights it. The right-side
   Connect button uses g_set_wifi_sel to drive the actual association. */
static void wifi_ap_clicked_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)g_wifi_scan_n) return;
    g_set_wifi_sel = idx;
    set_render_wifi_list();
}

/* Connect the currently selected AP. Open APs go straight; saved-password
   APs reuse the stored password; unknown-password APs open the keyboard. */
static void wifi_connect_selected_cb(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    int idx = g_set_wifi_sel;
    if (idx < 0 || idx >= (int)g_wifi_scan_n) return;
    const wifi_scan_ap_t *ap = &g_wifi_scan[idx];
    if (ap->auth == 0) {
        app_cfg_save_ssid_pass(ap->ssid, "");
        strncpy(g_cfg.last_ssid, ap->ssid, sizeof(g_cfg.last_ssid) - 1);
        g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
        app_cfg_save();
        wifi_connect(ap->ssid, "");
        if (g_set_wifi_status) lv_label_set_text_fmt(g_set_wifi_status, tr(I18N_WIFI_CONNECTING), ap->ssid);
        return;
    }
    char pass[65] = {0};
    if (app_cfg_get_ssid_pass(ap->ssid, pass, sizeof(pass))) {
        strncpy(g_cfg.last_ssid, ap->ssid, sizeof(g_cfg.last_ssid) - 1);
        g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
        app_cfg_save();
        wifi_connect(ap->ssid, pass);
        if (g_set_wifi_status) lv_label_set_text_fmt(g_set_wifi_status, tr(I18N_WIFI_CONNECTING), ap->ssid);
        return;
    }
    kb_open_for_ssid(ap->ssid);
}

/* Forget the selected AP's saved password and clear last_ssid if it
   matches. Keeps the AP visible in the list. */
static void wifi_forget_selected_cb(lv_event_t *e)
{
    (void)e;
    int idx = g_set_wifi_sel;
    if (idx < 0 || idx >= (int)g_wifi_scan_n) return;
    const wifi_scan_ap_t *ap = &g_wifi_scan[idx];
    /* nvs_erase_key on the per-SSID record + the auto-connect ssid if
       it points here. */
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        char key[16] = {0};
        strncpy(key, ap->ssid, sizeof(key) - 1);
        nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }
    if (strncmp(ap->ssid, g_cfg.last_ssid, sizeof(g_cfg.last_ssid)) == 0) {
        g_cfg.last_ssid[0] = 0;
        app_cfg_save();
        esp_wifi_disconnect();
        g_wifi_connected = false;
    }
    set_render_wifi_list();
    if (g_set_wifi_status) lv_label_set_text(g_set_wifi_status, tr(I18N_WIFI_NOT_CONN));
}

static void set_render_wifi_list(void)
{
    if (!g_set_wifi_list) return;
    lv_obj_clean(g_set_wifi_list);
    if (g_wifi_scan_n == 0) {
        lv_obj_t *empty = lv_label_create(g_set_wifi_list);
        lv_label_set_text(empty, tr(I18N_WIFI_NO_APS));
        lv_obj_set_style_text_color(empty, lv_color_make(0xa0, 0xa0, 0xa0), 0);
        lv_obj_set_style_text_font(empty, i18n_font(), 0);
        return;
    }
    for (int i = 0; i < g_wifi_scan_n; i++) {
        const wifi_scan_ap_t *ap = &g_wifi_scan[i];
        lv_obj_t *btn = lv_btn_create(g_set_wifi_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 22);
        bool is_connected = g_wifi_connected &&
                            strncmp(ap->ssid, g_wifi_curr_ssid,
                                    sizeof(g_wifi_curr_ssid)) == 0;
        bool is_selected  = (i == g_set_wifi_sel);
        char dummy_pass[2];
        bool is_saved = app_cfg_get_ssid_pass(ap->ssid, dummy_pass, sizeof(dummy_pass));
        lv_obj_set_style_bg_color(btn,
            is_selected ? lv_color_make(0x40, 0x40, 0x60)
                        : lv_color_make(0x20, 0x20, 0x30), 0);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_add_event_cb(btn, wifi_ap_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(btn);
        /* Prefix glyphs: ✓ if currently connected; otherwise * if we have
           a saved password but aren't connected. Lock indicates encryption. */
        const char *prefix = "";
        if (is_connected)      prefix = LV_SYMBOL_OK " ";
        else if (is_saved)     prefix = "* ";
        lv_label_set_text_fmt(l, "%s%s%s  %ddB",
                              prefix,
                              ap->auth == 0 ? "" : LV_SYMBOL_KEYBOARD " ",
                              ap->ssid[0] ? ap->ssid : "(hidden)",
                              ap->rssi);
        lv_color_t col = lv_color_white();
        if (is_connected) col = lv_color_make(0x40, 0xc0, 0x80);
        else if (is_saved) col = lv_color_make(0xa0, 0xc0, 0xff);
        lv_obj_set_style_text_color(l, col, 0);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

/* One-shot UI refresh ~3s after a scan kicks off. Was a C++ lambda inside
   scan_btn_cb; lifted to a named static function so the file compiles as C. */
static void scan_refresh_cb(lv_timer_t *tt)
{
    if (g_set_wifi_status) {
        lv_label_set_text_fmt(g_set_wifi_status, "Found %u networks", (unsigned)g_wifi_scan_n);
    }
    set_render_wifi_list();
    lv_timer_del(tt);
}

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_set_wifi_status) lv_label_set_text(g_set_wifi_status, tr(I18N_WIFI_SCANNING));
    wifi_start_scan();
    /* Schedule a one-shot UI refresh ~3s later when scan_done has fired. */
    lv_timer_t *t = lv_timer_create(scan_refresh_cb, 3000, NULL);
    (void)t;
}

bool menu_input_blocked(void)
{
    uint32_t now = lv_tick_get();
    /* Block if we're inside the back-press debounce window. */
    if ((int32_t)(g_menu_input_block_until_ms - now) > 0) return true;
    /* iOS/Android-style: also block if the last scroll motion was within
       SCROLL_CLICK_SUPPRESS_MS. Stops a fling-then-release from firing a
       click on the row the finger happened to lift on. */
    if (g_last_scroll_ms != 0 &&
        lv_tick_elaps(g_last_scroll_ms) < SCROLL_CLICK_SUPPRESS_MS) {
        return true;
    }
    return false;
}

static void menu_shield_drop_cb(lv_timer_t *t)
{
    if (g_menu_shield) {
        lv_obj_del(g_menu_shield);
        g_menu_shield = NULL;
    }
    lv_timer_del(t);
}

static void menu_back_clicked_cb(lv_event_t *e)
{
    (void)e;
    g_menu_input_block_until_ms = lv_tick_get() + MENU_BACK_DEBOUNCE_MS;
    if (g_menu_shield) return;  /* already shielded */
    /* Parent to the active screen so the shield covers the menu and any
       header/back-button that might still be sitting under the finger. */
    g_menu_shield = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(g_menu_shield);
    lv_obj_set_size(g_menu_shield, canvas_w, canvas_h);
    lv_obj_align(g_menu_shield, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(g_menu_shield, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(g_menu_shield, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_menu_shield, LV_OBJ_FLAG_CLICKABLE);
    /* Move to top z-order so subsequent clicks land on the shield. */
    lv_obj_move_foreground(g_menu_shield);
    lv_timer_t *t = lv_timer_create(menu_shield_drop_cb, MENU_BACK_DEBOUNCE_MS, NULL);
    (void)t;
}

static void tz_city_pick_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    /* user_data carries the city index packed into a void*. */
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= TZ_CITY_COUNT) return;
    g_cfg.tz_idx = (uint16_t)idx;
    tz_apply_current();
    app_cfg_save();
    if (g_clock_tz_label) lv_label_set_text(g_clock_tz_label, tz_current_city_name());
    /* Force an immediate clock-face refresh so the user sees the change. */
    clock_update_cb(NULL);
}

static void bri_slider_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_cfg.brightness = (uint8_t)v;
    if (g_dim_state == 0) backlight_apply(g_cfg.brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) app_cfg_save();
}

static void fmt_duration(char *buf, size_t buflen, uint32_t total_s)
{
    if (total_s == 0)              snprintf(buf, buflen, "%s", tr(I18N_NEVER));
    else if (total_s < 60)         snprintf(buf, buflen, "%us", (unsigned)total_s);
    else if (total_s < 3600) {
        unsigned m = total_s / 60, s = total_s % 60;
        if (s) snprintf(buf, buflen, "%um %us", m, s);
        else   snprintf(buf, buflen, "%um", m);
    } else {
        unsigned h = total_s / 3600, m = (total_s % 3600) / 60;
        if (m) snprintf(buf, buflen, "%uh %um", h, m);
        else   snprintf(buf, buflen, "%uh", h);
    }
}

#define IDLE_SLIDER_MAX (8 * 3600)

static void dim_s_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > IDLE_SLIDER_MAX) v = IDLE_SLIDER_MAX;
    g_cfg.dim_s = (uint16_t)v;
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (lbl) {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.dim_s);
        if (g_cfg.dim_s == 0) lv_label_set_text(lbl, tr(I18N_DIM_NEVER));
        else                  lv_label_set_text_fmt(lbl, tr(I18N_DIM_AFTER), d);
    }
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) app_cfg_save();
}

/* ---------- Display sub-page callbacks (12/24h, date fmt, secs/ms, FPS) ---------- */

static void hour_fmt_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.hour24 = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    app_cfg_save();
    clock_update_cb(NULL);
}

static void show_sec_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.show_seconds = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    app_cfg_save();
    clock_update_cb(NULL);
}

static void show_ms_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.show_ms = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    app_cfg_save();
}

static void show_fps_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.show_fps = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    app_cfg_save();
    if (fps_label) {
        if (g_cfg.show_fps) lv_obj_clear_flag(fps_label, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(fps_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void date_fmt_cb(lv_event_t *e)
{
    lv_obj_t *r = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(r);
    if (sel < 0) sel = 0;
    if (sel > 2) sel = 2;
    g_cfg.date_fmt = (uint8_t)sel;
    app_cfg_save();
    clock_update_cb(NULL);
}

/* ---------- Sound sub-page callbacks (enable + volume) ---------- */

static void audio_en_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.audio_enable = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    if (!g_cfg.audio_enable && audio_min_is_playing()) audio_min_play_midi(false);
    app_cfg_save();
}

static void audio_vol_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    g_cfg.audio_volume = (uint8_t)v;
    audio_min_set_volume(g_cfg.audio_volume);
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (lbl) lv_label_set_text_fmt(lbl, tr(I18N_VOLUME_PCT), (unsigned)v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) app_cfg_save();
}

/* ---------- Theme + Wi-Fi auto-connect ---------- */

static void theme_cb(lv_event_t *e)
{
    lv_obj_t *r = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(r);
    if (sel < 0) sel = 0;
    if (sel > 2) sel = 2;
    g_cfg.theme = (uint8_t)sel;
    app_cfg_save();
    /* Sunmap can be re-themed live; menu colors apply on next boot. */
    sunmap_redraw();
}

static void wifi_ac_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.wifi_autoconnect = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    app_cfg_save();
}

/* ---------- Reset to defaults ---------- */

static void reset_confirm_cb(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "settings reset, rebooting");
    esp_restart();
}

/* ---------- Auto-dim sliders ---------- */

static void off_s_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > IDLE_SLIDER_MAX) v = IDLE_SLIDER_MAX;
    g_cfg.off_s = (uint16_t)v;
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (lbl) {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.off_s);
        if (g_cfg.off_s == 0) lv_label_set_text(lbl, tr(I18N_SLEEP_NEVER));
        else                  lv_label_set_text_fmt(lbl, tr(I18N_SLEEP_AFTER), d);
    }
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) app_cfg_save();
}

/* Build a sub-page that the menu can navigate to. Returns the page so
   the caller can attach it via lv_menu_set_load_page_event. */

static lv_obj_t *build_subpage_wifi(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_WIFI));

    /* Outer cont laid out as a flex row: left column = AP list, right
       column = status + Scan / Connect / Forget / auto-connect. */
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cont, 4, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_set_height(cont, lv_pct(100));

    /* ---------- Left: AP list ---------- */
    g_set_wifi_list = lv_obj_create(cont);
    lv_obj_set_width(g_set_wifi_list, lv_pct(60));
    lv_obj_set_height(g_set_wifi_list, lv_pct(100));
    lv_obj_set_layout(g_set_wifi_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_set_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_set_wifi_list, 2, 0);
    lv_obj_set_style_pad_all(g_set_wifi_list, 2, 0);
    lv_obj_set_style_bg_color(g_set_wifi_list, lv_color_make(0x10, 0x10, 0x14), 0);
    lv_obj_set_scroll_dir(g_set_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_set_wifi_list, LV_SCROLLBAR_MODE_AUTO);
    set_render_wifi_list();

    /* ---------- Right: actions + status ---------- */
    lv_obj_t *side = lv_obj_create(cont);
    lv_obj_remove_style_all(side);
    lv_obj_set_width(side, lv_pct(38));
    lv_obj_set_height(side, lv_pct(100));
    lv_obj_set_layout(side, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 4, 0);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);

    g_set_wifi_status = lv_label_create(side);
    lv_label_set_long_mode(g_set_wifi_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_set_wifi_status, lv_pct(100));
    lv_label_set_text_fmt(g_set_wifi_status, "%s",
                          g_wifi_connected ? g_wifi_curr_ssid : tr(I18N_WIFI_NOT_CONN));
    lv_obj_set_style_text_color(g_set_wifi_status, lv_color_make(0xc0, 0xc0, 0xc0), 0);
    lv_obj_set_style_text_font(g_set_wifi_status, i18n_font(), 0);

    /* Scan button */
    lv_obj_t *scan_btn = lv_btn_create(side);
    lv_obj_set_size(scan_btn, lv_pct(100), 24);
    lv_obj_t *scan_l = lv_label_create(scan_btn);
    lv_label_set_text(scan_l, tr(I18N_WIFI_SCAN_BTN));
    lv_obj_set_style_text_font(scan_l, i18n_font(), 0);
    lv_obj_center(scan_l);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Connect button (acts on selected list row) */
    lv_obj_t *conn_btn = lv_btn_create(side);
    lv_obj_set_size(conn_btn, lv_pct(100), 24);
    lv_obj_set_style_bg_color(conn_btn, lv_color_make(0x20, 0x80, 0x40), 0);
    lv_obj_t *conn_l = lv_label_create(conn_btn);
    lv_label_set_text(conn_l, tr(I18N_WIFI_CONNECT_BTN));
    lv_obj_set_style_text_color(conn_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(conn_l, i18n_font(), 0);
    lv_obj_center(conn_l);
    lv_obj_add_event_cb(conn_btn, wifi_connect_selected_cb, LV_EVENT_CLICKED, NULL);

    /* Forget button */
    lv_obj_t *forget_btn = lv_btn_create(side);
    lv_obj_set_size(forget_btn, lv_pct(100), 24);
    lv_obj_set_style_bg_color(forget_btn, lv_color_make(0x80, 0x40, 0x20), 0);
    lv_obj_t *forget_l = lv_label_create(forget_btn);
    lv_label_set_text(forget_l, tr(I18N_WIFI_FORGET_BTN));
    lv_obj_set_style_text_color(forget_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(forget_l, i18n_font(), 0);
    lv_obj_center(forget_l);
    lv_obj_add_event_cb(forget_btn, wifi_forget_selected_cb, LV_EVENT_CLICKED, NULL);

    return page;
}

static lv_obj_t *build_subpage_tz_city_list(lv_obj_t *menu, uint16_t first, uint16_t last,
                                              const char *title)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)title);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    for (uint16_t i = first; i < last; i++) {
        lv_obj_t *cont = lv_menu_cont_create(page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text(l, k_tz_cities[i].name);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        if (i == g_cfg.tz_idx) {
            lv_obj_set_style_text_color(l, lv_color_make(0x40, 0xc0, 0x80), 0);
        }
        lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cont, tz_city_pick_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }
    return page;
}

static lv_obj_t *build_subpage_tz(lv_obj_t *menu)
{
    /* Build the per-continent city pages first, then a continent-list
       root page that links to them. Two-level navigation matches the
       OpenWRT-style "Continent / City" picker. */
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_TZ));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    for (uint16_t c = 0; c < TZ_CONTINENT_COUNT; c++) {
        lv_obj_t *city_page = build_subpage_tz_city_list(menu,
            k_tz_continents[c].first, k_tz_continents[c].last,
            k_tz_continents[c].name);
        lv_obj_t *cont = lv_menu_cont_create(page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text(l, k_tz_continents[c].name);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        lv_menu_set_load_page_event(menu, cont, city_page);
        lv_obj_add_event_cb(cont, menu_back_clicked_cb, LV_EVENT_CLICKED, NULL);
    }
    return page;
}

static lv_obj_t *build_subpage_brightness(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_BRIGHTNESS));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    lv_obj_t *l = lv_label_create(cont);
    lv_label_set_text(l, tr(I18N_BACKLIGHT_LEVEL));
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, i18n_font(), 0);

    lv_obj_t *bri_s = lv_slider_create(cont);
    lv_obj_set_width(bri_s, lv_pct(95));
    lv_slider_set_range(bri_s, 8, 255);
    lv_slider_set_value(bri_s, g_cfg.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(bri_s, bri_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(bri_s, bri_slider_cb, LV_EVENT_RELEASED,      NULL);
    lv_obj_clear_flag(bri_s, LV_OBJ_FLAG_GESTURE_BUBBLE);

    return page;
}

static lv_obj_t *build_subpage_autodim(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_AUTODIM));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    lv_obj_t *dim_lbl = lv_label_create(cont);
    {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.dim_s);
        if (g_cfg.dim_s == 0) lv_label_set_text(dim_lbl, tr(I18N_DIM_NEVER));
        else                  lv_label_set_text_fmt(dim_lbl, tr(I18N_DIM_AFTER), d);
    }
    lv_obj_set_style_text_color(dim_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(dim_lbl, i18n_font(), 0);
    lv_obj_t *dim_s = lv_slider_create(cont);
    lv_obj_set_width(dim_s, lv_pct(95));
    lv_slider_set_range(dim_s, 0, IDLE_SLIDER_MAX);
    lv_slider_set_value(dim_s, g_cfg.dim_s, LV_ANIM_OFF);
    /* Don't let horizontal slider drags bubble up and trigger a tileview
       page swipe. Same for the gesture flag. */
    lv_obj_clear_flag(dim_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(dim_s, dim_s_cb, LV_EVENT_VALUE_CHANGED, dim_lbl);
    lv_obj_add_event_cb(dim_s, dim_s_cb, LV_EVENT_RELEASED,      dim_lbl);

    lv_obj_t *off_lbl = lv_label_create(cont);
    {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.off_s);
        if (g_cfg.off_s == 0) lv_label_set_text(off_lbl, tr(I18N_SLEEP_NEVER));
        else                  lv_label_set_text_fmt(off_lbl, tr(I18N_SLEEP_AFTER), d);
    }
    lv_obj_set_style_text_color(off_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(off_lbl, i18n_font(), 0);
    lv_obj_t *off_s = lv_slider_create(cont);
    lv_obj_set_width(off_s, lv_pct(95));
    lv_slider_set_range(off_s, 0, IDLE_SLIDER_MAX);
    lv_slider_set_value(off_s, g_cfg.off_s, LV_ANIM_OFF);
    lv_obj_clear_flag(off_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(off_s, off_s_cb, LV_EVENT_VALUE_CHANGED, off_lbl);
    lv_obj_add_event_cb(off_s, off_s_cb, LV_EVENT_RELEASED,      off_lbl);

    return page;
}

/* Helper: a labelled toggle row (label on the left, switch on the right). */
static lv_obj_t *add_toggle_row(lv_obj_t *parent, const char *label,
                                bool checked, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, i18n_font(), 0);
    lv_obj_t *sw = lv_switch_create(row);
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return row;
}

static void lang_pick_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "lang_pick: %d", idx);
    i18n_set_lang(idx);
    /* Repaint every label in this language sub-page so the green tick
       moves to the newly-selected language immediately. */
    lv_obj_t *cont = lv_event_get_current_target(e);
    /* Climb to the menu_page (event came from the row, parent is the page). */
    lv_obj_t *page = cont ? lv_obj_get_parent(cont) : NULL;
    if (page) {
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(page); i++) {
            lv_obj_t *row = lv_obj_get_child(page, i);
            if (!row || lv_obj_get_child_cnt(row) == 0) continue;
            lv_obj_t *lbl = lv_obj_get_child(row, 0);
            if (!lbl) continue;
            lv_obj_set_style_text_color(lbl,
                ((int)i == idx) ? lv_color_make(0x40, 0xc0, 0x80)
                                : lv_color_white(), 0);
        }
    }
}

static lv_obj_t *build_subpage_language(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_LANGUAGE));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    int cur = i18n_lang();
    /* One row per supported language; tap to switch. */
    for (int i = 0; i < I18N_LANG_COUNT; i++) {
        lv_obj_t *cont = lv_menu_cont_create(page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text(l, k_i18n_lang_names[i]);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        if (i == cur) {
            lv_obj_set_style_text_color(l, lv_color_make(0x40, 0xc0, 0x80), 0);
        }
        lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cont, lang_pick_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    return page;
}

static lv_obj_t *build_subpage_display(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_DISPLAY));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    add_toggle_row(cont, tr(I18N_HOUR_24), g_cfg.hour24, hour_fmt_cb);
    add_toggle_row(cont, tr(I18N_SHOW_SECONDS), g_cfg.show_seconds, show_sec_cb);
    add_toggle_row(cont, tr(I18N_SHOW_MS), g_cfg.show_ms, show_ms_cb);
    add_toggle_row(cont, tr(I18N_SHOW_FPS), g_cfg.show_fps, show_fps_cb);

    lv_obj_t *df_l = lv_label_create(cont);
    lv_label_set_text(df_l, tr(I18N_DATE_FORMAT));
    lv_obj_set_style_text_font(df_l, i18n_font(), 0);
    lv_obj_t *df = lv_dropdown_create(cont);
    lv_dropdown_set_options_static(df,
        "YYYY.MM.DD\nDD.MM.YYYY\nMM.DD.YYYY");
    lv_dropdown_set_selected(df, g_cfg.date_fmt);
    lv_obj_add_event_cb(df, date_fmt_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *th_l = lv_label_create(cont);
    lv_label_set_text(th_l, tr(I18N_THEME));
    lv_obj_set_style_text_font(th_l, i18n_font(), 0);
    lv_obj_t *th = lv_dropdown_create(cont);
    /* Build a "Dark\nLight\nHigh contrast"-style options string in a static
       buffer so the dropdown can keep using it. Refreshed on each entry to
       this sub-page so it picks up the active language. */
    static char theme_opts[96];
    snprintf(theme_opts, sizeof(theme_opts), "%s\n%s\n%s",
             tr(I18N_THEME_DARK), tr(I18N_THEME_LIGHT), tr(I18N_THEME_HICONTRAST));
    lv_dropdown_set_options_static(th, theme_opts);
    lv_dropdown_set_selected(th, g_cfg.theme);
    lv_obj_add_event_cb(th, theme_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return page;
}

static lv_obj_t *build_subpage_sound(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_SOUND));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    add_toggle_row(cont, tr(I18N_SOUND_ENABLED), g_cfg.audio_enable, audio_en_cb);

    lv_obj_t *vol_lbl = lv_label_create(cont);
    lv_label_set_text_fmt(vol_lbl, tr(I18N_VOLUME_PCT), (unsigned)g_cfg.audio_volume);
    lv_obj_set_style_text_font(vol_lbl, i18n_font(), 0);

    lv_obj_t *vol_s = lv_slider_create(cont);
    lv_obj_set_width(vol_s, lv_pct(95));
    lv_slider_set_range(vol_s, 0, 100);
    lv_slider_set_value(vol_s, g_cfg.audio_volume, LV_ANIM_OFF);
    lv_obj_clear_flag(vol_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(vol_s, audio_vol_cb, LV_EVENT_VALUE_CHANGED, vol_lbl);
    lv_obj_add_event_cb(vol_s, audio_vol_cb, LV_EVENT_RELEASED,      vol_lbl);

    return page;
}

static lv_obj_t *build_subpage_reset(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_RESET));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    lv_obj_t *l = lv_label_create(cont);
    lv_label_set_text(l, tr(I18N_RESET_WARN));
    lv_obj_set_style_text_font(l, i18n_font(), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(95));

    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_height(btn, 32);
    lv_obj_set_width(btn, 160);
    lv_obj_set_style_bg_color(btn, lv_color_make(0xa0, 0x20, 0x20), 0);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, tr(I18N_RESET_BTN));
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl, i18n_font(), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, reset_confirm_cb, LV_EVENT_CLICKED, NULL);

    return page;
}

void storage_info_refresh(void)
{
    if (!g_storage_info_lbl) return;
    if (!sdcard_is_mounted()) {
        lv_label_set_text(g_storage_info_lbl, "SD: not mounted");
        return;
    }
    uint64_t total = 0, free = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &free) == ESP_OK && total > 0) {
        lv_label_set_text_fmt(g_storage_info_lbl, "SD: %llu MB free of %llu MB",
                              (unsigned long long)(free / (1024 * 1024)),
                              (unsigned long long)(total / (1024 * 1024)));
    } else {
        lv_label_set_text(g_storage_info_lbl, "SD: read err");
    }
}

void sd_format_worker(void *arg)
{
    (void)arg;
    /* Stop any ongoing recording first; the file handle would be invalid
       across an unmount/reformat. */
    if (recorder_is_recording()) recorder_stop();
    esp_err_t r = sdcard_format();
    ESP_LOGI(TAG, "sd format result: %s", esp_err_to_name(r));
    /* mkdir the recordings folder back; format leaves the root empty. */
    mkdir("/sdcard/recordings", 0775);
    /* LVGL touch: must run on the LVGL task. We borrow the safe
       cross-task path by setting flags and letting the LVGL timer pick
       them up. Simpler: just call from the worker since lv_label_set
       is OK if no-one is reading on the LVGL side simultaneously --
       but the cleanest is to set a "needs refresh" flag and let the
       recorder tile's poll handle it. We log here and let the next
       poll repaint. */
    storage_info_refresh();
    if (g_storage_btn_lbl) {
        lv_label_set_text(g_storage_btn_lbl,
                          r == ESP_OK ? "Format SD card"
                                      : "Format failed");
    }
    recorder_refresh_list();
    vTaskDelete(NULL);
}

static void sd_format_cb(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    if (g_storage_btn_lbl) lv_label_set_text(g_storage_btn_lbl, "Formatting...");
    /* Format on a worker so the LVGL task keeps drawing. 64GB FAT32
       format takes ~30-60s on this controller. */
    xTaskCreatePinnedToCore(sd_format_worker, "sd_fmt", 4 * 1024,
                            NULL, 4, NULL, 1);
}

static lv_obj_t *build_subpage_storage(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_STORAGE));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    g_storage_info_lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(g_storage_info_lbl, i18n_font(), 0);
    lv_obj_set_style_text_color(g_storage_info_lbl, lv_color_white(), 0);
    /* Don't call esp_vfs_fat_info at tile-build time -- on some cards it
       hangs the SDMMC driver for many seconds and wedges boot. Show a
       static placeholder; storage_info_refresh() is invoked from the
       sd_format_worker after a format completes. */
    lv_label_set_text(g_storage_info_lbl,
                      sdcard_is_mounted() ? "SD: mounted" : "SD: not mounted");

    /* Format button: full FAT32 reformat via esp_vfs_fat_sdcard_format.
       Wipes everything on the card. Runs on a worker task so the UI
       stays responsive (~30-60s for 64GB). */
    lv_obj_t *fmt_btn = lv_btn_create(cont);
    lv_obj_set_size(fmt_btn, 200, 32);
    lv_obj_set_style_bg_color(fmt_btn, lv_color_make(0xa0, 0x20, 0x20), 0);
    g_storage_btn_lbl = lv_label_create(fmt_btn);
    lv_label_set_text(g_storage_btn_lbl, "Format SD card");
    lv_obj_set_style_text_color(g_storage_btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_storage_btn_lbl, i18n_font(), 0);
    lv_obj_center(g_storage_btn_lbl);
    lv_obj_add_event_cb(fmt_btn, sd_format_cb, LV_EVENT_CLICKED, NULL);
    return page;
}

/* Wi-Fi sub-page is built earlier; add the auto-connect toggle there. */
static void wifi_subpage_add_autoconnect(lv_obj_t *page)
{
    /* page contents: menu_cont -> [list, side]. Toggle goes inside side. */
    lv_obj_t *menu_cont = lv_obj_get_child(page, 0);
    if (!menu_cont || lv_obj_get_child_cnt(menu_cont) < 2) return;
    lv_obj_t *side = lv_obj_get_child(menu_cont, 1);
    if (!side) return;
    add_toggle_row(side, tr(I18N_WIFI_AUTOCONNECT), g_cfg.wifi_autoconnect, wifi_ac_cb);
}

void build_settings_tile(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    theme_palette_t pal = theme_get();
    lv_obj_t *menu = lv_menu_create(parent);
    lv_obj_set_size(menu, lv_pct(100), lv_pct(100));
    /* Show a back button on sub-pages, none on the root list. */
    lv_menu_set_mode_header(menu, LV_MENU_HEADER_TOP_FIXED);
    lv_menu_set_mode_root_back_btn(menu, LV_MENU_ROOT_BACK_BTN_DISABLED);
    lv_obj_set_style_bg_color(menu, pal.menu_surf, 0);
    lv_obj_set_style_text_color(menu, pal.text, 0);
    /* Use the i18n font (CJK glyphs at 14 px with Latin fallback) so labels
       in zh/ja/ko render. Latin-only labels still pick the Montserrat
       fallback, so this is safe even in en mode. */
    lv_obj_set_style_text_font(menu, i18n_font(), 0);

    /* Header bar: title fills the middle, back button anchored right. */
    lv_obj_t *hdr = lv_menu_get_main_header(menu);
    if (hdr) {
        lv_obj_set_style_bg_color(hdr, pal.menu_hdr, 0);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(hdr, 4, 0);
        lv_obj_set_style_pad_gap(hdr, 6, 0);
        /* Children stay packed at the start; the title gets flex-grow so it
           expands to fill the middle and the back button rides at the end. */
        lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    lv_obj_t *title_lbl = lv_menu_get_main_header(menu)
                            ? lv_obj_get_child(lv_menu_get_main_header(menu), 1)
                            : NULL;
    if (title_lbl) {
        lv_obj_set_flex_grow(title_lbl, 1);
        lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(title_lbl, i18n_font(), 0);
    }
    lv_obj_t *back = lv_menu_get_main_header_back_btn(menu);
    if (back) {
        lv_obj_set_size(back, 60, 32);
        lv_obj_set_style_bg_color(back, pal.menu_btn, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(back, 4, 0);
        /* Stamp a debounce window so the same touch that pops the page
           doesn't also trigger the city/wifi/reset action it lands on. */
        lv_obj_add_event_cb(back, menu_back_clicked_cb, LV_EVENT_CLICKED, NULL);
        /* lv_menu already added an lv_img arrow as the first child; hide it
           so we don't render two stacked arrows. */
        if (lv_obj_get_child_cnt(back) > 0) {
            lv_obj_add_flag(lv_obj_get_child(back, 0), LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_t *txt = lv_label_create(back);
        lv_label_set_text_fmt(txt, LV_SYMBOL_LEFT " %s", tr(I18N_BACK));
        lv_obj_set_style_text_color(txt, lv_color_white(), 0);
        lv_obj_set_style_text_font(txt, i18n_font(), 0);
        lv_obj_center(txt);
    }

    /* Build sub-pages first; the main page links them. */
    lv_obj_t *p_wifi = build_subpage_wifi(menu);
    wifi_subpage_add_autoconnect(p_wifi);
    lv_obj_t *p_tz    = build_subpage_tz(menu);
    lv_obj_t *p_bri   = build_subpage_brightness(menu);
    lv_obj_t *p_dim   = build_subpage_autodim(menu);
    lv_obj_t *p_disp  = build_subpage_display(menu);
    lv_obj_t *p_snd   = build_subpage_sound(menu);
    lv_obj_t *p_lang  = build_subpage_language(menu);
    lv_obj_t *p_reset = build_subpage_reset(menu);
    lv_obj_t *p_storage = build_subpage_storage(menu);

    /* Main (root) page: list of menu items. Scrolls vertically if
       there are more entries than fit on the 172 px tall canvas. */
    lv_obj_t *main_page = lv_menu_page_create(menu, (char *)tr(I18N_MENU_TITLE));
    lv_obj_set_scroll_dir(main_page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(main_page, LV_SCROLLBAR_MODE_AUTO);
    struct { const char *icon; i18n_key_t key; lv_obj_t *page; } rows[] = {
        { LV_SYMBOL_WIFI,     I18N_SET_WIFI,       p_wifi  },
        { LV_SYMBOL_BELL,     I18N_SET_TZ,         p_tz    },
        { LV_SYMBOL_IMAGE,    I18N_SET_DISPLAY,    p_disp  },
        { LV_SYMBOL_AUDIO,    I18N_SET_SOUND,      p_snd   },
        { LV_SYMBOL_EYE_OPEN, I18N_SET_BRIGHTNESS, p_bri   },
        { LV_SYMBOL_POWER,    I18N_SET_AUTODIM,    p_dim   },
        { LV_SYMBOL_SD_CARD,  I18N_SET_STORAGE,    p_storage },
        { LV_SYMBOL_TRASH,    I18N_SET_RESET,      p_reset },
        { LV_SYMBOL_KEYBOARD, I18N_SET_LANGUAGE,   p_lang  },
    };
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        lv_obj_t *cont = lv_menu_cont_create(main_page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text_fmt(l, "%s  %s", rows[i].icon, tr(rows[i].key));
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        lv_menu_set_load_page_event(menu, cont, rows[i].page);
        /* Same input-shield trick as the back button: stamp the debounce
           when a row is clicked so the touch that loaded the new page
           can't also fire a click on whatever item lands under the
           finger on that new page. */
        lv_obj_add_event_cb(cont, menu_back_clicked_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_menu_set_page(menu, main_page);
}
