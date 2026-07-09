#include "../ui.h"
#include "ui_Screen_Settings_WifiTab.h"

#include "esp_wifi_config.h"
#include "wifi_adapter.h"

LV_FONT_DECLARE(lv_font_montserrat_32);

lv_obj_t * ui_Settings_Tabpage_network = NULL;
lv_obj_t * ui_Settings_Label_connectStatus = NULL;
lv_obj_t * ui_Settings_Label_wifi_hints = NULL;
lv_obj_t * ui_Settings_Dropdown_NetworkList = NULL;
lv_obj_t * ui_Settings_Textarea_Password = NULL;
lv_obj_t * ui_Settings_Button_NetworkSave = NULL;
lv_obj_t * ui_Settings_Label_saveBtnText = NULL;
lv_obj_t * ui_Settings_Button_NetworkScan = NULL;
lv_obj_t * ui_Settings_Label_scanBtnText = NULL;
lv_obj_t * ui_Settings_Switch_Wifi = NULL;

static void ui_event_Settings_Tabpage_network(lv_event_t * e);
static void ui_event_Settings_Textarea_Password(lv_event_t * e);
static void ui_event_Settings_Button_NetworkSave(lv_event_t * e);
static void ui_event_Settings_Button_NetworkScan(lv_event_t * e);
static void ui_event_Settings_Switch_Wifi(lv_event_t * e);

static void s_on_wifi_event(const event_t *evt, void *user_data);
static void wifi_tab_refresh_list(void);
static void wifi_tab_refresh_status(void);

static bool s_event_subscribed = false;

void ui_event_Settings_Tabpage_network(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        settings_activity_kick(e);
    }
}

void ui_event_Settings_Textarea_Password(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        _ui_flag_modify(ui_Settings_Keyboard_Keyboard1, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

void ui_event_Settings_Button_NetworkSave(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        saveWiFiCredential(e);
    }
}

void ui_event_Settings_Button_NetworkScan(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        scanNetwork(e);
    }
}

void ui_event_Settings_Switch_Wifi(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        toggleWiFi(e);
    }
}

static void wifi_tab_refresh_list(void)
{
    if (!ui_Settings_Dropdown_NetworkList) return;

    lv_dropdown_clear_options(ui_Settings_Dropdown_NetworkList);

    uint16_t scan_n = wifi_cfg_get_scan_count();
    if (scan_n == 0) {
        lv_dropdown_add_option(ui_Settings_Dropdown_NetworkList, "No AP found", 0);
        return;
    }

    char curr_ssid[33];
    wifi_cfg_get_current_ssid(curr_ssid, sizeof(curr_ssid));
    bool connected = wifi_cfg_is_connected();

    for (int i = 0; i < (int)scan_n; i++) {
        const wifi_scan_ap_t *ap = wifi_cfg_get_scan_ap((uint16_t)i);
        if (!ap) continue;
        bool is_connected = connected &&
                            strncmp(ap->ssid, curr_ssid, sizeof(curr_ssid)) == 0;
        char option[128];
        const char *prefix = is_connected ? LV_SYMBOL_OK " " : "";
        const char *ssid_text = ap->ssid[0] ? ap->ssid : "(hidden)";
        snprintf(option, sizeof(option), "%s%s (%ddBm)",
                 prefix, ssid_text, ap->rssi);
        lv_dropdown_add_option(ui_Settings_Dropdown_NetworkList, option, i);
    }
}

