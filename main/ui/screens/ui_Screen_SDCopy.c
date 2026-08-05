#include "../ui.h"
#include "ui_Screen_SDCopy.h"
#include "esp_log.h"
#include "esp_wifi_config.h"
#include "wifi_adapter.h"
#include "../../utils/theme.h"

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_32);
LV_FONT_DECLARE(lv_font_montserrat_16);

typedef struct {
    lv_obj_t *screen;

    /* 状态栏 */
    lv_obj_t *label_time;
    lv_obj_t *label_up;
    lv_obj_t *label_down;
    lv_obj_t *label_ip;
    lv_obj_t *icon_wifi;
    lv_obj_t *icon_bt;

    /* 复制进度区域 */
    lv_obj_t *label_status;
    lv_obj_t *label_percent;
    lv_obj_t *label_speed;
    lv_obj_t *bar_progress;

    /* HDD 槽位 */
    lv_obj_t *hdd_leds[MAX_DISKS];
    lv_obj_t *hdd_labels[MAX_DISKS];
    lv_obj_t *hdd_buttons[MAX_DISKS];
    lv_obj_t *hdd_container;

    struct {
        int hdd_health[MAX_DISKS];
        bool hdd_online[MAX_DISKS];
        char hdd_names[MAX_DISKS][32];
    } last_values;
} SDCopyScreen;

static SDCopyScreen s_screen = {0};

lv_obj_t *ui_Screen_SDCopy = NULL;

static void sdcopy_init_last_values(void) {
    for (int i = 0; i < MAX_DISKS; i++) {
        s_screen.last_values.hdd_health[i] = -1;
    }
}

static void set_default_style(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, theme_get().bg, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

/* ========= 状态栏 (与 Overview 一致) ========= */
static void create_status_bar(lv_obj_t *parent) {
    lv_obj_t *status_bar = lv_obj_create(parent);
    lv_obj_set_size(status_bar, 640, 35);
    set_default_style(status_bar);

    lv_obj_t *label_title = lv_label_create(status_bar);
    static char title_str[32];
    if (strlen(g_config.nas_user) > 0) {
        snprintf(title_str, sizeof(title_str), "%s", g_config.nas_user);
    } else if (strlen(g_config.nas_type) > 0 && strcmp(g_config.nas_type, "mock") != 0) {
        snprintf(title_str, sizeof(title_str), "%s", g_config.nas_type);
    } else {
        snprintf(title_str, sizeof(title_str), "NAS Monitor");
    }
    lv_label_set_text(label_title, title_str);
    lv_obj_set_style_text_color(label_title, theme_get().text, 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_12, 0);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 5, 0);

    s_screen.label_time = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_time, "--:--:--");
    lv_obj_set_style_text_color(s_screen.label_time, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_time, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_time, LV_ALIGN_LEFT_MID, 110, 0);

    s_screen.label_up = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_up, "^ 0.00KB/s");
    lv_obj_set_style_text_color(s_screen.label_up, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_up, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_up, LV_ALIGN_LEFT_MID, 250, 0);

    s_screen.label_down = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_down, "v 0.00KB/s");
    lv_obj_set_style_text_color(s_screen.label_down, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_down, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_down, LV_ALIGN_LEFT_MID, 330, 0);

    s_screen.icon_wifi = lv_label_create(status_bar);
    lv_label_set_text(s_screen.icon_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_screen.icon_wifi, theme_get().dim, 0);
    lv_obj_set_style_text_font(s_screen.icon_wifi, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.icon_wifi, LV_ALIGN_LEFT_MID, 594, 0);

    s_screen.icon_bt = lv_label_create(status_bar);
    lv_label_set_text(s_screen.icon_bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_screen.icon_bt, theme_get().dim, 0);
    lv_obj_set_style_text_font(s_screen.icon_bt, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.icon_bt, LV_ALIGN_RIGHT_MID, -5, 0);

    s_screen.label_ip = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_ip, "IP: --");
    lv_obj_set_style_text_color(s_screen.label_ip, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_ip, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_ip, LV_ALIGN_LEFT_MID, 445, 0);

    /* 分隔线 */
    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_set_size(divider, 640, 2);
    lv_obj_set_style_bg_color(divider, theme_get().inactive, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(divider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 35);
}

