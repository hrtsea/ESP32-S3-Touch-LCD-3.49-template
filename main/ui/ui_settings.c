#include "ui_settings.h"
#include "ui_state.h"

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
#include "esp_wifi.h"
#include "ui_clock.h"
#include "ui_main.h"
#include "event_bus.h"

static const char *TAG = "ui_settings";

/* Forward declaration of the local wifi-list renderer (defined below). */
static void set_render_wifi_list(void);
static void init_styles(void);

/* ===== 1. 对象定义 ===== */
lv_obj_t *ui_Settings = NULL;

/* Settings tile widgets (rebuilt on rotate).
   widget 指针在 ui_settings.h 中 extern 声明，按 SquareLine 命名 */
lv_obj_t *ui_Settings_label_wifi_status = NULL;
lv_obj_t *ui_Settings_obj_wifi_list    = NULL;
lv_obj_t *ui_Settings_kb_overlay       = NULL;
lv_obj_t *ui_Settings_kb_textarea      = NULL;
lv_obj_t *ui_Settings_menu_shield      = NULL;

/* Storage page widgets we update after a format finishes. */
lv_obj_t *ui_Settings_label_storage_info = NULL;
lv_obj_t *ui_Settings_label_storage_btn  = NULL;

/* ===== 2. 静态样式变量 ===== */
/* 固定样式（不依赖 theme_palette）抽取为 lv_style_t；
   依赖 pal 的动态样式（menu/header/back_btn）保留行内设置。 */
static lv_style_t style_tile_bg;       /* 黑色背景 */
static lv_style_t style_wifi_list_bg;   /* 0x10,0x10,0x14 */
static lv_style_t style_wifi_status;   /* 0xc0,0xc0,0xc0 */
static lv_style_t style_connect_btn;   /* 0x20,0x80,0x40 */
static lv_style_t style_forget_btn;    /* 0x80,0x40,0x20 */
static lv_style_t style_format_btn;    /* 0xa0,0x20,0x20 */
static lv_style_t style_reset_btn;     /* 0xa0,0x20,0x20 */
static lv_style_t style_label_white;   /* 白色文字 */
static lv_style_t style_label_dim;    /* 0xa0,0xa0,0xa0 */
static lv_style_t style_submenu_cont; /* flex column + pad_row 6 */
static lv_style_t style_toggle_row;   /* flex row space-between */
static bool styles_inited = false;

