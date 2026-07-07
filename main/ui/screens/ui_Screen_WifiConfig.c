#include "ui_Screen_WifiConfig.h"
#include "../ui_helpers.h"
#include "../ui_clock.h"
#include "../../network/wifi_manager.h"
#include "../../network/wifi_provision.h"
#include "../../config/app_cfg.h"
#include "../../utils/event_bus.h"
#include "../../drivers/disp_driver.h"
#include "i18n.h"
#include "../ui.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "lvgl.h"

static const char *TAG = "ui_Screen_WifiConfig";

#define MENU_BACK_DEBOUNCE_MS  350

static int  g_wifi_sel          = -1;
static char g_kb_ssid[33]       = {0};
static lv_timer_t *s_wifi_status_timer = NULL;

static lv_obj_t *s_wifi_list = NULL;
static lv_obj_t *s_wifi_status_label = NULL;
static lv_obj_t *s_kb_overlay = NULL;
static lv_obj_t *s_kb_textarea = NULL;
static lv_obj_t *s_provision_btn = NULL;

static lv_style_t s_style_wifi_list_bg;
static lv_style_t s_style_connect_btn;
static lv_style_t s_style_forget_btn;
static lv_style_t s_style_scan_btn;
static lv_style_t s_style_provision_btn;
static lv_style_t s_style_label_dim;
static bool s_styles_inited = false;

lv_obj_t* ui_Screen_WifiConfig = NULL;

static void wifi_config_refresh_list(void);

static void init_wifi_styles(void)
{
    if (s_styles_inited) return;

    lv_style_init(&s_style_wifi_list_bg);
    lv_style_set_bg_color(&s_style_wifi_list_bg, lv_color_make(0x10, 0x10, 0x14));

    lv_style_init(&s_style_connect_btn);
    lv_style_set_bg_color(&s_style_connect_btn, lv_color_make(0x20, 0x80, 0x40));

    lv_style_init(&s_style_forget_btn);
    lv_style_set_bg_color(&s_style_forget_btn, lv_color_make(0x80, 0x40, 0x20));

    lv_style_init(&s_style_scan_btn);
    lv_style_set_bg_color(&s_style_scan_btn, lv_color_make(0x40, 0x60, 0xa0));

    lv_style_init(&s_style_provision_btn);
    lv_style_set_bg_color(&s_style_provision_btn, lv_color_make(0x80, 0x60, 0xa0));

    lv_style_init(&s_style_label_dim);
    lv_style_set_text_color(&s_style_label_dim, lv_color_make(0xa0, 0xa0, 0xa0));

    s_styles_inited = true;
}

static void wifi_config_refresh_status(void)
{
    if (!s_wifi_status_label) return;
    char ssid_buf[33];
    wifi_get_curr_ssid(ssid_buf, sizeof(ssid_buf));
    char status_buf[128];
    if (wifi_is_connected()) {
        snprintf(status_buf, sizeof(status_buf), LV_SYMBOL_OK " %s", ssid_buf);
        lv_label_set_text(s_wifi_status_label, status_buf);
    } else if (ssid_buf[0]) {
        uint32_t elapsed = lv_tick_elaps(wifi_get_connect_started_ms());
        uint8_t reason = wifi_get_last_reason();
        if (reason) {
            snprintf(status_buf, sizeof(status_buf), LV_SYMBOL_WARNING " %s: %s",
                     ssid_buf, wifi_reason_str(reason));
            lv_label_set_text(s_wifi_status_label, status_buf);
        } else if (elapsed > 15000) {
            snprintf(status_buf, sizeof(status_buf), LV_SYMBOL_WARNING " %s: timed out",
                     ssid_buf);
            lv_label_set_text(s_wifi_status_label, status_buf);
        } else {
            snprintf(status_buf, sizeof(status_buf), tr(I18N_WIFI_CONNECTING_N),
                     ssid_buf, (unsigned)(elapsed / 1000));
            lv_label_set_text(s_wifi_status_label, status_buf);
        }
    } else {
        lv_label_set_text(s_wifi_status_label, tr(I18N_WIFI_NOT_CONN));
    }
}

static void s_wifi_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    wifi_config_refresh_status();
}

static void s_on_wifi_event(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (evt->id == EVENT_WIFI_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected, launching main UI");
        ui_Screen_WifiConfig_screen_cleanup();
        show_main_ui(NULL);
        return;
    }
    wifi_config_refresh_status();
}

static void kb_close(void)
{
    if (s_kb_overlay) {
        lv_obj_del_async(s_kb_overlay);
        s_kb_overlay = NULL;
        s_kb_textarea = NULL;
    }
    clock_ms_timer_resume();
}

