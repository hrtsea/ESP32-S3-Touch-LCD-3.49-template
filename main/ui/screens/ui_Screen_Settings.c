#include "../ui_events.h"
#include "../ui_helpers.h"
#include "../../network/wifi_manager.h"
#include "../../config/app_cfg.h"
#include "../../utils/event_bus.h"
#include "../ui_clock.h"
#include "i18n.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"

LV_FONT_DECLARE(lv_font_montserrat_32);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(ui_font_NotoSanThai16);

LV_IMG_DECLARE(ui_img_images_globe_png);
LV_IMG_DECLARE(ui_img_images_timezone_png);
LV_IMG_DECLARE(ui_img_images_button_png);

static const char *TAG = "ui_Screen_Settings";

#define MENU_BACK_DEBOUNCE_MS  350

static int  g_wifi_sel          = -1;
static char g_kb_ssid[33]       = {0};
static lv_timer_t *s_wifi_status_timer = NULL;

static lv_obj_t *s_wifi_list = NULL;
static lv_obj_t *s_wifi_status_label = NULL;
static lv_obj_t *s_kb_overlay = NULL;
static lv_obj_t *s_kb_textarea = NULL;

static lv_style_t s_style_wifi_list_bg;
static lv_style_t s_style_connect_btn;
static lv_style_t s_style_forget_btn;
static lv_style_t s_style_scan_btn;
static lv_style_t s_style_label_dim;
static lv_style_t s_style_submenu_cont;
static bool s_styles_inited = false;

static void init_wifi_styles(void)
{
    if (s_styles_inited) return;

    lv_style_init(&s_style_wifi_list_bg);
    lv_style_set_bg_color(&s_style_wifi_list_bg, lv_color_make(0x10, 0x10, 0x14));
    lv_style_set_pad_row(&s_style_wifi_list_bg, 2);
    lv_style_set_pad_all(&s_style_wifi_list_bg, 2);

    lv_style_init(&s_style_connect_btn);
    lv_style_set_bg_color(&s_style_connect_btn, lv_color_make(0x20, 0x80, 0x40));

    lv_style_init(&s_style_forget_btn);
    lv_style_set_bg_color(&s_style_forget_btn, lv_color_make(0x80, 0x40, 0x20));

    lv_style_init(&s_style_scan_btn);
    lv_style_set_bg_color(&s_style_scan_btn, lv_color_make(0x40, 0x60, 0xa0));

    lv_style_init(&s_style_label_dim);
    lv_style_set_text_color(&s_style_label_dim, lv_color_make(0xa0, 0xa0, 0xa0));

    lv_style_init(&s_style_submenu_cont);
    lv_style_set_layout(&s_style_submenu_cont, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&s_style_submenu_cont, LV_FLEX_FLOW_COLUMN);
    lv_style_set_pad_row(&s_style_submenu_cont, 6);

    s_styles_inited = true;
}

static void refresh_wifi_status_text(void)
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
            snprintf(status_buf, sizeof(status_buf), "Connecting %s... (%ds)",
                     ssid_buf, (unsigned)(elapsed / 1000));
            lv_label_set_text(s_wifi_status_label, status_buf);
        }
    } else {
        lv_label_set_text(s_wifi_status_label, "Not connected");
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

static void kb_close(void)
{
    if (s_kb_overlay) {
        lv_obj_del_async(s_kb_overlay);
        s_kb_overlay = NULL;
        s_kb_textarea = NULL;
    }
    clock_ms_timer_resume();
}

static void ui_event_Settings_kb_event(lv_event_t *e)
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
        if (s_wifi_status_label) lv_label_set_text_fmt(s_wifi_status_label, "Connecting %s...", ssid);
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

static void ui_event_Settings_kb_eye_toggle(lv_event_t *e)
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

    lv_obj_t *scr = lv_scr_act();
    s_kb_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_kb_overlay);
    lv_obj_set_size(s_kb_overlay, 640, 172);
    lv_obj_set_style_bg_color(s_kb_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_kb_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(s_kb_overlay, LV_OBJ_FLAG_SCROLLABLE);

    const int TA_H  = 20;
    const int EYE_W = 28;
    s_kb_textarea = lv_textarea_create(s_kb_overlay);
    lv_textarea_set_one_line(s_kb_textarea, true);
    lv_textarea_set_password_mode(s_kb_textarea, true);
    char ph[64];
    snprintf(ph, sizeof(ph), "Password for %s", ssid);
    lv_textarea_set_placeholder_text(s_kb_textarea, ph);
    lv_obj_set_size(s_kb_textarea, 640 - EYE_W, TA_H);
    lv_obj_align(s_kb_textarea, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(s_kb_textarea, &lv_font_montserrat_18, 0);
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
    lv_obj_set_style_text_font(eye_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(eye_lbl);
    lv_obj_add_event_cb(eye, ui_event_Settings_kb_eye_toggle, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(s_kb_overlay);
    lv_obj_set_width(kb, 640);
    lv_obj_set_height(kb, 172 - TA_H);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_row(kb, 2, 0);
    lv_obj_set_style_pad_column(kb, 2, 0);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER,
                        (const char **)kKbMapLower, kKbCtrlLower);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER,
                        (const char **)kKbMapUpper, kKbCtrlUpper);
    lv_keyboard_set_textarea(kb, s_kb_textarea);
    lv_obj_add_event_cb(kb, ui_event_Settings_kb_event, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, ui_event_Settings_kb_event, LV_EVENT_CANCEL, NULL);
}

static void ui_event_Settings_wifi_ap(lv_event_t *e)
{
    if (ui_helpers_menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)wifi_get_scan_count()) return;
    g_wifi_sel = idx;
}

static void ui_event_Settings_wifi_connect(lv_event_t *e)
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
        if (s_wifi_status_label) lv_label_set_text_fmt(s_wifi_status_label, "Connecting %s...", ap->ssid);
        return;
    }
    char pass[65] = {0};
    if (app_cfg_get_ssid_pass(ap->ssid, pass, sizeof(pass))) {
        app_cfg_wifi_pending_set(ap->ssid, pass);
        wifi_connect(ap->ssid, pass);
        if (s_wifi_status_label) lv_label_set_text_fmt(s_wifi_status_label, "Connecting %s...", ap->ssid);
        return;
    }
    kb_open_for_ssid(ap->ssid);
}

static void ui_event_Settings_wifi_forget(lv_event_t *e)
{
    (void)e;
    int idx = g_wifi_sel;
    if (idx < 0 || idx >= (int)wifi_get_scan_count()) return;
    const wifi_scan_ap_t *ap = wifi_get_scan_ap((uint16_t)idx);
    if (!ap) return;
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK) {
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
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, "Not connected");
}

