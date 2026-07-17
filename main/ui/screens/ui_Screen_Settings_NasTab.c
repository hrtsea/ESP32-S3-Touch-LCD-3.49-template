#include "../ui.h"
#include "ui_Screen_Settings_NasTab.h"
#include "esp_log.h"
#include "data_source.h"

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);

static const char *TAG = "ui_Screen_Settings_NasTab";

static lv_obj_t* ui_Settings_Tabpage_nas = NULL;
static lv_obj_t* nas_type_btn = NULL;
static lv_obj_t* nas_type_btn_label = NULL;
static lv_obj_t* sata_disk_dropdown = NULL;
static lv_obj_t* m2_disk_dropdown = NULL;
static lv_obj_t* disk_total_label = NULL;

static lv_obj_t* nas_config_dialog = NULL;
static lv_obj_t* dialog_nas_dropdown = NULL;
static bool dialog_fields_created = false;
static lv_obj_t* dialog_scroll_container = NULL;
static int dialog_temp_nas_type_idx = -1;
static int selected_nas_type_idx = -1;

static lv_obj_t* dialog_ip_input = NULL;
static lv_obj_t* dialog_port_input = NULL;
static lv_obj_t* dialog_username_input = NULL;
static lv_obj_t* dialog_password_input = NULL;
static lv_obj_t* dialog_apiurl_input = NULL;
static lv_obj_t* dialog_ip_label = NULL;
static lv_obj_t* dialog_port_label = NULL;
static lv_obj_t* dialog_username_label = NULL;
static lv_obj_t* dialog_password_label = NULL;
static lv_obj_t* dialog_apiurl_label = NULL;
static lv_obj_t* dialog_snmp_community_input = NULL;
static lv_obj_t* dialog_snmp_community_label = NULL;
static lv_obj_t* dialog_snmp_version_input = NULL;
static lv_obj_t* dialog_snmp_version_label = NULL;
static lv_obj_t* dialog_serial_device_input = NULL;
static lv_obj_t* dialog_serial_device_label = NULL;
static lv_obj_t* dialog_serial_baud_input = NULL;
static lv_obj_t* dialog_serial_baud_label = NULL;
static lv_obj_t* dialog_keyboard = NULL;
static lv_obj_t* dialog_current_focused_ta = NULL;

static void update_disk_total(void);
static void update_sata_dropdown_options(void);
static void update_m2_dropdown_options(void);
static void create_nas_config_dialog(void);
static void create_dialog_fields(void);
static void update_dialog_fields(void);
static void dialog_ta_clicked_cb(lv_event_t* e);
static void dialog_keyboard_event_cb(lv_event_t* e);

static void update_disk_total(void) {
    if (!disk_total_label) return;

    int sata = sata_disk_dropdown ? (int)lv_dropdown_get_selected(sata_disk_dropdown) : g_config.sata_disk_count;
    int m2 = m2_disk_dropdown ? (int)lv_dropdown_get_selected(m2_disk_dropdown) : g_config.m2_disk_count;
    int total = sata + m2;

    char buf[32];
    snprintf(buf, sizeof(buf), "Total: %d/%d", total, MAX_DISKS);
    lv_label_set_text(disk_total_label, buf);

    theme_palette_t theme = theme_get();
    if (total > MAX_DISKS) {
        lv_obj_set_style_text_color(disk_total_label, theme.danger, 0);
    } else {
        lv_obj_set_style_text_color(disk_total_label, theme.info, 0);
    }
}

static void update_sata_dropdown_options(void) {
    if (!sata_disk_dropdown) return;

    int m2 = m2_disk_dropdown ? (int)lv_dropdown_get_selected(m2_disk_dropdown) : g_config.m2_disk_count;
    int max_sata = MAX_DISKS - m2;

    int current_sata_value = -1;
    if (sata_disk_dropdown && lv_dropdown_get_option_cnt(sata_disk_dropdown) > 0) {
        current_sata_value = (int)lv_dropdown_get_selected(sata_disk_dropdown);
    }

    static char sata_options[64];
    sata_options[0] = '\0';
    for (int i = 0; i <= max_sata && i <= 16; i++) {
        if (i > 0) strcat(sata_options, "\n");
        char num[4];
        snprintf(num, sizeof(num), "%d", i);
        strcat(sata_options, num);
    }

    lv_dropdown_set_options(sata_disk_dropdown, sata_options);

    if (current_sata_value >= 0) {
        if (current_sata_value > max_sata) {
            current_sata_value = max_sata;
        }
        lv_dropdown_set_selected(sata_disk_dropdown, current_sata_value);
    }
}