static void wifi_tab_refresh_status(void)
{
    if (!ui_Settings_Label_connectStatus) return;

    char ssid_buf[33];
    wifi_cfg_get_current_ssid(ssid_buf, sizeof(ssid_buf));

    if (wifi_cfg_is_connected()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s", LV_SYMBOL_OK, ssid_buf);
        lv_label_set_text(ui_Settings_Label_connectStatus, buf);
        lv_obj_set_style_text_color(ui_Settings_Label_connectStatus,
                                    lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (ssid_buf[0]) {
        uint32_t elapsed = lv_tick_elaps(wifi_get_connect_started_ms());
        uint8_t reason = wifi_get_last_reason();
        char buf[128];
        if (reason) {
            snprintf(buf, sizeof(buf), "%s %s: reason %u",
                     LV_SYMBOL_WARNING, ssid_buf, (unsigned)reason);
        } else if (elapsed > 15000) {
            snprintf(buf, sizeof(buf), "%s %s: timed out",
                     LV_SYMBOL_WARNING, ssid_buf);
        } else {
            snprintf(buf, sizeof(buf), "Connecting %s... (%lus)",
                     ssid_buf, (unsigned long)(elapsed / 1000));
        }
        lv_label_set_text(ui_Settings_Label_connectStatus, buf);
        lv_obj_set_style_text_color(ui_Settings_Label_connectStatus,
                                    lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_label_set_text(ui_Settings_Label_connectStatus, "Disconnected");
        lv_obj_set_style_text_color(ui_Settings_Label_connectStatus,
                                    lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void s_on_wifi_event(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!lvgl_lock(100)) return;

    switch (evt->id) {
        case EVENT_WIFI_SCAN_DONE:
            wifi_tab_refresh_list();
            break;
        case EVENT_WIFI_CONNECTED:
        case EVENT_WIFI_DISCONNECTED:
            wifi_tab_refresh_status();
            break;
        default:
            break;
    }

    lvgl_unlock();
}

void ui_Screen_Settings_WifiTab_init(lv_obj_t *parent)
{
    ui_Settings_Tabpage_network = lv_tabview_add_tab(parent, "Wi-Fi");
    lv_obj_set_scrollbar_mode(ui_Settings_Tabpage_network, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_Settings_Tabpage_network, LV_DIR_HOR);

    ui_Settings_Label_connectStatus = lv_label_create(ui_Settings_Tabpage_network);
    lv_obj_set_width(ui_Settings_Label_connectStatus, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_connectStatus, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_Settings_Label_connectStatus, 60);
    lv_obj_set_y(ui_Settings_Label_connectStatus, 12);
    lv_obj_set_align(ui_Settings_Label_connectStatus, LV_ALIGN_BOTTOM_LEFT);
    lv_label_set_text(ui_Settings_Label_connectStatus, "Disconnected");
    lv_obj_set_style_text_color(ui_Settings_Label_connectStatus, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Settings_Label_connectStatus, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Label_wifi_hints = lv_label_create(ui_Settings_Tabpage_network);
    lv_obj_set_width(ui_Settings_Label_wifi_hints, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_wifi_hints, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_Settings_Label_wifi_hints, -11);
    lv_obj_set_y(ui_Settings_Label_wifi_hints, -2);
    lv_label_set_text(ui_Settings_Label_wifi_hints,
                      "SSID:                                                                                        Max 10\n-------																							                                          Slot\nPass:                                                                                        \n-------                                                                                       Wi-Fi\nStatus:");
    lv_obj_clear_flag(ui_Settings_Label_wifi_hints,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_text_letter_space(ui_Settings_Label_wifi_hints, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui_Settings_Label_wifi_hints, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Dropdown_NetworkList = lv_dropdown_create(ui_Settings_Tabpage_network);
    lv_dropdown_set_options(ui_Settings_Dropdown_NetworkList, "");
    lv_obj_set_width(ui_Settings_Dropdown_NetworkList, 260);
    lv_obj_set_height(ui_Settings_Dropdown_NetworkList, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_Settings_Dropdown_NetworkList, 50);
    lv_obj_set_y(ui_Settings_Dropdown_NetworkList, -15);
    lv_obj_add_flag(ui_Settings_Dropdown_NetworkList, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_Settings_Textarea_Password = lv_textarea_create(ui_Settings_Tabpage_network);
    lv_obj_set_width(ui_Settings_Textarea_Password, 260);
    lv_obj_set_height(ui_Settings_Textarea_Password, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_Settings_Textarea_Password, 50);
    lv_obj_set_y(ui_Settings_Textarea_Password, 38);
    lv_textarea_set_max_length(ui_Settings_Textarea_Password, 32);
    lv_textarea_set_placeholder_text(ui_Settings_Textarea_Password, "Wi-Fi Password");
    lv_textarea_set_one_line(ui_Settings_Textarea_Password, true);

    lv_obj_set_style_border_color(ui_Settings_Textarea_Password, lv_color_hex(0x000000), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ui_Settings_Textarea_Password, 255, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ui_Settings_Textarea_Password, 1, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ui_Settings_Textarea_Password, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);

    ui_Settings_Button_NetworkSave = lv_btn_create(ui_Settings_Tabpage_network);
    lv_obj_set_width(ui_Settings_Button_NetworkSave, 150);
    lv_obj_set_height(ui_Settings_Button_NetworkSave, 50);
    lv_obj_set_x(ui_Settings_Button_NetworkSave, 85);
    lv_obj_set_y(ui_Settings_Button_NetworkSave, 10);
    lv_obj_set_align(ui_Settings_Button_NetworkSave, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Settings_Button_NetworkSave, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_Settings_Button_NetworkSave, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Settings_Button_NetworkSave, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Settings_Button_NetworkSave, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_Settings_Button_NetworkSave, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_Settings_Button_NetworkSave, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Settings_Button_NetworkSave, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Label_saveBtnText = lv_label_create(ui_Settings_Button_NetworkSave);
    lv_obj_set_width(ui_Settings_Label_saveBtnText, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_saveBtnText, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_Settings_Label_saveBtnText, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Settings_Label_saveBtnText, "D Save");
    lv_obj_set_style_text_font(ui_Settings_Label_saveBtnText, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Button_NetworkScan = lv_btn_create(ui_Settings_Tabpage_network);
    lv_obj_set_width(ui_Settings_Button_NetworkScan, 150);
    lv_obj_set_height(ui_Settings_Button_NetworkScan, 50);
    lv_obj_set_x(ui_Settings_Button_NetworkScan, 85);
    lv_obj_set_y(ui_Settings_Button_NetworkScan, -42);
    lv_obj_set_align(ui_Settings_Button_NetworkScan, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Settings_Button_NetworkScan, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_Settings_Button_NetworkScan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Settings_Button_NetworkScan, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Settings_Button_NetworkScan, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_Settings_Button_NetworkScan, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_Settings_Button_NetworkScan, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Settings_Button_NetworkScan, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Label_scanBtnText = lv_label_create(ui_Settings_Button_NetworkScan);
    lv_obj_set_width(ui_Settings_Label_scanBtnText, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_scanBtnText, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_Settings_Label_scanBtnText, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Settings_Label_scanBtnText, "W Scan");
    lv_obj_set_style_text_font(ui_Settings_Label_scanBtnText, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Switch_Wifi = lv_switch_create(ui_Settings_Tabpage_network);
    lv_obj_add_state(ui_Settings_Switch_Wifi, LV_STATE_CHECKED);
    lv_obj_set_width(ui_Settings_Switch_Wifi, 60);
    lv_obj_set_height(ui_Settings_Switch_Wifi, 29);
    lv_obj_set_x(ui_Settings_Switch_Wifi, 0);
    lv_obj_set_y(ui_Settings_Switch_Wifi, -9);
    lv_obj_set_align(ui_Settings_Switch_Wifi, LV_ALIGN_BOTTOM_RIGHT);

    lv_obj_set_style_bg_color(ui_Settings_Switch_Wifi, lv_color_hex(0x088CFD), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(ui_Settings_Switch_Wifi, 255, LV_PART_INDICATOR | LV_STATE_CHECKED);

    lv_obj_set_style_bg_color(ui_Settings_Switch_Wifi, lv_color_hex(0x0845FA), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Settings_Switch_Wifi, 255, LV_PART_KNOB | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Settings_Textarea_Password, ui_event_Settings_Textarea_Password, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Button_NetworkSave, ui_event_Settings_Button_NetworkSave, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Button_NetworkScan, ui_event_Settings_Button_NetworkScan, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Switch_Wifi, ui_event_Settings_Switch_Wifi, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Tabpage_network, ui_event_Settings_Tabpage_network, LV_EVENT_ALL, NULL);

    if (!s_event_subscribed) {
        event_bus_subscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event, NULL);
        event_bus_subscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event, NULL);
        event_bus_subscribe(EVENT_WIFI_SCAN_STARTED, s_on_wifi_event, NULL);
        event_bus_subscribe(EVENT_WIFI_SCAN_DONE, s_on_wifi_event, NULL);
        s_event_subscribed = true;
    }

    wifi_tab_refresh_status();
    wifi_tab_refresh_list();
}

void ui_Screen_Settings_WifiTab_cleanup(void)
{
    if (s_event_subscribed) {
        event_bus_unsubscribe(EVENT_WIFI_CONNECTED, s_on_wifi_event);
        event_bus_unsubscribe(EVENT_WIFI_DISCONNECTED, s_on_wifi_event);
        event_bus_unsubscribe(EVENT_WIFI_SCAN_STARTED, s_on_wifi_event);
        event_bus_unsubscribe(EVENT_WIFI_SCAN_DONE, s_on_wifi_event);
        s_event_subscribed = false;
    }

    ui_Settings_Tabpage_network = NULL;
    ui_Settings_Label_connectStatus = NULL;
    ui_Settings_Label_wifi_hints = NULL;
    ui_Settings_Dropdown_NetworkList = NULL;
    ui_Settings_Textarea_Password = NULL;
    ui_Settings_Button_NetworkSave = NULL;
    ui_Settings_Label_saveBtnText = NULL;
    ui_Settings_Button_NetworkScan = NULL;
    ui_Settings_Label_scanBtnText = NULL;
    ui_Settings_Switch_Wifi = NULL;
}