static void set_render_wifi_list(void)
{
    if (!s_wifi_list) return;
    lv_obj_clean(s_wifi_list);
    uint16_t scan_n = wifi_get_scan_count();
    if (scan_n == 0) {
        lv_obj_t *empty = lv_label_create(s_wifi_list);
        lv_label_set_text(empty, "No networks found");
        lv_obj_add_style(empty, &s_style_label_dim, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_18, 0);
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
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_add_event_cb(btn, ui_event_Settings_wifi_ap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
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
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void ui_Settings_scan_refresh_timer_cb(lv_timer_t *tt)
{
    if (s_wifi_status_label) {
        lv_label_set_text_fmt(s_wifi_status_label, "Found %u networks", (unsigned)wifi_get_scan_count());
    }
    set_render_wifi_list();
    lv_timer_del(tt);
}

static void ui_event_Settings_scan_btn(lv_event_t *e)
{
    (void)e;
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, "Scanning...");
    wifi_start_scan();
    lv_timer_t *t = lv_timer_create(ui_Settings_scan_refresh_timer_cb, 3000, NULL);
    (void)t;
}

lv_obj_t * ui_Screen_Settings = NULL;
lv_obj_t * ui_MainMenu_Tabview_ConfigPanel = NULL;
lv_obj_t * ui_MainMenu_Tabpage_network = NULL;
lv_obj_t * ui_MainMenu_Label_connectStatus = NULL;
lv_obj_t * ui_MainMenu_Label_Label3 = NULL;
lv_obj_t * ui_MainMenu_Dropdown_NetworkList = NULL;
lv_obj_t * ui_MainMenu_Textarea_Password = NULL;
lv_obj_t * ui_MainMenu_Button_NetworkSave = NULL;
lv_obj_t * ui_MainMenu_Label_Label12 = NULL;
lv_obj_t * ui_MainMenu_Button_NetworkScan = NULL;
lv_obj_t * ui_MainMenu_Label_Label13 = NULL;
lv_obj_t * ui_MainMenu_Switch_Wifi = NULL;
lv_obj_t * ui_MainMenu_Tabpage_screen = NULL;
lv_obj_t * ui_MainMenu_Dropdown_Brightness = NULL;
lv_obj_t * ui_MainMenu_Dropdown_SleepTimer = NULL;
lv_obj_t * ui_MainMenu_Dropdown_Wallpaper = NULL;
lv_obj_t * ui_MainMenu_Label_Label8 = NULL;
lv_obj_t * ui_MainMenu_Tabpage_station = NULL;
lv_obj_t * ui_MainMenu_Textarea_stationList = NULL;
lv_obj_t * ui_MainMenu_Label_Label10 = NULL;
lv_obj_t * ui_MainMenu_Button_LoadStation = NULL;
lv_obj_t * ui_MainMenu_Label_Label20 = NULL;
lv_obj_t * ui_MainMenu_Tabpage_music = NULL;
lv_obj_t * ui_MainMenu_Button_scanMusic = NULL;
lv_obj_t * ui_MainMenu_Label_Label14 = NULL;
lv_obj_t * ui_MainMenu_Label_Label15 = NULL;
lv_obj_t * ui_MainMenu_Label_trackCount = NULL;
lv_obj_t * ui_MainMenu_Tabpage_region = NULL;
lv_obj_t * ui_MainMenu_Image_Globe = NULL;
lv_obj_t * ui_MainMenu_Checkbox_AutoIP = NULL;
lv_obj_t * ui_MainMenu_Textarea_Latitude = NULL;
lv_obj_t * ui_MainMenu_Textarea_Longitude = NULL;
lv_obj_t * ui_MainMenu_Image_timezone = NULL;
lv_obj_t * ui_MainMenu_Roller_Hour = NULL;
lv_obj_t * ui_MainMenu_Roller_Minute = NULL;
lv_obj_t * ui_MainMenu_Roller_Unit = NULL;
lv_obj_t * ui_MainMenu_Label_Label25 = NULL;
lv_obj_t * ui_MainMenu_Tabpage_guide = NULL;
lv_obj_t * ui_MainMenu_Textarea_UserGuide = NULL;
lv_obj_t * ui_MainMenu_Image_buttons = NULL;
lv_obj_t * ui_MainMenu_Button_closeConfig = NULL;
lv_obj_t * ui_MainMenu_Label_Label9 = NULL;
lv_obj_t * ui_MainMenu_Keyboard_Keyboard1 = NULL;
lv_obj_t * ui_MainMenu_Keyboard_Number = NULL;
lv_obj_t * ui_MainMenu_Panel_blindPanel = NULL;

void ui_event_MainMenu_Tabview_ConfigPanel(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        resetScreenOffTimer(e);
    }
    if(event_code == LV_EVENT_VALUE_CHANGED) {
        resetScreenOffTimer(e);
    }
}

void ui_event_MainMenu_Tabpage_network(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        resetScreenOffTimer(e);
    }
}

void ui_event_MainMenu_Textarea_Password(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        _ui_flag_modify(ui_MainMenu_Keyboard_Keyboard1, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

void ui_event_MainMenu_Button_NetworkSave(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        saveWiFiCredential(e);
    }
}

void ui_event_MainMenu_Button_NetworkScan(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        scanNetwork(e);
    }
}

void ui_event_MainMenu_Switch_Wifi(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        toggleWiFi(e);
    }
}

void ui_event_MainMenu_Tabpage_screen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        resetScreenOffTimer(e);
    }
}

void ui_event_MainMenu_Dropdown_Brightness(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setBrightness(e);
    }
}

void ui_event_MainMenu_Dropdown_SleepTimer(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setTimer(e);
    }
}

void ui_event_MainMenu_Dropdown_Wallpaper(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setWallpaper(e);
    }
}

void ui_event_MainMenu_Tabpage_station(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        resetScreenOffTimer(e);
    }
}

void ui_event_MainMenu_Button_LoadStation(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        loadStationFromSDCARD(e);
    }
}

void ui_event_MainMenu_Tabpage_music(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        resetScreenOffTimer(e);
    }
}

void ui_event_MainMenu_Button_scanMusic(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        loadMusicFromSDCARD(e);
    }
}

void ui_event_MainMenu_Tabpage_region(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        resetScreenOffTimer(e);
    }
}