static void update_m2_dropdown_options(void) {
    if (!m2_disk_dropdown) return;

    int sata = sata_disk_dropdown ? (int)lv_dropdown_get_selected(sata_disk_dropdown) : g_config.sata_disk_count;
    int max_m2 = MAX_DISKS - sata;

    int current_m2_value = -1;
    if (m2_disk_dropdown && lv_dropdown_get_option_cnt(m2_disk_dropdown) > 0) {
        current_m2_value = (int)lv_dropdown_get_selected(m2_disk_dropdown);
    }

    static char m2_options[64];
    m2_options[0] = '\0';
    for (int i = 0; i <= max_m2 && i <= 16; i++) {
        if (i > 0) strcat(m2_options, "\n");
        char num[4];
        snprintf(num, sizeof(num), "%d", i);
        strcat(m2_options, num);
    }

    lv_dropdown_set_options(m2_disk_dropdown, m2_options);

    if (current_m2_value >= 0) {
        if (current_m2_value > max_m2) {
            current_m2_value = max_m2;
        }
        lv_dropdown_set_selected(m2_disk_dropdown, current_m2_value);
    }
}

void nas_tab_save(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "NAS Tab save started");

    int sata_disks = sata_disk_dropdown ? (int)lv_dropdown_get_selected(sata_disk_dropdown) : g_config.sata_disk_count;
    int m2_disks = m2_disk_dropdown ? (int)lv_dropdown_get_selected(m2_disk_dropdown) : g_config.m2_disk_count;

    ESP_LOGI(TAG, "Saving disk config: sata=%d, m2=%d", sata_disks, m2_disks);

    config_save_disk_config((uint8_t)sata_disks, (uint8_t)m2_disks);

    ESP_LOGI(TAG, "Disk config saved");
}

static void nas_type_btn_cb(lv_event_t* e) {
    if (nas_type_btn_label == NULL) {
        ESP_LOGI(TAG, "nas_type_btn_label is NULL");
        return;
    }
    create_nas_config_dialog();
}

static void dialog_save_cb(lv_event_t* e) {
    if (dialog_temp_nas_type_idx >= 0 && dialog_temp_nas_type_idx < DATA_TYPE_COUNT) {
        if (nas_type_btn_label != NULL) {
            lv_label_set_text(nas_type_btn_label, NAS_TYPES[dialog_temp_nas_type_idx].display_name);
        } else {
            ESP_LOGI(TAG, "nas_type_btn_label is NULL, skipping label update");
        }
        selected_nas_type_idx = dialog_temp_nas_type_idx;

        const char* ip = dialog_ip_input ? lv_textarea_get_text(dialog_ip_input) : g_config.nas_ip;
        const char* port_str = dialog_port_input ? lv_textarea_get_text(dialog_port_input) : "0";
        const char* username = dialog_username_input ? lv_textarea_get_text(dialog_username_input) : g_config.nas_user;
        const char* password = dialog_password_input ? lv_textarea_get_text(dialog_password_input) : g_config.nas_pass;
        uint16_t port = (uint16_t)atoi(port_str);

        ESP_LOGI(TAG, "Saving NAS config: type=%s, ip=%s, port=%d, user=%s",
                 g_config.nas_type, ip, port, username);

        ESP_LOGI(TAG, "Saving NAS type id: %s", NAS_TYPES[dialog_temp_nas_type_idx].id);

        config_save_nas(g_config.nas_type, ip, port, username, password, g_config.nas_https);

        ESP_LOGI(TAG, "NAS config saved, switching data source...");
        data_source_switch(NAS_TYPES[dialog_temp_nas_type_idx].id);

        ESP_LOGI(TAG, "Data source switch initiated");
    }

    if (nas_config_dialog) {
        lv_obj_del(nas_config_dialog);
        nas_config_dialog = NULL;
        dialog_ip_input = NULL;
        dialog_port_input = NULL;
        dialog_username_input = NULL;
        dialog_password_input = NULL;
        dialog_apiurl_input = NULL;
        dialog_ip_label = NULL;
        dialog_port_label = NULL;
        dialog_username_label = NULL;
        dialog_password_label = NULL;
        dialog_apiurl_label = NULL;
        dialog_snmp_community_input = NULL;
        dialog_snmp_community_label = NULL;
        dialog_snmp_version_input = NULL;
        dialog_snmp_version_label = NULL;
        dialog_serial_device_input = NULL;
        dialog_serial_device_label = NULL;
        dialog_serial_baud_input = NULL;
        dialog_serial_baud_label = NULL;
        dialog_scroll_container = NULL;
        dialog_nas_dropdown = NULL;
    }
}

static void dialog_cancel_cb(lv_event_t* e) {
    if (nas_config_dialog) {
        lv_obj_del(nas_config_dialog);
        nas_config_dialog = NULL;
        dialog_ip_input = NULL;
        dialog_port_input = NULL;
        dialog_username_input = NULL;
        dialog_password_input = NULL;
        dialog_apiurl_input = NULL;
        dialog_ip_label = NULL;
        dialog_port_label = NULL;
        dialog_username_label = NULL;
        dialog_password_label = NULL;
        dialog_apiurl_label = NULL;
        dialog_snmp_community_input = NULL;
        dialog_snmp_community_label = NULL;
        dialog_snmp_version_input = NULL;
        dialog_snmp_version_label = NULL;
        dialog_serial_device_input = NULL;
        dialog_serial_device_label = NULL;
        dialog_serial_baud_input = NULL;
        dialog_serial_baud_label = NULL;
        dialog_scroll_container = NULL;
        dialog_nas_dropdown = NULL;
        dialog_keyboard = NULL;
        dialog_current_focused_ta = NULL;
    }
}