/* ========= 中间复制进度区域 ========= */
static void create_copy_progress(lv_obj_t *parent) {
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, 640, 100);
    set_default_style(container);
    lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 37);
    lv_obj_set_style_pad_hor(container, 12, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 左侧信息区 */
    lv_obj_t *info_panel = lv_obj_create(container);
    lv_obj_set_size(info_panel, 120, LV_SIZE_CONTENT);
    set_default_style(info_panel);
    lv_obj_set_flex_flow(info_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_screen.label_status = lv_label_create(info_panel);
    lv_label_set_text(s_screen.label_status, "复制中");
    lv_obj_set_style_text_color(s_screen.label_status, theme_get().text_dim, 0);
    lv_obj_set_style_text_font(s_screen.label_status, &lv_font_montserrat_12, 0);

    s_screen.label_percent = lv_label_create(info_panel);
    lv_label_set_text(s_screen.label_percent, "0.00%");
    lv_obj_set_style_text_color(s_screen.label_percent, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_percent, &lv_font_montserrat_16, 0);

    s_screen.label_speed = lv_label_create(info_panel);
    lv_label_set_text(s_screen.label_speed, "0KB/s");
    lv_obj_set_style_text_color(s_screen.label_speed, theme_get().text_dim, 0);
    lv_obj_set_style_text_font(s_screen.label_speed, &lv_font_montserrat_12, 0);

    /* 右侧进度条区域 */
    s_screen.bar_progress = lv_bar_create(container);
    lv_obj_set_size(s_screen.bar_progress, 496, 22);
    lv_bar_set_value(s_screen.bar_progress, 0, LV_ANIM_OFF);
    lv_bar_set_range(s_screen.bar_progress, 0, 10000);  /* 用 0.01% 精度 */
    lv_obj_set_style_bg_color(s_screen.bar_progress, theme_get().inactive, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_screen.bar_progress, theme_get().accent, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_screen.bar_progress, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen.bar_progress, 1, LV_PART_INDICATOR);
}

/* ========= 底部 HDD 槽位 (与 Overview 一致) ========= */
static void create_hdd_indicators(lv_obj_t *parent) {
    s_screen.hdd_container = lv_obj_create(parent);
    lv_obj_set_size(s_screen.hdd_container, 640, 35);
    set_default_style(s_screen.hdd_container);
    lv_obj_align(s_screen.hdd_container, LV_ALIGN_BOTTOM_MID, 0, -2);

    uint8_t total_disks = config_get_total_disk_slots();
    if (total_disks == 0) total_disks = 1;

    int slot_width = 640 / total_disks;

    for (uint8_t i = 0; i < total_disks; i++) {
        char label_text[16];
        if (config_is_sata_slot(i)) {
            snprintf(label_text, sizeof(label_text), "HDD%d", i + 1);
        } else {
            uint8_t m2_index = i - g_config.sata_disk_count;
            snprintf(label_text, sizeof(label_text), "M.2%d", m2_index + 1);
        }

        lv_obj_t *btn_hdd = lv_btn_create(s_screen.hdd_container);
        lv_obj_set_size(btn_hdd, slot_width, 35);
        lv_obj_set_style_bg_color(btn_hdd, theme_get().inactive, 0);
        lv_obj_set_style_border_width(btn_hdd, 0, 0);
        lv_obj_set_style_radius(btn_hdd, 3, 0);
        lv_obj_set_style_pad_all(btn_hdd, 0, 0);
        lv_obj_set_style_pad_hor(btn_hdd, 4, 0);
        lv_obj_set_flex_flow(btn_hdd, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_hdd, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(btn_hdd, LV_ALIGN_LEFT_MID, i * slot_width, 0);

        s_screen.hdd_buttons[i] = btn_hdd;

        s_screen.hdd_labels[i] = lv_label_create(btn_hdd);
        lv_label_set_text(s_screen.hdd_labels[i], label_text);
        lv_obj_set_style_text_color(s_screen.hdd_labels[i], theme_get().text, 0);
        lv_obj_set_style_text_font(s_screen.hdd_labels[i], &lv_font_montserrat_12, 0);

        s_screen.hdd_leds[i] = lv_obj_create(btn_hdd);
        lv_obj_set_size(s_screen.hdd_leds[i], 14, 14);
        lv_obj_set_style_bg_color(s_screen.hdd_leds[i], theme_get().dim, 0);
        lv_obj_set_style_radius(s_screen.hdd_leds[i], LV_RADIUS_CIRCLE, 0);
    }
}

void ui_Screen_SDCopy_screen_init(void) {
    if (ui_Screen_SDCopy != NULL) {
        ESP_LOGW("SDCopy", "Screen already initialized, destroying first");
        ui_Screen_SDCopy_screen_destroy();
    }

    memset(&s_screen, 0, sizeof(s_screen));
    sdcopy_init_last_values();

    ui_Screen_SDCopy = lv_obj_create(NULL);
    s_screen.screen = ui_Screen_SDCopy;
    lv_obj_clear_flag(ui_Screen_SDCopy, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_SDCopy, theme_get().bg, 0);
    lv_obj_set_style_pad_all(ui_Screen_SDCopy, 0, 0);

    create_status_bar(ui_Screen_SDCopy);
    create_copy_progress(ui_Screen_SDCopy);
    create_hdd_indicators(ui_Screen_SDCopy);

    lv_obj_add_event_cb(ui_Screen_SDCopy, ui_event_Screen_SDCopy_gesture, LV_EVENT_GESTURE, NULL);

    ui_helpers_create_page_dots(ui_Screen_SDCopy, 4, 3, -1);

    ESP_LOGI("SDCopy", "SD Copy screen initialized");
}

void ui_Screen_SDCopy_screen_destroy(void) {
    if (ui_Screen_SDCopy) {
        lv_obj_del(ui_Screen_SDCopy);
        ui_Screen_SDCopy = NULL;
    }
    memset(&s_screen, 0, sizeof(s_screen));
    ESP_LOGI("SDCopy", "SD Copy screen destroyed");
}

/* ========= 更新函数 ========= */

void sdcopy_screen_update_time(const char *time_str) {
    if (s_screen.label_time) {
        lv_label_set_text(s_screen.label_time, time_str);
    }
}

void sdcopy_screen_update_network(int upload_kbps, int download_kbps) {
    if (s_screen.label_up) {
        static char up_str[16];
        snprintf(up_str, sizeof(up_str), "^ %.2fKB/s", upload_kbps / 1000.0f);
        lv_label_set_text(s_screen.label_up, up_str);
    }
    if (s_screen.label_down) {
        static char down_str[16];
        snprintf(down_str, sizeof(down_str), "v %.2fKB/s", download_kbps / 1000.0f);
        lv_label_set_text(s_screen.label_down, down_str);
    }
}

void sdcopy_screen_update_ip(const char *ip_str) {
    if (s_screen.label_ip) {
        static char ip_buf[24];
        snprintf(ip_buf, sizeof(ip_buf), "IP: %s", ip_str);
        lv_label_set_text(s_screen.label_ip, ip_buf);
    }
}

void sdcopy_screen_update_wifi(bool connected) {
    if (s_screen.icon_wifi) {
        lv_obj_set_style_text_color(s_screen.icon_wifi,
            connected ? theme_get().ok : theme_get().dim, 0);
    }
}

void sdcopy_screen_update_hdd_led(int index, bool online, int health) {
    if (index >= MAX_DISKS || !s_screen.hdd_leds[index]) return;
    if (online) {
        if (s_screen.last_values.hdd_health[index] != health) {
            lv_color_t color;
            switch (health) {
                case 0: color = theme_get().ok; break;
                case 1: color = theme_get().warn; break;
                case 2: color = theme_get().danger; break;
                default: color = theme_get().dim; break;
            }
            lv_obj_set_style_bg_color(s_screen.hdd_leds[index], color, 0);
            s_screen.last_values.hdd_health[index] = health;
        }
        if (!s_screen.last_values.hdd_online[index]) {
            if (s_screen.hdd_buttons[index]) {
                lv_obj_set_style_bg_color(s_screen.hdd_buttons[index], theme_get().inactive, 0);
                lv_obj_set_style_border_width(s_screen.hdd_buttons[index], 0, 0);
            }
            s_screen.last_values.hdd_online[index] = true;
        }
    } else {
        if (s_screen.last_values.hdd_online[index]) {
            lv_obj_set_style_bg_color(s_screen.hdd_leds[index], theme_get().dim, 0);
            if (s_screen.hdd_buttons[index]) {
                lv_obj_set_style_bg_color(s_screen.hdd_buttons[index], theme_get().bg, 0);
                lv_obj_set_style_border_color(s_screen.hdd_buttons[index], theme_get().dim, 0);
                lv_obj_set_style_border_width(s_screen.hdd_buttons[index], 1, 0);
            }
            s_screen.last_values.hdd_online[index] = false;
        }
    }
}

void sdcopy_screen_update_hdd_name(int index, const char *name) {
    if (index >= MAX_DISKS || !s_screen.hdd_labels[index] || !name) return;
    if (!name[0]) return;
    if (strcmp(name, s_screen.last_values.hdd_names[index]) != 0) {
        lv_label_set_text(s_screen.hdd_labels[index], name);
        strncpy(s_screen.last_values.hdd_names[index], name, sizeof(s_screen.last_values.hdd_names[index]) - 1);
    }
}

void sdcopy_screen_update_progress(int pct, const char *speed_str) {
    if (s_screen.bar_progress) {
        int val = (pct < 0) ? 0 : (pct > 10000) ? 10000 : pct;
        lv_bar_set_value(s_screen.bar_progress, val, LV_ANIM_ON);
    }
    if (s_screen.label_percent) {
        static char pct_str[16];
        snprintf(pct_str, sizeof(pct_str), "%.2f%%", pct / 100.0f);
        lv_label_set_text(s_screen.label_percent, pct_str);
    }
    if (s_screen.label_speed && speed_str) {
        lv_label_set_text(s_screen.label_speed, speed_str);
    }
}