void ui_event_MainMenu_Checkbox_AutoIP(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);

    if(event_code == LV_EVENT_VALUE_CHANGED &&  lv_obj_has_state(target, LV_STATE_CHECKED)) {
        set_query_para_autoip(e);
        _ui_flag_modify(ui_MainMenu_Textarea_Latitude, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
        _ui_flag_modify(ui_MainMenu_Textarea_Longitude, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
    }
    if(event_code == LV_EVENT_VALUE_CHANGED &&  !lv_obj_has_state(target, LV_STATE_CHECKED)) {
        _ui_flag_modify(ui_MainMenu_Textarea_Latitude, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
        _ui_flag_modify(ui_MainMenu_Textarea_Longitude, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

void ui_event_MainMenu_Textarea_Latitude(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        _ui_keyboard_set_target(ui_MainMenu_Keyboard_Number,  ui_MainMenu_Textarea_Latitude);
        _ui_flag_modify(ui_MainMenu_Keyboard_Number, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

void ui_event_MainMenu_Textarea_Longitude(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        _ui_keyboard_set_target(ui_MainMenu_Keyboard_Number,  ui_MainMenu_Textarea_Longitude);
        _ui_flag_modify(ui_MainMenu_Keyboard_Number, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

void ui_event_MainMenu_Roller_Hour(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setOffsetHour(e);
    }
}

void ui_event_MainMenu_Roller_Minute(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setOffsetMinute(e);
    }
}

void ui_event_MainMenu_Roller_Unit(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setTempUnit(e);
    }
}

void ui_event_MainMenu_Tabpage_guide(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        resetScreenOffTimer(e);
    }
}

void ui_event_MainMenu_Textarea_UserGuide(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        resetScreenOffTimer(e);
    }
}

void ui_event_MainMenu_Button_closeConfig(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        saveConfig(e);
        if (ui_Screen_Overview != NULL) {
            lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, false);
        }
    }
}

void ui_event_MainMenu_Keyboard_Keyboard1(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t* keyboard = lv_event_get_target(e);

    if(event_code == LV_EVENT_READY) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    else if(event_code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_event_MainMenu_Keyboard_Number(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t* keyboard = lv_event_get_target(e);

    if(event_code == LV_EVENT_READY) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    else if(event_code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_event_MainMenu_Panel_blindPanel(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        turnonScreen(e);
    }
}

void ui_Screen_Settings_screen_init(void)
{
    ui_Screen_Settings = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen_Settings,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_text_font(ui_Screen_Settings, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_Screen_Settings, LV_OBJ_FLAG_GESTURE_BUBBLE);
    ESP_LOGI(TAG, "Added GESTURE_BUBBLE flag for Settings screen");

    ui_MainMenu_Tabview_ConfigPanel = lv_tabview_create(ui_Screen_Settings, LV_DIR_BOTTOM, 40);
    lv_obj_set_width(ui_MainMenu_Tabview_ConfigPanel, lv_pct(100));
    lv_obj_set_height(ui_MainMenu_Tabview_ConfigPanel, lv_pct(100));
    lv_obj_set_align(ui_MainMenu_Tabview_ConfigPanel, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_MainMenu_Tabview_ConfigPanel,
                      LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                      LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_bg_color(ui_MainMenu_Tabview_ConfigPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Tabview_ConfigPanel, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_MainMenu_Tabview_ConfigPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MainMenu_Tabview_ConfigPanel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), lv_color_hex(0x00ADFF),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), 255,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), lv_color_hex(0x025074),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), LV_GRAD_DIR_VER,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), lv_color_hex(0x000000),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), 255,
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), lv_color_hex(0xFFFFFF),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), 255,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), lv_color_hex(0x9D9C9C),
                                   LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), LV_GRAD_DIR_VER,
                                 LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), lv_color_hex(0x000000),
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), 255,
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), 2,
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), lv_color_hex(0xFFFFFF),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(lv_tabview_get_tab_btns(ui_MainMenu_Tabview_ConfigPanel), 255,
                              LV_PART_ITEMS | LV_STATE_CHECKED);

    ui_MainMenu_Tabpage_network = lv_tabview_add_tab(ui_MainMenu_Tabview_ConfigPanel, "Wi-Fi");
    lv_obj_set_scrollbar_mode(ui_MainMenu_Tabpage_network, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_MainMenu_Tabpage_network, LV_DIR_HOR);

    ui_MainMenu_Label_connectStatus = lv_label_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(ui_MainMenu_Label_connectStatus, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_connectStatus, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Label_connectStatus, 60);
    lv_obj_set_y(ui_MainMenu_Label_connectStatus, 12);
    lv_obj_set_align(ui_MainMenu_Label_connectStatus, LV_ALIGN_BOTTOM_LEFT);
    lv_label_set_text(ui_MainMenu_Label_connectStatus, "Disconnected");
    lv_obj_set_style_text_color(ui_MainMenu_Label_connectStatus, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MainMenu_Label_connectStatus, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Label_Label3 = lv_label_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(ui_MainMenu_Label_Label3, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label3, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Label_Label3, -11);
    lv_obj_set_y(ui_MainMenu_Label_Label3, -2);
    lv_label_set_text(ui_MainMenu_Label_Label3,
                      "SSID:                                                                                        Max 10\n-------																							                                          Slot\nPass:                                                                                        \n-------                                                                                       Wi-Fi\nStatus:");
    lv_obj_clear_flag(ui_MainMenu_Label_Label3,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_text_letter_space(ui_MainMenu_Label_Label3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui_MainMenu_Label_Label3, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Dropdown_NetworkList = lv_dropdown_create(ui_MainMenu_Tabpage_network);
    lv_dropdown_set_options(ui_MainMenu_Dropdown_NetworkList, "");
    lv_obj_set_width(ui_MainMenu_Dropdown_NetworkList, 260);
    lv_obj_set_height(ui_MainMenu_Dropdown_NetworkList, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Dropdown_NetworkList, 50);
    lv_obj_set_y(ui_MainMenu_Dropdown_NetworkList, -15);
    lv_obj_add_flag(ui_MainMenu_Dropdown_NetworkList, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_MainMenu_Textarea_Password = lv_textarea_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(ui_MainMenu_Textarea_Password, 260);
    lv_obj_set_height(ui_MainMenu_Textarea_Password, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Textarea_Password, 50);
    lv_obj_set_y(ui_MainMenu_Textarea_Password, 38);
    lv_textarea_set_max_length(ui_MainMenu_Textarea_Password, 32);
    lv_textarea_set_placeholder_text(ui_MainMenu_Textarea_Password, "Wi-Fi Password");
    lv_textarea_set_one_line(ui_MainMenu_Textarea_Password, true);

    lv_obj_set_style_border_color(ui_MainMenu_Textarea_Password, lv_color_hex(0x000000), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ui_MainMenu_Textarea_Password, 255, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ui_MainMenu_Textarea_Password, 1, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ui_MainMenu_Textarea_Password, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);

    ui_MainMenu_Button_NetworkSave = lv_btn_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(ui_MainMenu_Button_NetworkSave, 150);
    lv_obj_set_height(ui_MainMenu_Button_NetworkSave, 50);
    lv_obj_set_x(ui_MainMenu_Button_NetworkSave, 85);
    lv_obj_set_y(ui_MainMenu_Button_NetworkSave, 10);
    lv_obj_set_align(ui_MainMenu_Button_NetworkSave, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_MainMenu_Button_NetworkSave, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_MainMenu_Button_NetworkSave, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_MainMenu_Button_NetworkSave, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Button_NetworkSave, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MainMenu_Button_NetworkSave, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MainMenu_Button_NetworkSave, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MainMenu_Button_NetworkSave, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Label_Label12 = lv_label_create(ui_MainMenu_Button_NetworkSave);
    lv_obj_set_width(ui_MainMenu_Label_Label12, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label12, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_MainMenu_Label_Label12, LV_ALIGN_CENTER);
    lv_label_set_text(ui_MainMenu_Label_Label12, "D Save");
    lv_obj_set_style_text_font(ui_MainMenu_Label_Label12, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Button_NetworkScan = lv_btn_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(ui_MainMenu_Button_NetworkScan, 150);
    lv_obj_set_height(ui_MainMenu_Button_NetworkScan, 50);
    lv_obj_set_x(ui_MainMenu_Button_NetworkScan, 85);
    lv_obj_set_y(ui_MainMenu_Button_NetworkScan, -42);
    lv_obj_set_align(ui_MainMenu_Button_NetworkScan, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_MainMenu_Button_NetworkScan, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_MainMenu_Button_NetworkScan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_MainMenu_Button_NetworkScan, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Button_NetworkScan, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MainMenu_Button_NetworkScan, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MainMenu_Button_NetworkScan, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MainMenu_Button_NetworkScan, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Label_Label13 = lv_label_create(ui_MainMenu_Button_NetworkScan);
    lv_obj_set_width(ui_MainMenu_Label_Label13, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label13, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_MainMenu_Label_Label13, LV_ALIGN_CENTER);
    lv_label_set_text(ui_MainMenu_Label_Label13, "W Scan");
    lv_obj_set_style_text_font(ui_MainMenu_Label_Label13, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Switch_Wifi = lv_switch_create(ui_MainMenu_Tabpage_network);
    lv_obj_add_state(ui_MainMenu_Switch_Wifi, LV_STATE_CHECKED);
    lv_obj_set_width(ui_MainMenu_Switch_Wifi, 60);
    lv_obj_set_height(ui_MainMenu_Switch_Wifi, 29);
    lv_obj_set_x(ui_MainMenu_Switch_Wifi, 0);
    lv_obj_set_y(ui_MainMenu_Switch_Wifi, -9);
    lv_obj_set_align(ui_MainMenu_Switch_Wifi, LV_ALIGN_BOTTOM_RIGHT);

    lv_obj_set_style_bg_color(ui_MainMenu_Switch_Wifi, lv_color_hex(0x088CFD), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(ui_MainMenu_Switch_Wifi, 255, LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_set_style_bg_color(ui_MainMenu_Switch_Wifi, lv_color_hex(0x0845FA), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Switch_Wifi, 255, LV_PART_KNOB | LV_STATE_DEFAULT);

    init_wifi_styles();

    s_wifi_status_label = lv_label_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(s_wifi_status_label, lv_pct(100));
    lv_obj_set_height(s_wifi_status_label, 24);
    lv_obj_set_y(s_wifi_status_label, 60);
    lv_label_set_text(s_wifi_status_label, "Not connected");
    lv_obj_add_style(s_wifi_status_label, &s_style_label_dim, 0);
    lv_obj_set_style_text_font(s_wifi_status_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(s_wifi_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_left(s_wifi_status_label, 8, 0);

    s_wifi_list = lv_list_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(s_wifi_list, lv_pct(100));
    lv_obj_set_height(s_wifi_list, 200);
    lv_obj_set_y(s_wifi_list, 84);
    lv_obj_add_style(s_wifi_list, &s_style_wifi_list_bg, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 2, 0);
    lv_obj_set_style_pad_left(s_wifi_list, 0, 0);
    lv_obj_set_style_pad_right(s_wifi_list, 0, 0);
    lv_obj_set_style_bg_color(s_wifi_list, lv_color_make(0x10, 0x10, 0x14), 0);

    lv_obj_t *wifi_btn_connect = lv_btn_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(wifi_btn_connect, 140);
    lv_obj_set_height(wifi_btn_connect, 40);
    lv_obj_set_x(wifi_btn_connect, 8);
    lv_obj_set_y(wifi_btn_connect, 290);
    lv_obj_add_style(wifi_btn_connect, &s_style_connect_btn, 0);
    lv_obj_add_event_cb(wifi_btn_connect, ui_event_Settings_wifi_connect, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wifi_btn_connect_label = lv_label_create(wifi_btn_connect);
    lv_label_set_text(wifi_btn_connect_label, LV_SYMBOL_WIFI " Connect");
    lv_obj_set_style_text_font(wifi_btn_connect_label, &lv_font_montserrat_18, 0);
    lv_obj_align(wifi_btn_connect_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *wifi_btn_forget = lv_btn_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(wifi_btn_forget, 140);
    lv_obj_set_height(wifi_btn_forget, 40);
    lv_obj_set_x(wifi_btn_forget, 156);
    lv_obj_set_y(wifi_btn_forget, 290);
    lv_obj_add_style(wifi_btn_forget, &s_style_forget_btn, 0);
    lv_obj_add_event_cb(wifi_btn_forget, ui_event_Settings_wifi_forget, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wifi_btn_forget_label = lv_label_create(wifi_btn_forget);
    lv_label_set_text(wifi_btn_forget_label, LV_SYMBOL_TRASH " Forget");
    lv_obj_set_style_text_font(wifi_btn_forget_label, &lv_font_montserrat_18, 0);
    lv_obj_align(wifi_btn_forget_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *wifi_btn_scan = lv_btn_create(ui_MainMenu_Tabpage_network);
    lv_obj_set_width(wifi_btn_scan, 140);
    lv_obj_set_height(wifi_btn_scan, 40);
    lv_obj_set_x(wifi_btn_scan, 304);
    lv_obj_set_y(wifi_btn_scan, 290);
    lv_obj_add_style(wifi_btn_scan, &s_style_scan_btn, 0);
    lv_obj_add_event_cb(wifi_btn_scan, ui_event_Settings_scan_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wifi_btn_scan_label = lv_label_create(wifi_btn_scan);
    lv_label_set_text(wifi_btn_scan_label, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_font(wifi_btn_scan_label, &lv_font_montserrat_18, 0);
    lv_obj_align(wifi_btn_scan_label, LV_ALIGN_CENTER, 0, 0);

    ui_MainMenu_Tabpage_screen = lv_tabview_add_tab(ui_MainMenu_Tabview_ConfigPanel, "Screen");
    lv_obj_set_scrollbar_mode(ui_MainMenu_Tabpage_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_MainMenu_Tabpage_screen, LV_DIR_HOR);

    ui_MainMenu_Dropdown_Brightness = lv_dropdown_create(ui_MainMenu_Tabpage_screen);
    lv_dropdown_set_options(ui_MainMenu_Dropdown_Brightness, "LOW\nMEDIUM\nHIGH");
    lv_obj_set_width(ui_MainMenu_Dropdown_Brightness, 150);
    lv_obj_set_height(ui_MainMenu_Dropdown_Brightness, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Dropdown_Brightness, 0);
    lv_obj_set_y(ui_MainMenu_Dropdown_Brightness, 15);
    lv_obj_add_flag(ui_MainMenu_Dropdown_Brightness, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_MainMenu_Dropdown_SleepTimer = lv_dropdown_create(ui_MainMenu_Tabpage_screen);
    lv_dropdown_set_options(ui_MainMenu_Dropdown_SleepTimer, "0\n15\n30\n60");
    lv_obj_set_width(ui_MainMenu_Dropdown_SleepTimer, 150);
    lv_obj_set_height(ui_MainMenu_Dropdown_SleepTimer, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Dropdown_SleepTimer, 180);
    lv_obj_set_y(ui_MainMenu_Dropdown_SleepTimer, 15);
    lv_obj_add_flag(ui_MainMenu_Dropdown_SleepTimer, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_MainMenu_Dropdown_Wallpaper = lv_dropdown_create(ui_MainMenu_Tabpage_screen);
    lv_dropdown_set_options(ui_MainMenu_Dropdown_Wallpaper, "");
    lv_obj_set_width(ui_MainMenu_Dropdown_Wallpaper, 150);
    lv_obj_set_height(ui_MainMenu_Dropdown_Wallpaper, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Dropdown_Wallpaper, 360);
    lv_obj_set_y(ui_MainMenu_Dropdown_Wallpaper, 15);
    lv_obj_add_flag(ui_MainMenu_Dropdown_Wallpaper, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_MainMenu_Label_Label8 = lv_label_create(ui_MainMenu_Tabpage_screen);
    lv_obj_set_width(ui_MainMenu_Label_Label8, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label8, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Label_Label8, 26);
    lv_obj_set_y(ui_MainMenu_Label_Label8, -15);
    lv_label_set_text(ui_MainMenu_Label_Label8, "Brightness        Screen off timer (sec)     Wallpaper");

    ui_MainMenu_Tabpage_station = lv_tabview_add_tab(ui_MainMenu_Tabview_ConfigPanel, "Radio");
    lv_obj_set_scrollbar_mode(ui_MainMenu_Tabpage_station, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_MainMenu_Tabpage_station, LV_DIR_HOR);

    ui_MainMenu_Textarea_stationList = lv_textarea_create(ui_MainMenu_Tabpage_station);
    lv_obj_set_width(ui_MainMenu_Textarea_stationList, 320);
    lv_obj_set_height(ui_MainMenu_Textarea_stationList, 128);
    lv_obj_set_x(ui_MainMenu_Textarea_stationList, -12);
    lv_obj_set_y(ui_MainMenu_Textarea_stationList, -16);
    lv_textarea_set_placeholder_text(ui_MainMenu_Textarea_stationList, "Station URL List");
    lv_obj_set_scroll_dir(ui_MainMenu_Textarea_stationList, LV_DIR_VER);
    lv_obj_set_style_text_color(ui_MainMenu_Textarea_stationList, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MainMenu_Textarea_stationList, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_MainMenu_Textarea_stationList, &ui_font_NotoSanThai16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MainMenu_Textarea_stationList, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Textarea_stationList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Label_Label10 = lv_label_create(ui_MainMenu_Tabpage_station);
    lv_obj_set_width(ui_MainMenu_Label_Label10, 300);
    lv_obj_set_height(ui_MainMenu_Label_Label10, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Label_Label10, 11);
    lv_obj_set_y(ui_MainMenu_Label_Label10, 13);
    lv_obj_set_align(ui_MainMenu_Label_Label10, LV_ALIGN_BOTTOM_RIGHT);
    lv_label_set_text(ui_MainMenu_Label_Label10,
                      "Insert a microSD card with filename\n[ stations.csv ]  click Load.\nDefaults will be used\nif the file is unavailable.");
    lv_obj_set_scrollbar_mode(ui_MainMenu_Label_Label10, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_font(ui_MainMenu_Label_Label10, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Button_LoadStation = lv_btn_create(ui_MainMenu_Tabpage_station);
    lv_obj_set_width(ui_MainMenu_Button_LoadStation, 144);
    lv_obj_set_height(ui_MainMenu_Button_LoadStation, 50);
    lv_obj_set_x(ui_MainMenu_Button_LoadStation, 88);
    lv_obj_set_y(ui_MainMenu_Button_LoadStation, -13);
    lv_obj_set_align(ui_MainMenu_Button_LoadStation, LV_ALIGN_TOP_MID);
    lv_obj_add_flag(ui_MainMenu_Button_LoadStation, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_MainMenu_Button_LoadStation, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_MainMenu_Button_LoadStation, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Button_LoadStation, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MainMenu_Button_LoadStation, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MainMenu_Button_LoadStation, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MainMenu_Button_LoadStation, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Label_Label20 = lv_label_create(ui_MainMenu_Button_LoadStation);
    lv_obj_set_width(ui_MainMenu_Label_Label20, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label20, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Label_Label20, 1);
    lv_obj_set_y(ui_MainMenu_Label_Label20, 0);
    lv_obj_set_align(ui_MainMenu_Label_Label20, LV_ALIGN_CENTER);
    lv_label_set_text(ui_MainMenu_Label_Label20, "D Load");
    lv_obj_set_style_text_font(ui_MainMenu_Label_Label20, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Tabpage_music = lv_tabview_add_tab(ui_MainMenu_Tabview_ConfigPanel, "Music");
    lv_obj_set_scrollbar_mode(ui_MainMenu_Tabpage_music, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_MainMenu_Tabpage_music, LV_DIR_HOR);

    ui_MainMenu_Button_scanMusic = lv_btn_create(ui_MainMenu_Tabpage_music);
    lv_obj_set_width(ui_MainMenu_Button_scanMusic, 144);
    lv_obj_set_height(ui_MainMenu_Button_scanMusic, 50);
    lv_obj_set_x(ui_MainMenu_Button_scanMusic, -6);
    lv_obj_set_y(ui_MainMenu_Button_scanMusic, 0);
    lv_obj_set_align(ui_MainMenu_Button_scanMusic, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_add_flag(ui_MainMenu_Button_scanMusic, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_MainMenu_Button_scanMusic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_MainMenu_Button_scanMusic, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Button_scanMusic, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MainMenu_Button_scanMusic, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MainMenu_Button_scanMusic, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MainMenu_Button_scanMusic, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Label_Label14 = lv_label_create(ui_MainMenu_Button_scanMusic);
    lv_obj_set_width(ui_MainMenu_Label_Label14, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label14, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Label_Label14, 1);
    lv_obj_set_y(ui_MainMenu_Label_Label14, 0);
    lv_obj_set_align(ui_MainMenu_Label_Label14, LV_ALIGN_CENTER);
    lv_label_set_text(ui_MainMenu_Label_Label14, "D Load");
    lv_obj_set_style_text_font(ui_MainMenu_Label_Label14, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Label_Label15 = lv_label_create(ui_MainMenu_Tabpage_music);
    lv_obj_set_width(ui_MainMenu_Label_Label15, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label15, LV_SIZE_CONTENT);
    lv_label_set_text(ui_MainMenu_Label_Label15, "Tap  LOAD to refresh the music library from the SD card");

    ui_MainMenu_Label_trackCount = lv_label_create(ui_MainMenu_Tabpage_music);
    lv_obj_set_width(ui_MainMenu_Label_trackCount, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_trackCount, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Label_trackCount, 150);
    lv_obj_set_y(ui_MainMenu_Label_trackCount, -9);
    lv_obj_set_align(ui_MainMenu_Label_trackCount, LV_ALIGN_BOTTOM_LEFT);
    lv_label_set_long_mode(ui_MainMenu_Label_trackCount, LV_LABEL_LONG_SCROLL);
    lv_label_set_text(ui_MainMenu_Label_trackCount, "Card Mount Failed");
    lv_obj_set_style_text_font(ui_MainMenu_Label_trackCount, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Tabpage_region = lv_tabview_add_tab(ui_MainMenu_Tabview_ConfigPanel, "Region");
    lv_obj_set_scrollbar_mode(ui_MainMenu_Tabpage_region, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_MainMenu_Tabpage_region, LV_DIR_HOR);

    ui_MainMenu_Image_Globe = lv_img_create(ui_MainMenu_Tabpage_region);
    lv_img_set_src(ui_MainMenu_Image_Globe, &ui_img_images_globe_png);
    lv_obj_set_width(ui_MainMenu_Image_Globe, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Image_Globe, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Image_Globe, -12);
    lv_obj_set_y(ui_MainMenu_Image_Globe, 0);
    lv_obj_set_align(ui_MainMenu_Image_Globe, LV_ALIGN_LEFT_MID);
    lv_obj_add_flag(ui_MainMenu_Image_Globe, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_MainMenu_Image_Globe, LV_OBJ_FLAG_SCROLLABLE);

    ui_MainMenu_Checkbox_AutoIP = lv_checkbox_create(ui_MainMenu_Tabpage_region);
    lv_checkbox_set_text(ui_MainMenu_Checkbox_AutoIP, "Auto by IP");
    lv_obj_set_width(ui_MainMenu_Checkbox_AutoIP, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Checkbox_AutoIP, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Checkbox_AutoIP, 118);
    lv_obj_set_y(ui_MainMenu_Checkbox_AutoIP, -11);
    lv_obj_add_flag(ui_MainMenu_Checkbox_AutoIP, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_MainMenu_Textarea_Latitude = lv_textarea_create(ui_MainMenu_Tabpage_region);
    lv_obj_set_width(ui_MainMenu_Textarea_Latitude, 150);
    lv_obj_set_height(ui_MainMenu_Textarea_Latitude, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Textarea_Latitude, 117);
    lv_obj_set_y(ui_MainMenu_Textarea_Latitude, 23);
    lv_textarea_set_accepted_chars(ui_MainMenu_Textarea_Latitude, "0123456789.-");
    lv_textarea_set_max_length(ui_MainMenu_Textarea_Latitude, 12);
    lv_textarea_set_placeholder_text(ui_MainMenu_Textarea_Latitude, "Latitude");
    lv_textarea_set_one_line(ui_MainMenu_Textarea_Latitude, true);
    lv_obj_set_scrollbar_mode(ui_MainMenu_Textarea_Latitude, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_color(ui_MainMenu_Textarea_Latitude, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MainMenu_Textarea_Latitude, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_MainMenu_Textarea_Latitude, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_MainMenu_Textarea_Latitude, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_border_color(ui_MainMenu_Textarea_Latitude, lv_color_hex(0x000000), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ui_MainMenu_Textarea_Latitude, 255, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ui_MainMenu_Textarea_Latitude, 1, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ui_MainMenu_Textarea_Latitude, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);

    lv_obj_set_style_text_color(ui_MainMenu_Textarea_Latitude, lv_color_hex(0x555555),
                                LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MainMenu_Textarea_Latitude, 255, LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);

    ui_MainMenu_Textarea_Longitude = lv_textarea_create(ui_MainMenu_Tabpage_region);
    lv_obj_set_width(ui_MainMenu_Textarea_Longitude, 150);
    lv_obj_set_height(ui_MainMenu_Textarea_Longitude, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Textarea_Longitude, 117);
    lv_obj_set_y(ui_MainMenu_Textarea_Longitude, 70);
    lv_textarea_set_accepted_chars(ui_MainMenu_Textarea_Longitude, "0123456789.-");
    lv_textarea_set_max_length(ui_MainMenu_Textarea_Longitude, 12);
    lv_textarea_set_placeholder_text(ui_MainMenu_Textarea_Longitude, "Longitude");
    lv_textarea_set_one_line(ui_MainMenu_Textarea_Longitude, true);
    lv_obj_set_scrollbar_mode(ui_MainMenu_Textarea_Longitude, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_color(ui_MainMenu_Textarea_Longitude, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MainMenu_Textarea_Longitude, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_MainMenu_Textarea_Longitude, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_MainMenu_Textarea_Longitude, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_border_color(ui_MainMenu_Textarea_Longitude, lv_color_hex(0x000000),
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ui_MainMenu_Textarea_Longitude, 255, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ui_MainMenu_Textarea_Longitude, 1, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ui_MainMenu_Textarea_Longitude, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);

    lv_obj_set_style_text_color(ui_MainMenu_Textarea_Longitude, lv_color_hex(0x555555),
                                LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MainMenu_Textarea_Longitude, 255, LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);

    ui_MainMenu_Image_timezone = lv_img_create(ui_MainMenu_Tabpage_region);
    lv_img_set_src(ui_MainMenu_Image_timezone, &ui_img_images_timezone_png);
    lv_obj_set_width(ui_MainMenu_Image_timezone, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Image_timezone, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Image_timezone, 24);
    lv_obj_set_y(ui_MainMenu_Image_timezone, 0);
    lv_obj_set_align(ui_MainMenu_Image_timezone, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_MainMenu_Image_timezone, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_MainMenu_Image_timezone, LV_OBJ_FLAG_SCROLLABLE);

    ui_MainMenu_Roller_Hour = lv_roller_create(ui_MainMenu_Tabpage_region);
    lv_roller_set_options(ui_MainMenu_Roller_Hour,
                          "+14\n+13\n+12\n+11\n+10\n+9\n+8\n+7\n+6\n+5\n+4\n+3\n+2\n+1\n0\n-1\n-2\n-3\n-4\n-5\n-6\n-7\n-8\n-9\n-10\n-11\n-12",
                          LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ui_MainMenu_Roller_Hour, 14, LV_ANIM_OFF);
    lv_obj_set_width(ui_MainMenu_Roller_Hour, 50);
    lv_obj_set_height(ui_MainMenu_Roller_Hour, 100);
    lv_obj_set_x(ui_MainMenu_Roller_Hour, -180);
    lv_obj_set_y(ui_MainMenu_Roller_Hour, 10);
    lv_obj_set_align(ui_MainMenu_Roller_Hour, LV_ALIGN_RIGHT_MID);

    ui_MainMenu_Roller_Minute = lv_roller_create(ui_MainMenu_Tabpage_region);
    lv_roller_set_options(ui_MainMenu_Roller_Minute, "+45\n+30\n+15\n0\n-15\n-30\n-45", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ui_MainMenu_Roller_Minute, 3, LV_ANIM_OFF);
    lv_obj_set_width(ui_MainMenu_Roller_Minute, 50);
    lv_obj_set_height(ui_MainMenu_Roller_Minute, 100);
    lv_obj_set_x(ui_MainMenu_Roller_Minute, -110);
    lv_obj_set_y(ui_MainMenu_Roller_Minute, 10);
    lv_obj_set_align(ui_MainMenu_Roller_Minute, LV_ALIGN_RIGHT_MID);

    ui_MainMenu_Roller_Unit = lv_roller_create(ui_MainMenu_Tabpage_region);
    lv_roller_set_options(ui_MainMenu_Roller_Unit, "°C\n°F", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ui_MainMenu_Roller_Unit, 3, LV_ANIM_OFF);
    lv_obj_set_width(ui_MainMenu_Roller_Unit, 50);
    lv_obj_set_height(ui_MainMenu_Roller_Unit, 100);
    lv_obj_set_x(ui_MainMenu_Roller_Unit, -40);
    lv_obj_set_y(ui_MainMenu_Roller_Unit, 10);
    lv_obj_set_align(ui_MainMenu_Roller_Unit, LV_ALIGN_RIGHT_MID);

    ui_MainMenu_Label_Label25 = lv_label_create(ui_MainMenu_Tabpage_region);
    lv_obj_set_width(ui_MainMenu_Label_Label25, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label25, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Label_Label25, -49);
    lv_obj_set_y(ui_MainMenu_Label_Label25, -17);
    lv_obj_set_align(ui_MainMenu_Label_Label25, LV_ALIGN_TOP_RIGHT);
    lv_label_set_text(ui_MainMenu_Label_Label25, "UTC Offset :Hour   Minute   Unit");
    lv_obj_set_style_text_align(ui_MainMenu_Label_Label25, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Tabpage_guide = lv_tabview_add_tab(ui_MainMenu_Tabview_ConfigPanel, "User Guide");
    lv_obj_set_scrollbar_mode(ui_MainMenu_Tabpage_guide, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_MainMenu_Tabpage_guide, LV_DIR_VER);

    ui_MainMenu_Textarea_UserGuide = lv_textarea_create(ui_MainMenu_Tabpage_guide);
    lv_obj_set_width(ui_MainMenu_Textarea_UserGuide, 640);
    lv_obj_set_height(ui_MainMenu_Textarea_UserGuide, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Textarea_UserGuide, 0);
    lv_obj_set_y(ui_MainMenu_Textarea_UserGuide, -17);
    lv_obj_set_align(ui_MainMenu_Textarea_UserGuide, LV_ALIGN_TOP_MID);
    lv_textarea_set_text(ui_MainMenu_Textarea_UserGuide,
                         "[Wi-Fi ] - Automatic Connection Setup\n\nSet up your device to automatically connect to your preferred Wi-Fi networks (up to 10 SSIDs).\n- Tap SCAN to search for nearby networks.\n- Select the network you wish to connect to from the dropdown list.\n- Enter the network password.\n- Tap SAVE to save the password\n- Please wait a moment, device is automatically connect to the network.\n\n[ Screen ] - Display Settings\n\nAdjust the behavior of your device's screen.\n- Set Screen Brightness: Adjust the display intensity.\n- Set Screen Off Timer: Control how long the screen stays on after inactivity.\n- Setting the timer to 0 means the screen will always stay on.\n\n[ Radio ] - Custom Radio Station List\n\nYou can create and upload a custom list of radio stations.\n- Create your own stations list using the CSV format in a file named stations.csv (title,url http only)\n- Save the stations.csv file onto your SD Card.\n- Tap UPLOAD to load your custom station list.\n- If the stations.csv file is not available, the device will use the default station list.\n\n[ Music ] - Update Music Library\n\nLoad audio files from your SD Card to create your music library.\n- Supported audio formats include: mp3, flac, aac, and ogg.\n- Copy your audio files to the SD Card. Files can be placed in the root directory or in subfolders (up to 5 levels deep).\n- Tap LOAD to build the music library.\n* Note: This process may take some time, depending on the number of audio files on the SD Card.\n\n[ Region ] - Set weather & clock region\n\n* Auto by IP - the location will be automatically detected by IP\n* Manual -  enter specific Latitude / Longitude\n* UTC offset - set timezone offset\n* Temp - set weather unit Celcius / Farenheit\n\n[ Button Control ]\n\n           [ RESET    ] -  Reset TuneBar\n\n           [ POWER ] - Hold a sec to turn on/ hold 3 sec to turn off\n\n           [  LIGHT  ] - Toggle screen ON/OFF");
    lv_obj_add_flag(ui_MainMenu_Textarea_UserGuide, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_dir(ui_MainMenu_Textarea_UserGuide, LV_DIR_VER);
    lv_obj_set_style_text_color(ui_MainMenu_Textarea_UserGuide, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_MainMenu_Textarea_UserGuide, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MainMenu_Textarea_UserGuide, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Textarea_UserGuide, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Image_buttons = lv_img_create(ui_MainMenu_Textarea_UserGuide);
    lv_img_set_src(ui_MainMenu_Image_buttons, &ui_img_images_button_png);
    lv_obj_set_width(ui_MainMenu_Image_buttons, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Image_buttons, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_MainMenu_Image_buttons, 0);
    lv_obj_set_y(ui_MainMenu_Image_buttons, 10);
    lv_obj_set_align(ui_MainMenu_Image_buttons, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_add_flag(ui_MainMenu_Image_buttons, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_MainMenu_Image_buttons, LV_OBJ_FLAG_SCROLLABLE);

    ui_MainMenu_Button_closeConfig = lv_btn_create(ui_Screen_Settings);
    lv_obj_set_width(ui_MainMenu_Button_closeConfig, 50);
    lv_obj_set_height(ui_MainMenu_Button_closeConfig, 50);
    lv_obj_set_align(ui_MainMenu_Button_closeConfig, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_flag(ui_MainMenu_Button_closeConfig, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_MainMenu_Button_closeConfig, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_MainMenu_Button_closeConfig, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Button_closeConfig, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MainMenu_Button_closeConfig, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MainMenu_Button_closeConfig, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_MainMenu_Button_closeConfig, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Label_Label9 = lv_label_create(ui_MainMenu_Button_closeConfig);
    lv_obj_set_width(ui_MainMenu_Label_Label9, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_MainMenu_Label_Label9, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_MainMenu_Label_Label9, LV_ALIGN_CENTER);
    lv_label_set_text(ui_MainMenu_Label_Label9, "X");
    lv_obj_set_style_text_font(ui_MainMenu_Label_Label9, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Keyboard_Keyboard1 = lv_keyboard_create(ui_Screen_Settings);
    lv_obj_set_width(ui_MainMenu_Keyboard_Keyboard1, 350);
    lv_obj_set_height(ui_MainMenu_Keyboard_Keyboard1, lv_pct(100));
    lv_obj_set_align(ui_MainMenu_Keyboard_Keyboard1, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_flag(ui_MainMenu_Keyboard_Keyboard1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(ui_MainMenu_Keyboard_Keyboard1, lv_color_hex(0x117200), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Keyboard_Keyboard1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_MainMenu_Keyboard_Keyboard1, lv_color_hex(0x143201), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui_MainMenu_Keyboard_Keyboard1, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Keyboard_Number = lv_keyboard_create(ui_Screen_Settings);
    lv_keyboard_set_mode(ui_MainMenu_Keyboard_Number, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_width(ui_MainMenu_Keyboard_Number, 300);
    lv_obj_set_height(ui_MainMenu_Keyboard_Number, 172);
    lv_obj_set_align(ui_MainMenu_Keyboard_Number, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_flag(ui_MainMenu_Keyboard_Number, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(ui_MainMenu_Keyboard_Number, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MainMenu_Keyboard_Number, lv_color_hex(0x0440FC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Keyboard_Number, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_MainMenu_Keyboard_Number, lv_color_hex(0x05037F), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui_MainMenu_Keyboard_Number, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_MainMenu_Panel_blindPanel = lv_obj_create(ui_Screen_Settings);
    lv_obj_set_width(ui_MainMenu_Panel_blindPanel, lv_pct(100));
    lv_obj_set_height(ui_MainMenu_Panel_blindPanel, lv_pct(100));
    lv_obj_set_x(ui_MainMenu_Panel_blindPanel, -2);
    lv_obj_set_y(ui_MainMenu_Panel_blindPanel, 0);
    lv_obj_set_align(ui_MainMenu_Panel_blindPanel, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_MainMenu_Panel_blindPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_MainMenu_Panel_blindPanel,
                      LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SNAPPABLE |
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scrollbar_mode(ui_MainMenu_Panel_blindPanel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(ui_MainMenu_Panel_blindPanel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_MainMenu_Panel_blindPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_MainMenu_Panel_blindPanel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_MainMenu_Panel_blindPanel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_MainMenu_Panel_blindPanel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);


    lv_obj_add_event_cb(ui_MainMenu_Textarea_Password, ui_event_MainMenu_Textarea_Password, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Button_NetworkSave, ui_event_MainMenu_Button_NetworkSave, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Button_NetworkScan, ui_event_MainMenu_Button_NetworkScan, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Switch_Wifi, ui_event_MainMenu_Switch_Wifi, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Tabpage_network, ui_event_MainMenu_Tabpage_network, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Dropdown_Brightness, ui_event_MainMenu_Dropdown_Brightness, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Dropdown_SleepTimer, ui_event_MainMenu_Dropdown_SleepTimer, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Dropdown_Wallpaper, ui_event_MainMenu_Dropdown_Wallpaper, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Tabpage_screen, ui_event_MainMenu_Tabpage_screen, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Button_LoadStation, ui_event_MainMenu_Button_LoadStation, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Tabpage_station, ui_event_MainMenu_Tabpage_station, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Button_scanMusic, ui_event_MainMenu_Button_scanMusic, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Tabpage_music, ui_event_MainMenu_Tabpage_music, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Checkbox_AutoIP, ui_event_MainMenu_Checkbox_AutoIP, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Textarea_Latitude, ui_event_MainMenu_Textarea_Latitude, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Textarea_Longitude, ui_event_MainMenu_Textarea_Longitude, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Roller_Hour, ui_event_MainMenu_Roller_Hour, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Roller_Minute, ui_event_MainMenu_Roller_Minute, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Roller_Unit, ui_event_MainMenu_Roller_Unit, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Tabpage_region, ui_event_MainMenu_Tabpage_region, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Textarea_UserGuide, ui_event_MainMenu_Textarea_UserGuide, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Tabpage_guide, ui_event_MainMenu_Tabpage_guide, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Tabview_ConfigPanel, ui_event_MainMenu_Tabview_ConfigPanel, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Button_closeConfig, ui_event_MainMenu_Button_closeConfig, LV_EVENT_ALL, NULL);
    lv_keyboard_set_textarea(ui_MainMenu_Keyboard_Keyboard1, ui_MainMenu_Textarea_Password);
    lv_obj_add_event_cb(ui_MainMenu_Keyboard_Keyboard1, ui_event_MainMenu_Keyboard_Keyboard1, LV_EVENT_ALL, NULL);
    lv_keyboard_set_textarea(ui_MainMenu_Keyboard_Number, ui_MainMenu_Textarea_Latitude);
    lv_obj_add_event_cb(ui_MainMenu_Keyboard_Number, ui_event_MainMenu_Keyboard_Number, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_MainMenu_Panel_blindPanel, ui_event_MainMenu_Panel_blindPanel, LV_EVENT_ALL, NULL);

    lv_obj_add_flag(ui_MainMenu_Tabview_ConfigPanel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(ui_Screen_Settings, ui_event_Screen_Settings_gesture, LV_EVENT_GESTURE, NULL);

    event_bus_subscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event, NULL);
    event_bus_subscribe(EVENT_WIFI_SCAN_DONE, s_on_wifi_event, NULL);
    s_wifi_status_timer = lv_timer_create(s_wifi_status_timer_cb, 2000, NULL);
    refresh_wifi_status_text();
    if (wifi_get_scan_count() > 0) {
        set_render_wifi_list();
        if (s_wifi_status_label) {
            lv_label_set_text_fmt(s_wifi_status_label, "Found %u networks", (unsigned)wifi_get_scan_count());
        }
    }

}

void ui_Screen_Settings_screen_destroy(void)
{
    if(ui_Screen_Settings) lv_obj_del(ui_Screen_Settings);

    ui_Screen_Settings = NULL;
    ui_MainMenu_Tabview_ConfigPanel = NULL;
    ui_MainMenu_Tabpage_network = NULL;
    ui_MainMenu_Label_connectStatus = NULL;
    ui_MainMenu_Label_Label3 = NULL;
    ui_MainMenu_Dropdown_NetworkList = NULL;
    ui_MainMenu_Textarea_Password = NULL;
    ui_MainMenu_Button_NetworkSave = NULL;
    ui_MainMenu_Label_Label12 = NULL;
    ui_MainMenu_Button_NetworkScan = NULL;
    ui_MainMenu_Label_Label13 = NULL;
    ui_MainMenu_Switch_Wifi = NULL;
    ui_MainMenu_Tabpage_screen = NULL;
    ui_MainMenu_Dropdown_Brightness = NULL;
    ui_MainMenu_Dropdown_SleepTimer = NULL;
    ui_MainMenu_Dropdown_Wallpaper = NULL;
    ui_MainMenu_Label_Label8 = NULL;
    ui_MainMenu_Tabpage_station = NULL;
    ui_MainMenu_Textarea_stationList = NULL;
    ui_MainMenu_Label_Label10 = NULL;
    ui_MainMenu_Button_LoadStation = NULL;
    ui_MainMenu_Label_Label20 = NULL;
    ui_MainMenu_Tabpage_music = NULL;
    ui_MainMenu_Button_scanMusic = NULL;
    ui_MainMenu_Label_Label14 = NULL;
    ui_MainMenu_Label_Label15 = NULL;
    ui_MainMenu_Label_trackCount = NULL;
    ui_MainMenu_Tabpage_region = NULL;
    ui_MainMenu_Image_Globe = NULL;
    ui_MainMenu_Checkbox_AutoIP = NULL;
    ui_MainMenu_Textarea_Latitude = NULL;
    ui_MainMenu_Textarea_Longitude = NULL;
    ui_MainMenu_Image_timezone = NULL;
    ui_MainMenu_Roller_Hour = NULL;
    ui_MainMenu_Roller_Minute = NULL;
    ui_MainMenu_Roller_Unit = NULL;
    ui_MainMenu_Label_Label25 = NULL;
    ui_MainMenu_Tabpage_guide = NULL;
    ui_MainMenu_Textarea_UserGuide = NULL;
    ui_MainMenu_Image_buttons = NULL;
    ui_MainMenu_Button_closeConfig = NULL;
    ui_MainMenu_Label_Label9 = NULL;
    ui_MainMenu_Keyboard_Keyboard1 = NULL;
    ui_MainMenu_Keyboard_Number = NULL;
    ui_MainMenu_Panel_blindPanel = NULL;

    event_bus_unsubscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event);
    event_bus_unsubscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event);
    event_bus_unsubscribe(EVENT_WIFI_SCAN_DONE, s_on_wifi_event);
    if (s_wifi_status_timer) {
        lv_timer_del(s_wifi_status_timer);
        s_wifi_status_timer = NULL;
    }
    s_wifi_status_label = NULL;
    s_wifi_list = NULL;
    s_kb_textarea = NULL;
    g_kb_ssid[0] = '\0';

}