static void init_styles(void)
{
    if (styles_inited) return;

    lv_style_init(&style_tile_bg);
    lv_style_set_bg_color(&style_tile_bg, lv_color_black());
    lv_style_set_bg_opa(&style_tile_bg, LV_OPA_COVER);
    lv_style_set_pad_all(&style_tile_bg, 0);

    lv_style_init(&style_wifi_list_bg);
    lv_style_set_bg_color(&style_wifi_list_bg, lv_color_make(0x10, 0x10, 0x14));
    lv_style_set_pad_row(&style_wifi_list_bg, 2);
    lv_style_set_pad_all(&style_wifi_list_bg, 2);

    lv_style_init(&style_wifi_status);
    lv_style_set_text_color(&style_wifi_status, lv_color_make(0xc0, 0xc0, 0xc0));

    lv_style_init(&style_connect_btn);
    lv_style_set_bg_color(&style_connect_btn, lv_color_make(0x20, 0x80, 0x40));

    lv_style_init(&style_forget_btn);
    lv_style_set_bg_color(&style_forget_btn, lv_color_make(0x80, 0x40, 0x20));

    lv_style_init(&style_format_btn);
    lv_style_set_bg_color(&style_format_btn, lv_color_make(0xa0, 0x20, 0x20));

    lv_style_init(&style_reset_btn);
    lv_style_set_bg_color(&style_reset_btn, lv_color_make(0xa0, 0x20, 0x20));

    lv_style_init(&style_label_white);
    lv_style_set_text_color(&style_label_white, lv_color_white());

    lv_style_init(&style_label_dim);
    lv_style_set_text_color(&style_label_dim, lv_color_make(0xa0, 0xa0, 0xa0));

    lv_style_init(&style_submenu_cont);
    lv_style_set_layout(&style_submenu_cont, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&style_submenu_cont, LV_FLEX_FLOW_COLUMN);
    lv_style_set_pad_row(&style_submenu_cont, 6);

    lv_style_init(&style_toggle_row);
    lv_style_set_layout(&style_toggle_row, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&style_toggle_row, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_main_place(&style_toggle_row, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_style_set_flex_cross_place(&style_toggle_row, LV_FLEX_ALIGN_CENTER);

    styles_inited = true;
}

/* ===== 内部状态变量（保留原名，文件内 static） ===== */
/* Block touch-driven menu actions for a short window after a back press,
   so the same gesture that pops a page doesn't also pick the item that
   slides in under the finger. Stamped by the back-button click handler
   below; tested by every action callback that mutates state. */
#define MENU_BACK_DEBOUNCE_MS  350
#define SCROLL_CLICK_SUPPRESS_MS 250

/* 业务状态变量：非 widget，保留 static 不暴露 */
static int  g_wifi_sel          = -1;   /* 选中的 AP 索引 */
static char g_kb_ssid[33]       = {0};  /* 待连接的 SSID */

/* WiFi 状态刷新：1Hz 定时器更新连接中秒数/状态 */
static lv_timer_t *s_wifi_status_timer = NULL;

static void refresh_wifi_status_text(void)
{
    if (!ui_Settings_label_wifi_status) return;
    char ssid_buf[33];
    wifi_get_curr_ssid(ssid_buf, sizeof(ssid_buf));
    char status_buf[128];
    if (wifi_is_connected()) {
        snprintf(status_buf, sizeof(status_buf), LV_SYMBOL_OK " %s", ssid_buf);
        lv_label_set_text(ui_Settings_label_wifi_status, status_buf);
    } else if (ssid_buf[0]) {
        uint32_t elapsed = lv_tick_elaps(wifi_get_connect_started_ms());
        uint8_t reason = wifi_get_last_reason();
        if (reason) {
            snprintf(status_buf, sizeof(status_buf), LV_SYMBOL_WARNING " %s: %s",
                     ssid_buf, wifi_reason_str(reason));
            lv_label_set_text(ui_Settings_label_wifi_status, status_buf);
        } else if (elapsed > 15000) {
            snprintf(status_buf, sizeof(status_buf), LV_SYMBOL_WARNING " %s: timed out",
                     ssid_buf);
            lv_label_set_text(ui_Settings_label_wifi_status, status_buf);
        } else {
            snprintf(status_buf, sizeof(status_buf), tr(I18N_WIFI_CONNECTING_N),
                     ssid_buf, (unsigned)(elapsed / 1000));
            lv_label_set_text(ui_Settings_label_wifi_status, status_buf);
        }
    } else {
        lv_label_set_text(ui_Settings_label_wifi_status, tr(I18N_WIFI_NOT_CONN));
    }
}

static void s_wifi_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_wifi_status_text();
}

static void s_on_wifi_event(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    refresh_wifi_status_text();
}

void settings_set_wifi_status_text(const char *text)
{
    if (ui_Settings_label_wifi_status && text) {
        lv_label_set_text(ui_Settings_label_wifi_status, text);
    }
}

/* ===== 3. 事件回调函数（含 keyboard 等辅助函数） ===== */

/* Keyboard overlay for password entry. */
static void kb_close(void)
{
    if (ui_Settings_kb_overlay) {
        lv_obj_del_async(ui_Settings_kb_overlay);  /* deferred to avoid use-
                                                after-free inside the
                                                lv_keyboard event chain */
        ui_Settings_kb_overlay = NULL;
        ui_Settings_kb_textarea      = NULL;
    }
    clock_ms_timer_resume();
}

static void ui_event_Settings_kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    if (code == LV_EVENT_READY) {
        /* Read the password BEFORE deleting the textarea. We snapshot
           the strings here because lv_keyboard_def_event_cb will keep
           dereferencing keyboard->ta after we return; we use
           lv_obj_del_async so the actual deletion happens after that
           function unwinds. */
        const char *pass = ui_Settings_kb_textarea ? lv_textarea_get_text(ui_Settings_kb_textarea) : "";
        char pass_copy[65] = {0};
        if (pass) {
            size_t pass_len = strlen(pass);
            if (pass_len >= sizeof(pass_copy)) pass_len = sizeof(pass_copy) - 1;
            memcpy(pass_copy, pass, pass_len);
            pass_copy[pass_len] = '\0';
        }
        char ssid[33] = {0};
        size_t ssid_len = strlen(g_kb_ssid);
        if (ssid_len >= sizeof(ssid)) ssid_len = sizeof(ssid) - 1;
        memcpy(ssid, g_kb_ssid, ssid_len);
        ssid[ssid_len] = '\0';
        ESP_LOGI(TAG, "kb: connect ssid=%s pass_len=%u", ssid,
                 (unsigned)strlen(pass_copy));
        /* 暂存凭证，连接成功后再落地到 NVS（避免错误密码被保存） */
        app_cfg_wifi_pending_set(ssid, pass_copy);
        wifi_connect(ssid, pass_copy);
        if (ui_Settings_label_wifi_status) lv_label_set_text_fmt(ui_Settings_label_wifi_status, tr(I18N_WIFI_CONNECTING), ssid);
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
static void ui_event_Settings_kb_eye_toggle(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (!ui_Settings_kb_textarea) return;
    bool was_pw = lv_textarea_get_password_mode(ui_Settings_kb_textarea);
    lv_textarea_set_password_mode(ui_Settings_kb_textarea, !was_pw);
    if (lbl) lv_label_set_text(lbl, was_pw ? LV_SYMBOL_EYE_OPEN
                                           : LV_SYMBOL_EYE_CLOSE);
}

static void kb_open_for_ssid(const char *ssid)
{
    strncpy(g_kb_ssid, ssid, sizeof(g_kb_ssid) - 1);
    g_kb_ssid[sizeof(g_kb_ssid) - 1] = 0;

    /* Pause the 60 Hz ms-clock while typing -- otherwise every
       keystroke contends with the ms label invalidation and the
       extra compositor passes can blow the LVGL task stack. */
    clock_ms_timer_pause();

    lv_obj_t *scr = lv_scr_act();
    ui_Settings_kb_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(ui_Settings_kb_overlay);
    lv_obj_set_size(ui_Settings_kb_overlay, disp_driver_get_canvas_w(), disp_driver_get_canvas_h());
    lv_obj_set_style_bg_color(ui_Settings_kb_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ui_Settings_kb_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(ui_Settings_kb_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Single-line text area at the top + eye toggle on the right edge.
       Pad-tight so the keyboard below gets the rest of the canvas. */
    const int TA_H  = 20;
    const int EYE_W = 28;
    ui_Settings_kb_textarea = lv_textarea_create(ui_Settings_kb_overlay);
    lv_textarea_set_one_line(ui_Settings_kb_textarea, true);
    lv_textarea_set_password_mode(ui_Settings_kb_textarea, true);
    char ph[64];
    snprintf(ph, sizeof(ph), tr(I18N_WIFI_PASS_FOR), ssid);
    lv_textarea_set_placeholder_text(ui_Settings_kb_textarea, ph);
    lv_obj_set_size(ui_Settings_kb_textarea, disp_driver_get_canvas_w() - EYE_W, TA_H);
    lv_obj_align(ui_Settings_kb_textarea, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(ui_Settings_kb_textarea, i18n_font(), 0);
    lv_obj_set_style_pad_top(ui_Settings_kb_textarea, 1, 0);
    lv_obj_set_style_pad_bottom(ui_Settings_kb_textarea, 1, 0);
    lv_obj_set_style_pad_left(ui_Settings_kb_textarea, 6, 0);
    lv_obj_set_style_pad_right(ui_Settings_kb_textarea, 6, 0);
    lv_obj_set_style_border_width(ui_Settings_kb_textarea, 0, 0);
    lv_obj_set_style_radius(ui_Settings_kb_textarea, 0, 0);

    /* Eye toggle: tap to show/hide the password text. Defaults to hidden
       (eye-closed glyph), since the textarea boots in password mode. */
    lv_obj_t *eye = lv_btn_create(ui_Settings_kb_overlay);
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
    lv_obj_add_event_cb(eye, ui_event_Settings_kb_eye_toggle, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(ui_Settings_kb_overlay);
    lv_obj_set_width(kb, disp_driver_get_canvas_w());
    lv_obj_set_height(kb, disp_driver_get_canvas_h() - TA_H);
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
    lv_keyboard_set_textarea(kb, ui_Settings_kb_textarea);
    lv_obj_add_event_cb(kb, ui_event_Settings_kb_event, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, ui_event_Settings_kb_event, LV_EVENT_CANCEL, NULL);
}

/* Tap-to-select: tapping an AP row just highlights it. The right-side
   Connect button uses g_wifi_sel to drive the actual association. */
static void ui_event_Settings_wifi_ap(lv_event_t *e)
{
    if (ui_state_menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)wifi_get_scan_count()) return;
    g_wifi_sel = idx;
    set_render_wifi_list();
}

/* Connect the currently selected AP. Open APs go straight; saved-password
   APs reuse the stored password; unknown-password APs open the keyboard. */
static void ui_event_Settings_wifi_connect(lv_event_t *e)
{
    (void)e;
    if (ui_state_menu_input_blocked()) return;
    int idx = g_wifi_sel;
    if (idx < 0 || idx >= (int)wifi_get_scan_count()) return;
    const wifi_scan_ap_t *ap = wifi_get_scan_ap((uint16_t)idx);
    if (!ap) return;
    if (ap->auth == 0) {
        /* Open AP：暂存空密码，连接成功后保存 */
        app_cfg_wifi_pending_set(ap->ssid, "");
        wifi_connect(ap->ssid, "");
        if (ui_Settings_label_wifi_status) lv_label_set_text_fmt(ui_Settings_label_wifi_status, tr(I18N_WIFI_CONNECTING), ap->ssid);
        return;
    }
    char pass[65] = {0};
    if (app_cfg_get_ssid_pass(ap->ssid, pass, sizeof(pass))) {
        /* 已存密码：暂存凭证，连接成功后刷新 last_ssid */
        app_cfg_wifi_pending_set(ap->ssid, pass);
        wifi_connect(ap->ssid, pass);
        if (ui_Settings_label_wifi_status) lv_label_set_text_fmt(ui_Settings_label_wifi_status, tr(I18N_WIFI_CONNECTING), ap->ssid);
        return;
    }
    kb_open_for_ssid(ap->ssid);
}

/* Forget the selected AP's saved password and clear last_ssid if it
   matches. Keeps the AP visible in the list. */
static void ui_event_Settings_wifi_forget(lv_event_t *e)
{
    (void)e;
    int idx = g_wifi_sel;
    if (idx < 0 || idx >= (int)wifi_get_scan_count()) return;
    const wifi_scan_ap_t *ap = wifi_get_scan_ap((uint16_t)idx);
    if (!ap) return;
    /* nvs_erase_key on the per-SSID record + the auto-connect ssid if
       it points here. */
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        char key[16] = {0};
        size_t copy_len = strlen(ap->ssid);
        if (copy_len > sizeof(key) - 1) copy_len = sizeof(key) - 1;
        memcpy(key, ap->ssid, copy_len);
        nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }
    if (strncmp(ap->ssid, g_cfg.last_ssid, sizeof(g_cfg.last_ssid)) == 0) {
        app_cfg_set_last_ssid("");
        esp_wifi_disconnect();
        g_wifi_connected = false;
    }
    set_render_wifi_list();
    if (ui_Settings_label_wifi_status) lv_label_set_text(ui_Settings_label_wifi_status, tr(I18N_WIFI_NOT_CONN));
}

static void set_render_wifi_list(void)
{
    if (!ui_Settings_obj_wifi_list) return;
    lv_obj_clean(ui_Settings_obj_wifi_list);
    uint16_t scan_n = wifi_get_scan_count();
    if (scan_n == 0) {
        lv_obj_t *empty = lv_label_create(ui_Settings_obj_wifi_list);
        lv_label_set_text(empty, tr(I18N_WIFI_NO_APS));
        lv_obj_add_style(empty, &style_label_dim, 0);
        lv_obj_set_style_text_font(empty, i18n_font(), 0);
        return;
    }
    char curr_ssid[33];
    wifi_get_curr_ssid(curr_ssid, sizeof(curr_ssid));
    bool connected = wifi_is_connected();
    for (int i = 0; i < (int)scan_n; i++) {
        const wifi_scan_ap_t *ap = wifi_get_scan_ap((uint16_t)i);
        if (!ap) continue;
        lv_obj_t *btn = lv_btn_create(ui_Settings_obj_wifi_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 22);
        bool is_connected = connected &&
                            strncmp(ap->ssid, curr_ssid,
                                    sizeof(curr_ssid)) == 0;
        bool is_selected  = (i == g_wifi_sel);
        char dummy_pass[2];
        bool is_saved = app_cfg_get_ssid_pass(ap->ssid, dummy_pass, sizeof(dummy_pass));
        lv_obj_set_style_bg_color(btn,
            is_selected ? lv_color_make(0x40, 0x40, 0x60)
                        : lv_color_make(0x20, 0x20, 0x30), 0);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_add_event_cb(btn, ui_event_Settings_wifi_ap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
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
   ui_event_Settings_scan_btn; lifted to a named static function so the file compiles as C. */
static void ui_Settings_scan_refresh_timer_cb(lv_timer_t *tt)
{
    if (ui_Settings_label_wifi_status) {
        lv_label_set_text_fmt(ui_Settings_label_wifi_status, "Found %u networks", (unsigned)wifi_get_scan_count());
    }
    set_render_wifi_list();
    lv_timer_del(tt);
}

static void ui_event_Settings_scan_btn(lv_event_t *e)
{
    (void)e;
    if (ui_Settings_label_wifi_status) lv_label_set_text(ui_Settings_label_wifi_status, tr(I18N_WIFI_SCANNING));
    wifi_start_scan();
    /* Schedule a one-shot UI refresh ~3s later when scan_done has fired. */
    lv_timer_t *t = lv_timer_create(ui_Settings_scan_refresh_timer_cb, 3000, NULL);
    (void)t;
}

static void ui_Settings_menu_shield_drop_timer_cb(lv_timer_t *t)
{
    if (ui_Settings_menu_shield) {
        lv_obj_del(ui_Settings_menu_shield);
        ui_Settings_menu_shield = NULL;
    }
    lv_timer_del(t);
}

static void ui_event_Settings_menu_back(lv_event_t *e)
{
    (void)e;
    ui_state_set_menu_block_until_ms(lv_tick_get() + MENU_BACK_DEBOUNCE_MS);
    if (ui_Settings_menu_shield) return;  /* already shielded */
    /* Parent to the active screen so the shield covers the menu and any
       header/back-button that might still be sitting under the finger. */
    ui_Settings_menu_shield = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(ui_Settings_menu_shield);
    lv_obj_set_size(ui_Settings_menu_shield, disp_driver_get_canvas_w(), disp_driver_get_canvas_h());
    lv_obj_align(ui_Settings_menu_shield, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(ui_Settings_menu_shield, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(ui_Settings_menu_shield, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui_Settings_menu_shield, LV_OBJ_FLAG_CLICKABLE);
    /* Move to top z-order so subsequent clicks land on the shield. */
    lv_obj_move_foreground(ui_Settings_menu_shield);
    lv_timer_t *t = lv_timer_create(ui_Settings_menu_shield_drop_timer_cb, MENU_BACK_DEBOUNCE_MS, NULL);
    (void)t;
}

static void ui_event_Settings_tz_city_pick(lv_event_t *e)
{
    if (ui_state_menu_input_blocked()) return;
    /* user_data carries the city index packed into a void*. */
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= TZ_CITY_COUNT) return;
    tz_apply_current();
    app_cfg_set_tz_idx((int)idx);
}

static void ui_event_Settings_bri_slider(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        app_cfg_set_brightness(v);
    } else {
        /* 拖动中实时更新背光，不保存 */
        g_cfg.brightness = (uint8_t)v;
        event_bus_publish(EVENT_BACKLIGHT_CHANGED, &g_cfg.brightness, sizeof(g_cfg.brightness));
    }
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

static void ui_event_Settings_dim_s(lv_event_t *e)
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
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        app_cfg_set_dim_off(g_cfg.dim_s, g_cfg.off_s);
    }
}

/* ---------- Display sub-page callbacks (12/24h, date fmt, secs/ms, FPS) ---------- */

static void ui_event_Settings_hour_fmt(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    app_cfg_set_hour24(lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0);
}

static void ui_event_Settings_show_sec(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    app_cfg_set_show_seconds(lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0);
}

static void ui_event_Settings_show_ms(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    app_cfg_set_show_ms(lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0);
}

static void ui_event_Settings_show_fps(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    app_cfg_set_show_fps(lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0);
}

static void ui_event_Settings_date_fmt(lv_event_t *e)
{
    lv_obj_t *r = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(r);
    if (sel < 0) sel = 0;
    if (sel > 2) sel = 2;
    app_cfg_set_date_fmt(sel);
}

/* ---------- Sound sub-page callbacks (enable + volume) ---------- */

static void ui_event_Settings_audio_en(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int enable = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    if (!enable && audio_min_is_playing()) audio_min_play_midi(false);
    app_cfg_set_audio_enable(enable);
}

static void ui_event_Settings_audio_vol(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    audio_min_set_volume((uint8_t)v);
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (lbl) lv_label_set_text_fmt(lbl, tr(I18N_VOLUME_PCT), (unsigned)v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        app_cfg_set_audio_volume(v);
    } else {
        /* 拖动中实时更新音量，不保存 */
        g_cfg.audio_volume = (uint8_t)v;
        uint8_t vol = (uint8_t)v;
        event_bus_publish(EVENT_AUDIO_VOLUME_CHANGED, &vol, sizeof(vol));
    }
}

/* ---------- Theme + Wi-Fi auto-connect ---------- */

static void ui_event_Settings_theme(lv_event_t *e)
{
    lv_obj_t *r = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(r);
    if (sel < 0) sel = 0;
    if (sel > 2) sel = 2;
    app_cfg_set_theme(sel);
    /* Sunmap can be re-themed live; menu colors apply on next boot. */
    sunmap_redraw();
}

static void ui_event_Settings_wifi_ac(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    app_cfg_set_wifi_autoconnect(lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0);
}

/* ---------- Reset to defaults ---------- */

static void ui_event_Settings_reset_confirm(lv_event_t *e)
{
    (void)e;
    if (ui_state_menu_input_blocked()) return;
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

static void ui_event_Settings_off_s(lv_event_t *e)
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
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        app_cfg_set_dim_off(g_cfg.dim_s, g_cfg.off_s);
    }
}

/* ===== 4. 子页构建器 + ui_Settings_create ===== */
/* Build a sub-page that the menu can navigate to. Returns the page so
   the caller can attach it via lv_menu_set_load_page_event. */

static lv_obj_t *ui_Settings_subpage_wifi_create(lv_obj_t *menu)
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
    ui_Settings_obj_wifi_list = lv_obj_create(cont);
    lv_obj_set_width(ui_Settings_obj_wifi_list, lv_pct(60));
    lv_obj_set_height(ui_Settings_obj_wifi_list, lv_pct(100));
    lv_obj_set_layout(ui_Settings_obj_wifi_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui_Settings_obj_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(ui_Settings_obj_wifi_list, &style_wifi_list_bg, 0);
    lv_obj_set_scroll_dir(ui_Settings_obj_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_Settings_obj_wifi_list, LV_SCROLLBAR_MODE_AUTO);
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

    ui_Settings_label_wifi_status = lv_label_create(side);
    lv_label_set_long_mode(ui_Settings_label_wifi_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui_Settings_label_wifi_status, lv_pct(100));
    {
        char ssid_buf[33];
        wifi_get_curr_ssid(ssid_buf, sizeof(ssid_buf));
        lv_label_set_text_fmt(ui_Settings_label_wifi_status, "%s",
                              wifi_is_connected() ? ssid_buf : tr(I18N_WIFI_NOT_CONN));
    }
    lv_obj_set_style_text_color(ui_Settings_label_wifi_status, lv_color_make(0xc0, 0xc0, 0xc0), 0);
    lv_obj_set_style_text_font(ui_Settings_label_wifi_status, i18n_font(), 0);

    /* Scan button */
    lv_obj_t *scan_btn = lv_btn_create(side);
    lv_obj_set_size(scan_btn, lv_pct(100), 24);
    lv_obj_t *scan_l = lv_label_create(scan_btn);
    lv_label_set_text(scan_l, tr(I18N_WIFI_SCAN_BTN));
    lv_obj_set_style_text_font(scan_l, i18n_font(), 0);
    lv_obj_center(scan_l);
    lv_obj_add_event_cb(scan_btn, ui_event_Settings_scan_btn, LV_EVENT_CLICKED, NULL);

    /* Connect button (acts on selected list row) */
    lv_obj_t *conn_btn = lv_btn_create(side);
    lv_obj_set_size(conn_btn, lv_pct(100), 24);
    lv_obj_add_style(conn_btn, &style_connect_btn, 0);
    lv_obj_t *conn_l = lv_label_create(conn_btn);
    lv_label_set_text(conn_l, tr(I18N_WIFI_CONNECT_BTN));
    lv_obj_set_style_text_color(conn_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(conn_l, i18n_font(), 0);
    lv_obj_center(conn_l);
    lv_obj_add_event_cb(conn_btn, ui_event_Settings_wifi_connect, LV_EVENT_CLICKED, NULL);

    /* Forget button */
    lv_obj_t *forget_btn = lv_btn_create(side);
    lv_obj_set_size(forget_btn, lv_pct(100), 24);
    lv_obj_add_style(forget_btn, &style_forget_btn, 0);
    lv_obj_t *forget_l = lv_label_create(forget_btn);
    lv_label_set_text(forget_l, tr(I18N_WIFI_FORGET_BTN));
    lv_obj_set_style_text_color(forget_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(forget_l, i18n_font(), 0);
    lv_obj_center(forget_l);
    lv_obj_add_event_cb(forget_btn, ui_event_Settings_wifi_forget, LV_EVENT_CLICKED, NULL);

    return page;
}

static lv_obj_t *ui_Settings_subpage_tz_city_list_create(lv_obj_t *menu, uint16_t first, uint16_t last,
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
        lv_obj_add_event_cb(cont, ui_event_Settings_tz_city_pick, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }
    return page;
}

static lv_obj_t *ui_Settings_subpage_tz_create(lv_obj_t *menu)
{
    /* Build the per-continent city pages first, then a continent-list
       root page that links to them. Two-level navigation matches the
       OpenWRT-style "Continent / City" picker. */
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_TZ));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    for (uint16_t c = 0; c < TZ_CONTINENT_COUNT; c++) {
        lv_obj_t *city_page = ui_Settings_subpage_tz_city_list_create(menu,
            k_tz_continents[c].first, k_tz_continents[c].last,
            k_tz_continents[c].name);
        lv_obj_t *cont = lv_menu_cont_create(page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text(l, k_tz_continents[c].name);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        lv_menu_set_load_page_event(menu, cont, city_page);
        lv_obj_add_event_cb(cont, ui_event_Settings_menu_back, LV_EVENT_CLICKED, NULL);
    }
    return page;
}

static lv_obj_t *ui_Settings_subpage_brightness_create(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_BRIGHTNESS));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_add_style(cont, &style_submenu_cont, 0);

    lv_obj_t *l = lv_label_create(cont);
    lv_label_set_text(l, tr(I18N_BACKLIGHT_LEVEL));
    lv_obj_add_style(l, &style_label_white, 0);
    lv_obj_set_style_text_font(l, i18n_font(), 0);

    lv_obj_t *bri_s = lv_slider_create(cont);
    lv_obj_set_width(bri_s, lv_pct(95));
    lv_slider_set_range(bri_s, 8, 255);
    lv_slider_set_value(bri_s, g_cfg.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(bri_s, ui_event_Settings_bri_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(bri_s, ui_event_Settings_bri_slider, LV_EVENT_RELEASED,      NULL);
    lv_obj_clear_flag(bri_s, LV_OBJ_FLAG_GESTURE_BUBBLE);

    return page;
}

static lv_obj_t *ui_Settings_subpage_autodim_create(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_AUTODIM));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_add_style(cont, &style_submenu_cont, 0);

    lv_obj_t *dim_lbl = lv_label_create(cont);
    {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.dim_s);
        if (g_cfg.dim_s == 0) lv_label_set_text(dim_lbl, tr(I18N_DIM_NEVER));
        else                  lv_label_set_text_fmt(dim_lbl, tr(I18N_DIM_AFTER), d);
    }
    lv_obj_add_style(dim_lbl, &style_label_white, 0);
    lv_obj_set_style_text_font(dim_lbl, i18n_font(), 0);
    lv_obj_t *dim_s = lv_slider_create(cont);
    lv_obj_set_width(dim_s, lv_pct(95));
    lv_slider_set_range(dim_s, 0, IDLE_SLIDER_MAX);
    lv_slider_set_value(dim_s, g_cfg.dim_s, LV_ANIM_OFF);
    /* Don't let horizontal slider drags bubble up and trigger a tileview
       page swipe. Same for the gesture flag. */
    lv_obj_clear_flag(dim_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(dim_s, ui_event_Settings_dim_s, LV_EVENT_VALUE_CHANGED, dim_lbl);
    lv_obj_add_event_cb(dim_s, ui_event_Settings_dim_s, LV_EVENT_RELEASED,      dim_lbl);

    lv_obj_t *off_lbl = lv_label_create(cont);
    {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.off_s);
        if (g_cfg.off_s == 0) lv_label_set_text(off_lbl, tr(I18N_SLEEP_NEVER));
        else                  lv_label_set_text_fmt(off_lbl, tr(I18N_SLEEP_AFTER), d);
    }
    lv_obj_add_style(off_lbl, &style_label_white, 0);
    lv_obj_set_style_text_font(off_lbl, i18n_font(), 0);
    lv_obj_t *off_s = lv_slider_create(cont);
    lv_obj_set_width(off_s, lv_pct(95));
    lv_slider_set_range(off_s, 0, IDLE_SLIDER_MAX);
    lv_slider_set_value(off_s, g_cfg.off_s, LV_ANIM_OFF);
    lv_obj_clear_flag(off_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(off_s, ui_event_Settings_off_s, LV_EVENT_VALUE_CHANGED, off_lbl);
    lv_obj_add_event_cb(off_s, ui_event_Settings_off_s, LV_EVENT_RELEASED,      off_lbl);

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
    lv_obj_add_style(row, &style_toggle_row, 0);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, i18n_font(), 0);
    lv_obj_t *sw = lv_switch_create(row);
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return row;
}

static void ui_event_Settings_lang_pick(lv_event_t *e)
{
    if (ui_state_menu_input_blocked()) return;
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

static lv_obj_t *ui_Settings_subpage_language_create(lv_obj_t *menu)
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
        lv_obj_add_event_cb(cont, ui_event_Settings_lang_pick, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    return page;
}

static lv_obj_t *ui_Settings_subpage_display_create(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_DISPLAY));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_add_style(cont, &style_submenu_cont, 0);

    add_toggle_row(cont, tr(I18N_HOUR_24), g_cfg.hour24, ui_event_Settings_hour_fmt);
    add_toggle_row(cont, tr(I18N_SHOW_SECONDS), g_cfg.show_seconds, ui_event_Settings_show_sec);
    add_toggle_row(cont, tr(I18N_SHOW_MS), g_cfg.show_ms, ui_event_Settings_show_ms);
    add_toggle_row(cont, tr(I18N_SHOW_FPS), g_cfg.show_fps, ui_event_Settings_show_fps);

    lv_obj_t *df_l = lv_label_create(cont);
    lv_label_set_text(df_l, tr(I18N_DATE_FORMAT));
    lv_obj_set_style_text_font(df_l, i18n_font(), 0);
    lv_obj_t *df = lv_dropdown_create(cont);
    lv_dropdown_set_options_static(df,
        "YYYY.MM.DD\nDD.MM.YYYY\nMM.DD.YYYY");
    lv_dropdown_set_selected(df, g_cfg.date_fmt);
    lv_obj_add_event_cb(df, ui_event_Settings_date_fmt, LV_EVENT_VALUE_CHANGED, NULL);

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
    lv_obj_add_event_cb(th, ui_event_Settings_theme, LV_EVENT_VALUE_CHANGED, NULL);

    return page;
}

static lv_obj_t *ui_Settings_subpage_sound_create(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_SOUND));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_add_style(cont, &style_submenu_cont, 0);

    add_toggle_row(cont, tr(I18N_SOUND_ENABLED), g_cfg.audio_enable, ui_event_Settings_audio_en);

    lv_obj_t *vol_lbl = lv_label_create(cont);
    lv_label_set_text_fmt(vol_lbl, tr(I18N_VOLUME_PCT), (unsigned)g_cfg.audio_volume);
    lv_obj_set_style_text_font(vol_lbl, i18n_font(), 0);

    lv_obj_t *vol_s = lv_slider_create(cont);
    lv_obj_set_width(vol_s, lv_pct(95));
    lv_slider_set_range(vol_s, 0, 100);
    lv_slider_set_value(vol_s, g_cfg.audio_volume, LV_ANIM_OFF);
    lv_obj_clear_flag(vol_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(vol_s, ui_event_Settings_audio_vol, LV_EVENT_VALUE_CHANGED, vol_lbl);
    lv_obj_add_event_cb(vol_s, ui_event_Settings_audio_vol, LV_EVENT_RELEASED,      vol_lbl);

    return page;
}

static lv_obj_t *ui_Settings_subpage_reset_create(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_RESET));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_add_style(cont, &style_submenu_cont, 0);

    lv_obj_t *l = lv_label_create(cont);
    lv_label_set_text(l, tr(I18N_RESET_WARN));
    lv_obj_set_style_text_font(l, i18n_font(), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(95));

    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_height(btn, 32);
    lv_obj_set_width(btn, 160);
    lv_obj_add_style(btn, &style_reset_btn, 0);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, tr(I18N_RESET_BTN));
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl, i18n_font(), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, ui_event_Settings_reset_confirm, LV_EVENT_CLICKED, NULL);

    return page;
}