static void ui_event_wifi_kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *pass = s_kb_textarea ? lv_textarea_get_text(s_kb_textarea) : "";
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
        ESP_LOGI(TAG, "kb: connect ssid=%s pass_len=%u", ssid, (unsigned)strlen(pass_copy));
        app_cfg_wifi_pending_set(ssid, pass_copy);
        wifi_connect(ssid, pass_copy);
        if (s_wifi_status_label) lv_label_set_text_fmt(s_wifi_status_label, tr(I18N_WIFI_CONNECTING), ssid);
        kb_close();
    } else if (code == LV_EVENT_CANCEL) {
        kb_close();
    }
}

static const char *kKbMapLower[] = {
    "1","2","3","4","5","6","7","8","9","0", LV_SYMBOL_BACKSPACE, "\n",
    "q","w","e","r","t","y","u","i","o","p", "\n",
    "a","s","d","f","g","h","j","k","l", "\n",
    "ABC", "z","x","c","v","b","n","m",",",".", "\n",
    "1#", "@",".", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, "_","-", LV_SYMBOL_OK, ""
};
#define KB_CHAR  (LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 1)
#define KB_CTRL2 (LV_KEYBOARD_CTRL_BTN_FLAGS | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 2)
#define KB_CTRL1 (LV_KEYBOARD_CTRL_BTN_FLAGS | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 1)
static const lv_btnmatrix_ctrl_t kKbCtrlLower[] = {
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

static void ui_event_wifi_kb_eye_toggle(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (!s_kb_textarea) return;
    bool was_pw = lv_textarea_get_password_mode(s_kb_textarea);
    lv_textarea_set_password_mode(s_kb_textarea, !was_pw);
    if (lbl) lv_label_set_text(lbl, was_pw ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static void kb_open_for_ssid(const char *ssid)
{
    strncpy(g_kb_ssid, ssid, sizeof(g_kb_ssid) - 1);
    g_kb_ssid[sizeof(g_kb_ssid) - 1] = 0;
    clock_ms_timer_pause();

    int canvas_w = disp_driver_get_canvas_w();
    int canvas_h = disp_driver_get_canvas_h();

    lv_obj_t *scr = lv_scr_act();
    s_kb_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_kb_overlay);
    lv_obj_set_size(s_kb_overlay, canvas_w, canvas_h);
    lv_obj_set_style_bg_color(s_kb_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_kb_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(s_kb_overlay, LV_OBJ_FLAG_SCROLLABLE);

    const int TA_H  = 20;
    const int EYE_W = 28;
    s_kb_textarea = lv_textarea_create(s_kb_overlay);
    lv_textarea_set_one_line(s_kb_textarea, true);
    lv_textarea_set_password_mode(s_kb_textarea, true);
    char ph[64];
    snprintf(ph, sizeof(ph), tr(I18N_WIFI_PASS_FOR), ssid);
    lv_textarea_set_placeholder_text(s_kb_textarea, ph);
    lv_obj_set_size(s_kb_textarea, canvas_w - EYE_W, TA_H);
    lv_obj_align(s_kb_textarea, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(s_kb_textarea, i18n_font(), 0);
    lv_obj_set_style_pad_top(s_kb_textarea, 1, 0);
    lv_obj_set_style_pad_bottom(s_kb_textarea, 1, 0);
    lv_obj_set_style_pad_left(s_kb_textarea, 6, 0);
    lv_obj_set_style_pad_right(s_kb_textarea, 6, 0);
    lv_obj_set_style_border_width(s_kb_textarea, 0, 0);
    lv_obj_set_style_radius(s_kb_textarea, 0, 0);

    lv_obj_t *eye = lv_btn_create(s_kb_overlay);
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
    lv_obj_add_event_cb(eye, ui_event_wifi_kb_eye_toggle, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(s_kb_overlay);
    lv_obj_set_width(kb, canvas_w);
    lv_obj_set_height(kb, canvas_h - TA_H);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_row(kb, 2, 0);
    lv_obj_set_style_pad_column(kb, 2, 0);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER,
                        (const char **)kKbMapLower, kKbCtrlLower);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER,
                        (const char **)kKbMapUpper, kKbCtrlUpper);
    lv_keyboard_set_textarea(kb, s_kb_textarea);
    lv_obj_add_event_cb(kb, ui_event_wifi_kb_event, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, ui_event_wifi_kb_event, LV_EVENT_CANCEL, NULL);
}

static void ui_event_wifi_ap(lv_event_t *e)
{
    if (ui_helpers_menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)wifi_get_scan_count()) return;
    g_wifi_sel = idx;
    wifi_config_refresh_list();
}

static void ui_event_wifi_connect(lv_event_t *e)
{
    (void)e;
    if (ui_helpers_menu_input_blocked()) return;
    int idx = g_wifi_sel;
    if (idx < 0 || idx >= (int)wifi_get_scan_count()) return;
    const wifi_scan_ap_t *ap = wifi_get_scan_ap((uint16_t)idx);
    if (!ap) return;
    if (ap->auth == 0) {
        app_cfg_wifi_pending_set(ap->ssid, "");
        wifi_connect(ap->ssid, "");
        if (s_wifi_status_label) lv_label_set_text_fmt(s_wifi_status_label, tr(I18N_WIFI_CONNECTING), ap->ssid);
        return;
    }
    char pass[65] = {0};
    if (app_cfg_get_ssid_pass(ap->ssid, pass, sizeof(pass))) {
        app_cfg_wifi_pending_set(ap->ssid, pass);
        wifi_connect(ap->ssid, pass);
        if (s_wifi_status_label) lv_label_set_text_fmt(s_wifi_status_label, tr(I18N_WIFI_CONNECTING), ap->ssid);
        return;
    }
    kb_open_for_ssid(ap->ssid);
}

static void ui_event_wifi_forget(lv_event_t *e)
{
    (void)e;
    int idx = g_wifi_sel;
    if (idx < 0 || idx >= (int)wifi_get_scan_count()) return;
    const wifi_scan_ap_t *ap = wifi_get_scan_ap((uint16_t)idx);
    if (!ap) return;
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
    wifi_config_refresh_list();
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, tr(I18N_WIFI_NOT_CONN));
}

static void ui_event_wifi_provision(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Starting WiFi AP provisioning");
    wifi_provision_start(NULL, NULL);
    if (s_wifi_status_label) {
        lv_label_set_text(s_wifi_status_label, "AP Started: NAS-Monitor");
    }
}

static void wifi_config_refresh_list(void)
{
    if (!s_wifi_list) return;
    lv_obj_clean(s_wifi_list);
    uint16_t scan_n = wifi_get_scan_count();
    if (scan_n == 0) {
        lv_obj_t *empty = lv_label_create(s_wifi_list);
        lv_label_set_text(empty, tr(I18N_WIFI_NO_APS));
        lv_obj_add_style(empty, &s_style_label_dim, 0);
        lv_obj_set_style_text_font(empty, i18n_font(), 0);
        return;
    }
    char curr_ssid[33];
    wifi_get_curr_ssid(curr_ssid, sizeof(curr_ssid));
    bool connected = wifi_is_connected();
    for (int i = 0; i < (int)scan_n; i++) {
        const wifi_scan_ap_t *ap = wifi_get_scan_ap((uint16_t)i);
        if (!ap) continue;
        lv_obj_t *btn = lv_btn_create(s_wifi_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 22);
        bool is_connected = connected &&
                            strncmp(ap->ssid, curr_ssid, sizeof(curr_ssid)) == 0;
        bool is_selected  = (i == g_wifi_sel);
        char dummy_pass[2];
        bool is_saved = app_cfg_get_ssid_pass(ap->ssid, dummy_pass, sizeof(dummy_pass));
        lv_obj_set_style_bg_color(btn,
            is_selected ? lv_color_make(0x40, 0x40, 0x60)
                        : lv_color_make(0x20, 0x20, 0x30), 0);
        lv_obj_add_event_cb(btn, ui_event_wifi_ap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(btn);
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

static void ui_wifi_scan_refresh_timer_cb(lv_timer_t *tt)
{
    if (s_wifi_status_label) {
        lv_label_set_text_fmt(s_wifi_status_label, tr(I18N_WIFI_FOUND_N), (unsigned)wifi_get_scan_count());
    }
    wifi_config_refresh_list();
    lv_timer_del(tt);
}

static void ui_event_wifi_scan_btn(lv_event_t *e)
{
    (void)e;
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, tr(I18N_WIFI_SCANNING));
    wifi_start_scan();
    lv_timer_t *t = lv_timer_create(ui_wifi_scan_refresh_timer_cb, 3000, NULL);
    (void)t;
}

void ui_Screen_WifiConfig_screen_init(void)
{
    init_wifi_styles();

    ui_Screen_WifiConfig = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen_WifiConfig, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_WifiConfig, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ui_Screen_WifiConfig, LV_OPA_COVER, 0);

    lv_obj_t *cont = lv_obj_create(ui_Screen_WifiConfig);
    lv_obj_remove_style_all(cont);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));

    s_wifi_list = lv_obj_create(cont);
    lv_obj_set_width(s_wifi_list, lv_pct(60));
    lv_obj_set_height(s_wifi_list, lv_pct(100));
    lv_obj_set_layout(s_wifi_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(s_wifi_list, &s_style_wifi_list_bg, 0);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_AUTO);
    wifi_config_refresh_list();

    lv_obj_t *side = lv_obj_create(cont);
    lv_obj_remove_style_all(side);
    lv_obj_set_width(side, lv_pct(40));
    lv_obj_set_height(side, lv_pct(100));
    lv_obj_set_layout(side, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);

    s_wifi_status_label = lv_label_create(side);
    lv_label_set_long_mode(s_wifi_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi_status_label, lv_pct(100));
    {
        char ssid_buf[33];
        wifi_get_curr_ssid(ssid_buf, sizeof(ssid_buf));
        lv_label_set_text_fmt(s_wifi_status_label, "%s",
                              wifi_is_connected() ? ssid_buf : tr(I18N_WIFI_NOT_CONN));
    }
    lv_obj_set_style_text_color(s_wifi_status_label, lv_color_make(0xc0, 0xc0, 0xc0), 0);
    lv_obj_set_style_text_font(s_wifi_status_label, i18n_font(), 0);

    lv_obj_t *scan_btn = lv_btn_create(side);
    lv_obj_set_size(scan_btn, lv_pct(100), 24);
    lv_obj_t *scan_l = lv_label_create(scan_btn);
    lv_label_set_text(scan_l, tr(I18N_WIFI_SCAN_BTN));
    lv_obj_set_style_text_font(scan_l, i18n_font(), 0);
    lv_obj_center(scan_l);
    lv_obj_add_event_cb(scan_btn, ui_event_wifi_scan_btn, LV_EVENT_CLICKED, NULL);

    lv_obj_t *conn_btn = lv_btn_create(side);
    lv_obj_set_size(conn_btn, lv_pct(100), 24);
    lv_obj_add_style(conn_btn, &s_style_connect_btn, 0);
    lv_obj_t *conn_l = lv_label_create(conn_btn);
    lv_label_set_text(conn_l, tr(I18N_WIFI_CONNECT_BTN));
    lv_obj_set_style_text_color(conn_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(conn_l, i18n_font(), 0);
    lv_obj_center(conn_l);
    lv_obj_add_event_cb(conn_btn, ui_event_wifi_connect, LV_EVENT_CLICKED, NULL);

    lv_obj_t *forget_btn = lv_btn_create(side);
    lv_obj_set_size(forget_btn, lv_pct(100), 24);
    lv_obj_add_style(forget_btn, &s_style_forget_btn, 0);
    lv_obj_t *forget_l = lv_label_create(forget_btn);
    lv_label_set_text(forget_l, tr(I18N_WIFI_FORGET_BTN));
    lv_obj_set_style_text_color(forget_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(forget_l, i18n_font(), 0);
    lv_obj_center(forget_l);
    lv_obj_add_event_cb(forget_btn, ui_event_wifi_forget, LV_EVENT_CLICKED, NULL);

    s_provision_btn = lv_btn_create(side);
    lv_obj_set_size(s_provision_btn, lv_pct(100), 24);
    lv_obj_add_style(s_provision_btn, &s_style_provision_btn, 0);
    lv_obj_t *prov_l = lv_label_create(s_provision_btn);
    lv_label_set_text(prov_l, "AP Setup");
    lv_obj_set_style_text_color(prov_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(prov_l, i18n_font(), 0);
    lv_obj_center(prov_l);
    lv_obj_add_event_cb(s_provision_btn, ui_event_wifi_provision, LV_EVENT_CLICKED, NULL);

    event_bus_subscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_SCAN_STARTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_SCAN_DONE, s_on_wifi_event, NULL);

    s_wifi_status_timer = lv_timer_create(s_wifi_status_timer_cb, 1000, NULL);
    wifi_config_refresh_status();
}

void ui_Screen_WifiConfig_screen_cleanup(void)
{
    event_bus_unsubscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event);
    event_bus_unsubscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event);
    event_bus_unsubscribe(EVENT_WIFI_SCAN_STARTED, s_on_wifi_event);
    event_bus_unsubscribe(EVENT_WIFI_SCAN_DONE, s_on_wifi_event);

    if (s_wifi_status_timer) {
        lv_timer_del(s_wifi_status_timer);
        s_wifi_status_timer = NULL;
    }

    if (ui_Screen_WifiConfig) {
        lv_obj_del(ui_Screen_WifiConfig);
        ui_Screen_WifiConfig = NULL;
    }

    s_wifi_status_label = NULL;
    s_wifi_list = NULL;
    s_kb_overlay = NULL;
    s_kb_textarea = NULL;
    s_provision_btn = NULL;
    g_wifi_sel = -1;
    g_kb_ssid[0] = '\0';
}