static void dialog_nas_dropdown_cb(lv_event_t* e) {
    dialog_temp_nas_type_idx = lv_dropdown_get_selected(dialog_nas_dropdown);
    ESP_LOGI(TAG, "NAS type selected: %d (%s)", dialog_temp_nas_type_idx,
             dialog_temp_nas_type_idx < DATA_TYPE_COUNT ? NAS_TYPES[dialog_temp_nas_type_idx].display_name : "invalid");

    if (dialog_temp_nas_type_idx >= 0 && dialog_temp_nas_type_idx < DATA_TYPE_COUNT) {
        strlcpy(g_config.nas_type, NAS_TYPES[dialog_temp_nas_type_idx].id, sizeof(g_config.nas_type));
        ESP_LOGI(TAG, "Auto set monitor mode: %s", g_config.nas_type);

        if (dialog_ip_input) lv_textarea_set_text(dialog_ip_input, "");
        if (dialog_port_input) lv_textarea_set_text(dialog_port_input, "0");
        if (dialog_username_input) lv_textarea_set_text(dialog_username_input, "");
        if (dialog_password_input) lv_textarea_set_text(dialog_password_input, "");
        if (dialog_apiurl_input) lv_textarea_set_text(dialog_apiurl_input, "");
        if (dialog_snmp_community_input) lv_textarea_set_text(dialog_snmp_community_input, "");
    }

    update_dialog_fields();
}

static void dialog_keyboard_event_cb(lv_event_t* e) {
    lv_obj_t* kb = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        dialog_current_focused_ta = NULL;
        ESP_LOGI(TAG, "Keyboard closed");
    }
}