void storage_info_refresh(void)
{
    if (!ui_Settings_label_storage_info) return;
    if (!sdcard_is_mounted()) {
        lv_label_set_text(ui_Settings_label_storage_info, "SD: not mounted");
        return;
    }
    uint64_t total = 0, free = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &free) == ESP_OK && total > 0) {
        lv_label_set_text_fmt(ui_Settings_label_storage_info, "SD: %llu MB free of %llu MB",
                              (unsigned long long)(free / (1024 * 1024)),
                              (unsigned long long)(total / (1024 * 1024)));
    } else {
        lv_label_set_text(ui_Settings_label_storage_info, "SD: read err");
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
    if (ui_Settings_label_storage_btn) {
        lv_label_set_text(ui_Settings_label_storage_btn,
                          r == ESP_OK ? "Format SD card"
                                      : "Format failed");
    }
    event_bus_publish(EVENT_STORAGE_CHANGED, NULL, 0);
    vTaskDelete(NULL);
}

static void ui_event_Settings_sd_format(lv_event_t *e)
{
    (void)e;
    if (ui_state_menu_input_blocked()) return;
    if (ui_Settings_label_storage_btn) lv_label_set_text(ui_Settings_label_storage_btn, "Formatting...");
    /* Format on a worker so the LVGL task keeps drawing. 64GB FAT32
       format takes ~30-60s on this controller. */
    xTaskCreatePinnedToCore(sd_format_worker, "sd_fmt", 4 * 1024,
                            NULL, 4, NULL, 1);
}