static void dialog_ta_clicked_cb(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target(e);

    if (dialog_keyboard && ta) {
        dialog_current_focused_ta = ta;
        lv_keyboard_set_textarea(dialog_keyboard, ta);
        lv_obj_clear_flag(dialog_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(dialog_keyboard);
        ESP_LOGI(TAG, "Keyboard opened");
    }
}

static void create_dialog_fields(void) {
    if (dialog_fields_created) return;

    ESP_LOGI(TAG, "Creating config fields");

    if (dialog_scroll_container) {
        lv_obj_del(dialog_scroll_container);
    }

    dialog_scroll_container = lv_obj_create(nas_config_dialog);
    lv_obj_set_size(dialog_scroll_container, lv_pct(100), 120);
    lv_obj_set_style_bg_opa(dialog_scroll_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dialog_scroll_container, 1, 0);
    lv_obj_set_style_border_color(dialog_scroll_container, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(dialog_scroll_container, 4, 0);
    lv_obj_set_style_pad_all(dialog_scroll_container, 4, 0);
    lv_obj_set_flex_flow(dialog_scroll_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(dialog_scroll_container, 4, 0);
    lv_obj_clear_flag(dialog_scroll_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dialog_ip_row = lv_obj_create(dialog_scroll_container);
    lv_obj_set_size(dialog_ip_row, lv_pct(100), 28);
    lv_obj_set_style_bg_opa(dialog_ip_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dialog_ip_row, 0, 0);
    lv_obj_set_style_pad_all(dialog_ip_row, 0, 0);
    lv_obj_set_flex_flow(dialog_ip_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dialog_ip_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dialog_ip_row, 4, 0);

    dialog_ip_label = lv_label_create(dialog_ip_row);
    lv_label_set_text(dialog_ip_label, "IP:");
    lv_obj_set_style_text_font(dialog_ip_label, &lv_font_montserrat_12, 0);

    dialog_ip_input = lv_textarea_create(dialog_ip_row);
    lv_obj_set_size(dialog_ip_input, 0, 24);
    lv_obj_set_flex_grow(dialog_ip_input, 1);
    lv_obj_set_style_bg_color(dialog_ip_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_ip_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_ip_input, true);
    lv_textarea_set_max_length(dialog_ip_input, 39);
    lv_textarea_set_accepted_chars(dialog_ip_input, "0123456789.");
    lv_textarea_set_text(dialog_ip_input, g_config.nas_ip);
    lv_obj_add_event_cb(dialog_ip_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    dialog_port_label = lv_label_create(dialog_ip_row);
    lv_label_set_text(dialog_port_label, "Port:");
    lv_obj_set_style_text_font(dialog_port_label, &lv_font_montserrat_12, 0);

    dialog_port_input = lv_textarea_create(dialog_ip_row);
    lv_obj_set_size(dialog_port_input, 50, 24);
    lv_obj_set_style_bg_color(dialog_port_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_port_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_port_input, true);
    lv_textarea_set_max_length(dialog_port_input, 5);
    lv_textarea_set_accepted_chars(dialog_port_input, "0123456789");
    char port_buf[6];
    snprintf(port_buf, sizeof(port_buf), "%d", g_config.nas_port);
    lv_textarea_set_text(dialog_port_input, port_buf);
    lv_obj_add_event_cb(dialog_port_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* dialog_auth_row = lv_obj_create(dialog_scroll_container);
    lv_obj_set_size(dialog_auth_row, lv_pct(100), 28);
    lv_obj_set_style_bg_opa(dialog_auth_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dialog_auth_row, 0, 0);
    lv_obj_set_style_pad_all(dialog_auth_row, 0, 0);
    lv_obj_set_flex_flow(dialog_auth_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dialog_auth_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dialog_auth_row, 4, 0);

    dialog_username_label = lv_label_create(dialog_auth_row);
    lv_label_set_text(dialog_username_label, "User:");
    lv_obj_set_style_text_font(dialog_username_label, &lv_font_montserrat_12, 0);

    dialog_username_input = lv_textarea_create(dialog_auth_row);
    lv_obj_set_size(dialog_username_input, 0, 24);
    lv_obj_set_flex_grow(dialog_username_input, 1);
    lv_obj_set_style_bg_color(dialog_username_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_username_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_username_input, true);
    lv_textarea_set_max_length(dialog_username_input, 32);
    lv_textarea_set_text(dialog_username_input, g_config.nas_user);
    lv_obj_add_event_cb(dialog_username_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    dialog_password_label = lv_label_create(dialog_auth_row);
    lv_label_set_text(dialog_password_label, "Pass:");
    lv_obj_set_style_text_font(dialog_password_label, &lv_font_montserrat_12, 0);

    dialog_password_input = lv_textarea_create(dialog_auth_row);
    lv_obj_set_size(dialog_password_input, 0, 24);
    lv_obj_set_flex_grow(dialog_password_input, 1);
    lv_obj_set_style_bg_color(dialog_password_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_password_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_password_input, true);
    lv_textarea_set_password_mode(dialog_password_input, true);
    lv_textarea_set_max_length(dialog_password_input, 64);
    lv_textarea_set_text(dialog_password_input, g_config.nas_pass);
    lv_obj_add_event_cb(dialog_password_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    dialog_apiurl_label = lv_label_create(dialog_scroll_container);
    lv_label_set_text(dialog_apiurl_label, "API URL:");
    lv_obj_set_style_text_font(dialog_apiurl_label, &lv_font_montserrat_12, 0);

    dialog_apiurl_input = lv_textarea_create(dialog_scroll_container);
    lv_obj_set_size(dialog_apiurl_input, lv_pct(100), 24);
    lv_obj_set_style_bg_color(dialog_apiurl_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_apiurl_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_apiurl_input, true);
    lv_textarea_set_max_length(dialog_apiurl_input, 128);
    lv_textarea_set_text(dialog_apiurl_input, "");
    lv_obj_add_event_cb(dialog_apiurl_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    dialog_snmp_community_label = lv_label_create(dialog_scroll_container);
    lv_label_set_text(dialog_snmp_community_label, "SNMP Community:");
    lv_obj_set_style_text_font(dialog_snmp_community_label, &lv_font_montserrat_12, 0);

    dialog_snmp_community_input = lv_textarea_create(dialog_scroll_container);
    lv_obj_set_size(dialog_snmp_community_input, lv_pct(100), 24);
    lv_obj_set_style_bg_color(dialog_snmp_community_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_snmp_community_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_snmp_community_input, true);
    lv_textarea_set_max_length(dialog_snmp_community_input, 32);
    lv_textarea_set_accepted_chars(dialog_snmp_community_input, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    lv_textarea_set_text(dialog_snmp_community_input, g_config.snmp_comm);
    lv_obj_add_event_cb(dialog_snmp_community_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    dialog_snmp_version_label = lv_label_create(dialog_scroll_container);
    lv_label_set_text(dialog_snmp_version_label, "SNMP Version:");
    lv_obj_set_style_text_font(dialog_snmp_version_label, &lv_font_montserrat_12, 0);

    dialog_snmp_version_input = lv_textarea_create(dialog_scroll_container);
    lv_obj_set_size(dialog_snmp_version_input, lv_pct(100), 24);
    lv_obj_set_style_bg_color(dialog_snmp_version_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_snmp_version_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_snmp_version_input, true);
    lv_textarea_set_max_length(dialog_snmp_version_input, 3);
    lv_textarea_set_accepted_chars(dialog_snmp_version_input, "0123456789abc");
    lv_textarea_set_text(dialog_snmp_version_input, "2c");
    lv_obj_add_event_cb(dialog_snmp_version_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    dialog_serial_device_label = lv_label_create(dialog_scroll_container);
    lv_label_set_text(dialog_serial_device_label, "Serial Device:");
    lv_obj_set_style_text_font(dialog_serial_device_label, &lv_font_montserrat_12, 0);

    dialog_serial_device_input = lv_textarea_create(dialog_scroll_container);
    lv_obj_set_size(dialog_serial_device_input, lv_pct(100), 24);
    lv_obj_set_style_bg_color(dialog_serial_device_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_serial_device_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_serial_device_input, true);
    lv_textarea_set_max_length(dialog_serial_device_input, 32);
    lv_textarea_set_accepted_chars(dialog_serial_device_input, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/ _-");
    lv_textarea_set_text(dialog_serial_device_input, "/dev/ttyUSB0");
    lv_obj_add_event_cb(dialog_serial_device_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    dialog_serial_baud_label = lv_label_create(dialog_scroll_container);
    lv_label_set_text(dialog_serial_baud_label, "Baud Rate:");
    lv_obj_set_style_text_font(dialog_serial_baud_label, &lv_font_montserrat_12, 0);

    dialog_serial_baud_input = lv_textarea_create(dialog_scroll_container);
    lv_obj_set_size(dialog_serial_baud_input, lv_pct(100), 24);
    lv_obj_set_style_bg_color(dialog_serial_baud_input, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(dialog_serial_baud_input, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(dialog_serial_baud_input, true);
    lv_textarea_set_max_length(dialog_serial_baud_input, 7);
    lv_textarea_set_accepted_chars(dialog_serial_baud_input, "0123456789");
    char baud_buf[8];
    snprintf(baud_buf, sizeof(baud_buf), "%lu", g_config.serial_baud);
    lv_textarea_set_text(dialog_serial_baud_input, baud_buf);
    lv_obj_add_event_cb(dialog_serial_baud_input, dialog_ta_clicked_cb, LV_EVENT_CLICKED, NULL);

    dialog_fields_created = true;
    ESP_LOGI(TAG, "Config fields created");
}

static void update_dialog_fields(void) {
    if (dialog_temp_nas_type_idx < 0 || dialog_temp_nas_type_idx >= DATA_TYPE_COUNT) {
        return;
    }

    create_dialog_fields();
    NasType current_type = nas_type_from_string(g_config.nas_type);

    bool show_ip = false;
    bool show_port = false;
    bool show_user = false;
    bool show_pass = false;
    bool show_apiurl = false;
    bool show_snmp = false;
    bool show_serial = false;

    switch (current_type) {
        case NET_LINUX_HTTP:
            if (dialog_ip_input) {
                const char* current_ip = lv_textarea_get_text(dialog_ip_input);
                if (current_ip[0] == '\0') {
                    lv_textarea_set_text(dialog_ip_input, "192.168.1.100");
                }
            }
            if (dialog_port_input) {
                const char* current_port = lv_textarea_get_text(dialog_port_input);
                if (current_port[0] == '\0' || strcmp(current_port, "0") == 0) {
                    lv_textarea_set_text(dialog_port_input, "8099");
                }
            }
            show_ip = true;
            show_port = true;
            break;

        case NET_LINUX_SERIAL:
            show_serial = true;
            break;

        case NET_NETDATA:
            show_ip = true;
            show_port = true;
            show_apiurl = true;
            break;

        case NET_SNMP:
            show_ip = true;
            show_port = true;
            show_snmp = true;
            break;

        case NAS_SYNOLOGY:
            if (dialog_ip_input) {
                const char* current_ip = lv_textarea_get_text(dialog_ip_input);
                if (current_ip[0] == '\0') {
                    lv_textarea_set_text(dialog_ip_input, "192.168.1.100");
                }
            }
            if (dialog_port_input) {
                const char* current_port = lv_textarea_get_text(dialog_port_input);
                if (current_port[0] == '\0' || strcmp(current_port, "0") == 0) {
                    lv_textarea_set_text(dialog_port_input, "5000");
                }
            }
            if (dialog_username_input) {
                const char* current_user = lv_textarea_get_text(dialog_username_input);
                if (current_user[0] == '\0') {
                    lv_textarea_set_text(dialog_username_input, "admin");
                }
            }
            show_ip = true;
            show_port = true;
            show_user = true;
            show_pass = true;
            show_apiurl = true;
            break;

        case NAS_QNAP:
            if (dialog_ip_input) {
                const char* current_ip = lv_textarea_get_text(dialog_ip_input);
                if (current_ip[0] == '\0') {
                    lv_textarea_set_text(dialog_ip_input, "192.168.1.100");
                }
            }
            if (dialog_port_input) {
                const char* current_port = lv_textarea_get_text(dialog_port_input);
                if (current_port[0] == '\0' || strcmp(current_port, "0") == 0) {
                    lv_textarea_set_text(dialog_port_input, "8080");
                }
            }
            if (dialog_username_input) {
                const char* current_user = lv_textarea_get_text(dialog_username_input);
                if (current_user[0] == '\0') {
                    lv_textarea_set_text(dialog_username_input, "admin");
                }
            }
            show_ip = true;
            show_port = true;
            show_user = true;
            show_pass = true;
            show_apiurl = true;
            break;

        case NAS_TRUENAS:
            if (dialog_ip_input) {
                const char* current_ip = lv_textarea_get_text(dialog_ip_input);
                if (current_ip[0] == '\0') {
                    lv_textarea_set_text(dialog_ip_input, "192.168.1.100");
                }
            }
            if (dialog_port_input) {
                const char* current_port = lv_textarea_get_text(dialog_port_input);
                if (current_port[0] == '\0' || strcmp(current_port, "0") == 0) {
                    lv_textarea_set_text(dialog_port_input, "80");
                }
            }
            if (dialog_username_input) {
                const char* current_user = lv_textarea_get_text(dialog_username_input);
                if (current_user[0] == '\0') {
                    lv_textarea_set_text(dialog_username_input, "root");
                }
            }
            show_ip = true;
            show_port = true;
            show_user = true;
            show_pass = true;
            show_apiurl = true;
            break;

        case NET_WINDOWS:
            show_ip = true;
            show_user = true;
            show_pass = true;
            break;

        case NAS_FNOS:
            show_ip = true;
            show_port = true;
            show_apiurl = true;
            break;

        case NAS_UNRAID:
            show_ip = true;
            show_port = true;
            show_user = true;
            show_pass = true;
            break;

        case NAS_MOCK:
            break;

        default:
            break;
    }

    if (dialog_ip_label) show_ip ? lv_obj_clear_flag(dialog_ip_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_ip_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_ip_input) show_ip ? lv_obj_clear_flag(dialog_ip_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_ip_input, LV_OBJ_FLAG_HIDDEN);
    if (dialog_port_label) show_port ? lv_obj_clear_flag(dialog_port_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_port_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_port_input) show_port ? lv_obj_clear_flag(dialog_port_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_port_input, LV_OBJ_FLAG_HIDDEN);
    if (dialog_username_label) show_user ? lv_obj_clear_flag(dialog_username_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_username_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_username_input) show_user ? lv_obj_clear_flag(dialog_username_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_username_input, LV_OBJ_FLAG_HIDDEN);
    if (dialog_password_label) show_pass ? lv_obj_clear_flag(dialog_password_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_password_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_password_input) show_pass ? lv_obj_clear_flag(dialog_password_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_password_input, LV_OBJ_FLAG_HIDDEN);
    if (dialog_apiurl_label) show_apiurl ? lv_obj_clear_flag(dialog_apiurl_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_apiurl_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_apiurl_input) show_apiurl ? lv_obj_clear_flag(dialog_apiurl_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_apiurl_input, LV_OBJ_FLAG_HIDDEN);
    if (dialog_snmp_community_label) show_snmp ? lv_obj_clear_flag(dialog_snmp_community_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_snmp_community_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_snmp_community_input) show_snmp ? lv_obj_clear_flag(dialog_snmp_community_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_snmp_community_input, LV_OBJ_FLAG_HIDDEN);
    if (dialog_snmp_version_label) show_snmp ? lv_obj_clear_flag(dialog_snmp_version_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_snmp_version_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_snmp_version_input) show_snmp ? lv_obj_clear_flag(dialog_snmp_version_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_snmp_version_input, LV_OBJ_FLAG_HIDDEN);
    if (dialog_serial_device_label) show_serial ? lv_obj_clear_flag(dialog_serial_device_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_serial_device_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_serial_device_input) show_serial ? lv_obj_clear_flag(dialog_serial_device_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_serial_device_input, LV_OBJ_FLAG_HIDDEN);
    if (dialog_serial_baud_label) show_serial ? lv_obj_clear_flag(dialog_serial_baud_label, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_serial_baud_label, LV_OBJ_FLAG_HIDDEN);
    if (dialog_serial_baud_input) show_serial ? lv_obj_clear_flag(dialog_serial_baud_input, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(dialog_serial_baud_input, LV_OBJ_FLAG_HIDDEN);
}

static void create_nas_config_dialog(void) {
    ESP_LOGI(TAG, "create_nas_config_dialog() called");

    if (nas_config_dialog) {
        ESP_LOGI(TAG, "Dialog already exists, closing first");
        lv_obj_del(nas_config_dialog);
        nas_config_dialog = NULL;
    }

    if (selected_nas_type_idx < 0 || selected_nas_type_idx >= DATA_TYPE_COUNT) {
        ESP_LOGI(TAG, "Invalid selected_nas_type_idx=%d (valid range: 0-%d)",
                 selected_nas_type_idx, DATA_TYPE_COUNT - 1);
        return;
    }

    ESP_LOGI(TAG, "Creating dialog for NAS type idx=%d", selected_nas_type_idx);

    dialog_temp_nas_type_idx = selected_nas_type_idx;

    lv_obj_t* current_screen = lv_scr_act();
    if (current_screen == NULL) {
        ESP_LOGI(TAG, "No active screen");
        return;
    }

    nas_config_dialog = lv_obj_create(current_screen);
    if (nas_config_dialog == NULL) {
        ESP_LOGI(TAG, "Failed to create dialog object");
        return;
    }

    lv_obj_set_size(nas_config_dialog, 280, 220);
    lv_obj_align(nas_config_dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(nas_config_dialog, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(nas_config_dialog, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(nas_config_dialog, 2, 0);
    lv_obj_set_style_radius(nas_config_dialog, 8, 0);
    lv_obj_set_style_pad_all(nas_config_dialog, 8, 0);
    lv_obj_set_flex_flow(nas_config_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(nas_config_dialog, 6, 0);
    lv_obj_set_style_pad_column(nas_config_dialog, 4, 0);
    lv_obj_clear_flag(nas_config_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* nas_type_row = lv_obj_create(nas_config_dialog);
    lv_obj_set_size(nas_type_row, lv_pct(100), 32);
    lv_obj_set_style_bg_opa(nas_type_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nas_type_row, 0, 0);
    lv_obj_set_style_pad_all(nas_type_row, 0, 0);
    lv_obj_set_flex_flow(nas_type_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nas_type_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(nas_type_row, 6, 0);

    lv_obj_t* nas_label = lv_label_create(nas_type_row);
    lv_label_set_text(nas_label, "NAS Type:");
    lv_obj_set_style_text_font(nas_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(nas_label, 60);

    dialog_nas_dropdown = lv_dropdown_create(nas_type_row);
    lv_obj_set_width(dialog_nas_dropdown, lv_pct(100));
    lv_obj_set_flex_grow(dialog_nas_dropdown, 1);

    static char options_buf[256];
    options_buf[0] = '\0';
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (i > 0) strcat(options_buf, "\n");
        strcat(options_buf, NAS_TYPES[i].display_name);
    }
    lv_dropdown_set_options(dialog_nas_dropdown, options_buf);

    lv_dropdown_set_selected(dialog_nas_dropdown, dialog_temp_nas_type_idx);
    lv_obj_add_event_cb(dialog_nas_dropdown, dialog_nas_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    update_dialog_fields();

    lv_obj_t* btn_row = lv_obj_create(nas_config_dialog);
    lv_obj_set_size(btn_row, lv_pct(100), 28);
    lv_obj_set_style_bg_color(btn_row, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 2, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    theme_palette_t theme = theme_get();

    lv_obj_t* save_btn = lv_btn_create(btn_row);
    lv_obj_set_size(save_btn, 80, 24);
    lv_obj_set_style_bg_color(save_btn, theme.ok, 0);
    lv_obj_add_event_cb(save_btn, dialog_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, LV_SYMBOL_OK " Save");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_12, 0);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 80, 24);
    lv_obj_set_style_bg_color(cancel_btn, theme.warn, 0);
    lv_obj_add_event_cb(cancel_btn, dialog_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_12, 0);

    dialog_keyboard = lv_keyboard_create(nas_config_dialog);
    lv_obj_set_size(dialog_keyboard, LV_PCT(100), 120);
    lv_obj_add_flag(dialog_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dialog_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(dialog_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(dialog_keyboard, dialog_keyboard_event_cb, LV_EVENT_ALL, NULL);

    ESP_LOGI(TAG, "Dialog created");
}

static void sata_dropdown_cb(lv_event_t* e) {
    update_m2_dropdown_options();
    update_disk_total();
}

static void m2_dropdown_cb(lv_event_t* e) {
    update_sata_dropdown_options();
    update_disk_total();
}

void ui_Screen_Settings_NasTab_init(lv_obj_t *parent)
{
    ui_Settings_Tabpage_nas = lv_tabview_add_tab(parent, "NAS");
    lv_obj_set_scrollbar_mode(ui_Settings_Tabpage_nas, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(ui_Settings_Tabpage_nas, LV_DIR_VER);

    selected_nas_type_idx = -1;
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(g_config.nas_type, NAS_TYPES[i].id) == 0) {
            selected_nas_type_idx = i;
            break;
        }
    }

    if (selected_nas_type_idx < 0) {
        selected_nas_type_idx = 3;
    }

    ESP_LOGI(TAG, "Initializing NAS tab, current type=%s, idx=%d", g_config.nas_type, selected_nas_type_idx);

    lv_obj_t* nas_type_row = lv_obj_create(ui_Settings_Tabpage_nas);
    lv_obj_set_size(nas_type_row, lv_pct(100), 36);
    lv_obj_set_style_bg_color(nas_type_row, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(nas_type_row, 0, 0);
    lv_obj_set_style_pad_all(nas_type_row, 0, 0);
    lv_obj_set_flex_flow(nas_type_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nas_type_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(nas_type_row, 6, 0);

    lv_obj_t* nas_type_lbl = lv_label_create(nas_type_row);
    lv_label_set_text(nas_type_lbl, "NAS Type:");
    lv_obj_set_style_text_font(nas_type_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_width(nas_type_lbl, 70);

    nas_type_btn = lv_btn_create(nas_type_row);
    lv_obj_set_size(nas_type_btn, 0, 36);
    lv_obj_set_flex_grow(nas_type_btn, 1);
    lv_obj_set_style_bg_color(nas_type_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(nas_type_btn, 1, 0);
    lv_obj_set_style_border_color(nas_type_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(nas_type_btn, 4, 0);
    lv_obj_add_event_cb(nas_type_btn, nas_type_btn_cb, LV_EVENT_CLICKED, NULL);

    nas_type_btn_label = lv_label_create(nas_type_btn);
    if (selected_nas_type_idx >= 0 && selected_nas_type_idx < DATA_TYPE_COUNT) {
        lv_label_set_text(nas_type_btn_label, NAS_TYPES[selected_nas_type_idx].display_name);
    } else {
        lv_label_set_text(nas_type_btn_label, "Unknown");
    }
    lv_obj_set_style_text_font(nas_type_btn_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(nas_type_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(nas_type_btn_label);

    lv_obj_t* disk_label = lv_label_create(ui_Settings_Tabpage_nas);
    lv_label_set_text(disk_label, "Disk Count (SATA / M.2):");
    lv_obj_set_style_text_font(disk_label, &lv_font_montserrat_12, 0);

    lv_obj_t* disk_count_row = lv_obj_create(ui_Settings_Tabpage_nas);
    lv_obj_set_size(disk_count_row, lv_pct(100), 36);
    lv_obj_set_style_bg_color(disk_count_row, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(disk_count_row, 0, 0);
    lv_obj_set_style_pad_all(disk_count_row, 0, 0);
    lv_obj_set_flex_flow(disk_count_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(disk_count_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* sata_label = lv_label_create(disk_count_row);
    lv_label_set_text(sata_label, "SATA:");
    lv_obj_set_style_text_font(sata_label, &lv_font_montserrat_12, 0);

    int initial_m2 = g_config.m2_disk_count;
    int max_sata_initial = 16 - initial_m2;
    static char sata_initial_options[64];
    sata_initial_options[0] = '\0';
    for (int i = 0; i <= max_sata_initial && i <= 16; i++) {
        if (i > 0) strcat(sata_initial_options, "\n");
        char num[4];
        snprintf(num, sizeof(num), "%d", i);
        strcat(sata_initial_options, num);
    }

    sata_disk_dropdown = lv_dropdown_create(disk_count_row);
    lv_dropdown_set_options(sata_disk_dropdown, sata_initial_options);
    lv_dropdown_set_selected(sata_disk_dropdown, g_config.sata_disk_count);
    lv_obj_set_width(sata_disk_dropdown, 60);
    lv_obj_set_style_text_font(sata_disk_dropdown, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(sata_disk_dropdown, sata_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* m2_label = lv_label_create(disk_count_row);
    lv_label_set_text(m2_label, "M.2:");
    lv_obj_set_style_text_font(m2_label, &lv_font_montserrat_12, 0);

    int initial_sata = g_config.sata_disk_count;
    int max_m2_initial = 16 - initial_sata;
    static char m2_initial_options[64];
    m2_initial_options[0] = '\0';
    for (int i = 0; i <= max_m2_initial && i <= 16; i++) {
        if (i > 0) strcat(m2_initial_options, "\n");
        char num[4];
        snprintf(num, sizeof(num), "%d", i);
        strcat(m2_initial_options, num);
    }

    m2_disk_dropdown = lv_dropdown_create(disk_count_row);
    lv_dropdown_set_options(m2_disk_dropdown, m2_initial_options);
    lv_dropdown_set_selected(m2_disk_dropdown, g_config.m2_disk_count);
    lv_obj_set_width(m2_disk_dropdown, 60);
    lv_obj_set_style_text_font(m2_disk_dropdown, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(m2_disk_dropdown, m2_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    disk_total_label = lv_label_create(disk_count_row);
    char disk_buf[32];
    int total_disks = config_get_total_disk_slots();
    snprintf(disk_buf, sizeof(disk_buf), "Total: %d/16", total_disks);
    lv_label_set_text(disk_total_label, disk_buf);
    lv_obj_set_style_text_font(disk_total_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(disk_total_label, lv_color_hex(0x00FF00), 0);

    lv_obj_t* spacer = lv_obj_create(ui_Settings_Tabpage_nas);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);

    theme_palette_t theme = theme_get();

    lv_obj_t* save_btn = lv_btn_create(ui_Settings_Tabpage_nas);
    lv_obj_set_size(save_btn, lv_pct(100), 40);
    lv_obj_set_style_bg_color(save_btn, theme.ok, 0);
    lv_obj_set_style_radius(save_btn, 4, 0);
    lv_obj_add_event_cb(save_btn, nas_tab_save, LV_EVENT_CLICKED, NULL);

    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, LV_SYMBOL_OK " Save NAS Config");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_14, 0);
    lv_obj_center(save_label);
}

void ui_Screen_Settings_NasTab_cleanup(void)
{
    if (nas_config_dialog) {
        ESP_LOGI(TAG, "Closing dialog during cleanup");
        lv_obj_del(nas_config_dialog);
        nas_config_dialog = NULL;
        dialog_ip_input = NULL;
        dialog_port_input = NULL;
        dialog_username_input = NULL;
        dialog_password_input = NULL;
        dialog_apiurl_input = NULL;
        dialog_ip_label = NULL;
        dialog_port_label = NULL;
        dialog_username_label = NULL;
        dialog_password_label = NULL;
        dialog_apiurl_label = NULL;
        dialog_snmp_community_input = NULL;
        dialog_snmp_community_label = NULL;
        dialog_snmp_version_input = NULL;
        dialog_snmp_version_label = NULL;
        dialog_serial_device_input = NULL;
        dialog_serial_device_label = NULL;
        dialog_serial_baud_input = NULL;
        dialog_serial_baud_label = NULL;
        dialog_scroll_container = NULL;
        dialog_nas_dropdown = NULL;
        dialog_keyboard = NULL;
        dialog_current_focused_ta = NULL;
    }

    ui_Settings_Tabpage_nas = NULL;
    nas_type_btn = NULL;
    nas_type_btn_label = NULL;
    sata_disk_dropdown = NULL;
    m2_disk_dropdown = NULL;
    disk_total_label = NULL;
}