static lv_obj_t *ui_Settings_subpage_storage_create(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_STORAGE));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_add_style(cont, &style_submenu_cont, 0);

    ui_Settings_label_storage_info = lv_label_create(cont);
    lv_obj_set_style_text_font(ui_Settings_label_storage_info, i18n_font(), 0);
    lv_obj_add_style(ui_Settings_label_storage_info, &style_label_white, 0);
    /* Don't call esp_vfs_fat_info at tile-build time -- on some cards it
       hangs the SDMMC driver for many seconds and wedges boot. Show a
       static placeholder; storage_info_refresh() is invoked from the
       sd_format_worker after a format completes. */
    lv_label_set_text(ui_Settings_label_storage_info,
                      sdcard_is_mounted() ? "SD: mounted" : "SD: not mounted");

    /* Format button: full FAT32 reformat via esp_vfs_fat_sdcard_format.
       Wipes everything on the card. Runs on a worker task so the UI
       stays responsive (~30-60s for 64GB). */
    lv_obj_t *fmt_btn = lv_btn_create(cont);
    lv_obj_set_size(fmt_btn, 200, 32);
    lv_obj_add_style(fmt_btn, &style_format_btn, 0);
    ui_Settings_label_storage_btn = lv_label_create(fmt_btn);
    lv_label_set_text(ui_Settings_label_storage_btn, "Format SD card");
    lv_obj_set_style_text_color(ui_Settings_label_storage_btn, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui_Settings_label_storage_btn, i18n_font(), 0);
    lv_obj_center(ui_Settings_label_storage_btn);
    lv_obj_add_event_cb(fmt_btn, ui_event_Settings_sd_format, LV_EVENT_CLICKED, NULL);
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
    add_toggle_row(side, tr(I18N_WIFI_AUTOCONNECT), g_cfg.wifi_autoconnect, ui_event_Settings_wifi_ac);
}

void ui_Settings_create(lv_obj_t *parent)
{
    init_styles();
    ui_Settings = parent;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(parent, &style_tile_bg, 0);

    /* 订阅 WiFi 事件，自动刷新状态文本 */
    event_bus_subscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_SCAN_STARTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_SCAN_DONE, s_on_wifi_event, NULL);
    /* 启动 1Hz 定时器，刷新连接中秒数显示 */
    s_wifi_status_timer = lv_timer_create(s_wifi_status_timer_cb, 1000, NULL);
    refresh_wifi_status_text();

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
        lv_obj_add_event_cb(back, ui_event_Settings_menu_back, LV_EVENT_CLICKED, NULL);
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
    lv_obj_t *p_wifi = ui_Settings_subpage_wifi_create(menu);
    wifi_subpage_add_autoconnect(p_wifi);
    lv_obj_t *p_tz    = ui_Settings_subpage_tz_create(menu);
    lv_obj_t *p_bri   = ui_Settings_subpage_brightness_create(menu);
    lv_obj_t *p_dim   = ui_Settings_subpage_autodim_create(menu);
    lv_obj_t *p_disp  = ui_Settings_subpage_display_create(menu);
    lv_obj_t *p_snd   = ui_Settings_subpage_sound_create(menu);
    lv_obj_t *p_lang  = ui_Settings_subpage_language_create(menu);
    lv_obj_t *p_reset = ui_Settings_subpage_reset_create(menu);
    lv_obj_t *p_storage = ui_Settings_subpage_storage_create(menu);

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
        lv_obj_add_event_cb(cont, ui_event_Settings_menu_back, LV_EVENT_CLICKED, NULL);
    }

    lv_menu_set_page(menu, main_page);
}

/* ===== 5. 清理函数 ===== */
void ui_Settings_cleanup(void)
{
    /* 取消 WiFi 事件订阅 */
    event_bus_unsubscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event);
    event_bus_unsubscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event);
    event_bus_unsubscribe(EVENT_WIFI_SCAN_STARTED, s_on_wifi_event);
    event_bus_unsubscribe(EVENT_WIFI_SCAN_DONE, s_on_wifi_event);
    /* 停止 WiFi 状态定时器 */
    if (s_wifi_status_timer) {
        lv_timer_del(s_wifi_status_timer);
        s_wifi_status_timer = NULL;
    }

    ui_Settings = NULL;
    ui_Settings_label_wifi_status = NULL;
    ui_Settings_obj_wifi_list    = NULL;
    ui_Settings_kb_overlay       = NULL;
    ui_Settings_kb_textarea      = NULL;
    ui_Settings_menu_shield      = NULL;
}
