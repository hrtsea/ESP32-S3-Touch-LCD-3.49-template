#include "ui_Screen_DiskDetail.h"
#include "../ui.h"
#include "esp_log.h"
#include "config.h"

LV_FONT_DECLARE(lv_font_montserrat_14);

static const char *TAG = "DiskDetail";

#define COLOR_BG        lv_color_hex(0x000000)
#define COLOR_TEXT      lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DIM  lv_color_hex(0xA0A0A0)
#define COLOR_PRIMARY  lv_color_hex(0x40E0D0)
#define COLOR_INACTIVE lv_color_hex(0x333333)
#define COLOR_OK       lv_color_hex(0x00FF00)
#define COLOR_WARN     lv_color_hex(0xFFA500)
#define COLOR_CRIT     lv_color_hex(0xFF0000)
#define COLOR_UNKNOWN  lv_color_hex(0x666666)

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *btn_back;
    lv_obj_t *label_title;
    lv_obj_t *btn_refresh;
    lv_obj_t *label_name;
    lv_obj_t *label_model;
    lv_obj_t *label_device;
    lv_obj_t *label_mount;
    lv_obj_t *label_type;
    lv_obj_t *label_slot;
    lv_obj_t *label_online;
    lv_obj_t *label_health;
    lv_obj_t *bar_usage;
    lv_obj_t *label_usage;
    lv_obj_t *label_temp;
    lv_obj_t *label_read;
    lv_obj_t *label_write;
    lv_obj_t *label_size;
} DiskDetailScreen;

static DiskDetailScreen s_screen = {0};
lv_obj_t *ui_Screen_DiskDetail = NULL;
static uint8_t s_disk_index = 0;

static void format_size(uint32_t gb, char *buf, size_t buf_size)
{
    if (gb >= 1024) {
        snprintf(buf, buf_size, "%.1fTB", gb / 1024.0f);
    } else {
        snprintf(buf, buf_size, "%luGB", (unsigned long)gb);
    }
}

static void format_speed(uint32_t kbps, char *buf, size_t buf_size)
{
    if (kbps >= 1024 * 1024) {
        snprintf(buf, buf_size, "%.1fGB/s", kbps / 1024.0f / 1024.0f);
    } else if (kbps >= 1024) {
        snprintf(buf, buf_size, "%.1fMB/s", kbps / 1024.0f);
    } else {
        snprintf(buf, buf_size, "%luKB/s", (unsigned long)kbps);
    }
}

static void back_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_Screen_Overview != NULL) {
        lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }
}

static void refresh_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    event_bus_publish(EVENT_TRIGGER_HTTP_FETCH, NULL, 0);
}

static void gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT) {
        if (ui_Screen_Overview != NULL) {
            lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
        }
    }
}

static void create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *status_bar = lv_obj_create(parent);
    lv_obj_set_size(status_bar, 640, 32);
    lv_obj_set_style_bg_color(status_bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);

    s_screen.btn_back = lv_btn_create(status_bar);
    lv_obj_set_size(s_screen.btn_back, 50, 26);
    lv_obj_set_style_bg_color(s_screen.btn_back, COLOR_INACTIVE, 0);
    lv_obj_set_style_radius(s_screen.btn_back, 3, 0);
    lv_obj_align(s_screen.btn_back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_add_event_cb(s_screen.btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(s_screen.btn_back);
    lv_label_set_text(lbl_back, "< Back");
    lv_obj_set_style_text_color(lbl_back, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_back);

    s_screen.label_title = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_title, "Disk Detail");
    lv_obj_set_style_text_color(s_screen.label_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(s_screen.label_title, LV_ALIGN_LEFT_MID, 60, 0);

    s_screen.btn_refresh = lv_btn_create(status_bar);
    lv_obj_set_size(s_screen.btn_refresh, 50, 26);
    lv_obj_set_style_bg_color(s_screen.btn_refresh, COLOR_INACTIVE, 0);
    lv_obj_set_style_radius(s_screen.btn_refresh, 3, 0);
    lv_obj_align(s_screen.btn_refresh, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_add_event_cb(s_screen.btn_refresh, refresh_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_ref = lv_label_create(s_screen.btn_refresh);
    lv_label_set_text(lbl_ref, "↻");
    lv_obj_set_style_text_color(lbl_ref, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_ref, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_ref);

    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_set_size(divider, 640, 1);
    lv_obj_set_style_bg_color(divider, COLOR_INACTIVE, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 32);
}

static void create_content(lv_obj_t *parent)
{
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_set_size(content, 640, 138);
    lv_obj_set_style_bg_color(content, COLOR_BG, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 4, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 34);

    /* 左列：Name / Model / Device / Mount */
    s_screen.label_name = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_name, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_name, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_name, "Name: --");
    lv_obj_align(s_screen.label_name, LV_ALIGN_TOP_LEFT, 4, 2);

    s_screen.label_model = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_model, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_model, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_model, "Model: --");
    lv_obj_align(s_screen.label_model, LV_ALIGN_TOP_LEFT, 4, 22);

    s_screen.label_device = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_device, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_device, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_device, "Device: --");
    lv_obj_align(s_screen.label_device, LV_ALIGN_TOP_LEFT, 4, 42);

    s_screen.label_mount = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_mount, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_mount, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_mount, "Mount: --");
    lv_obj_align(s_screen.label_mount, LV_ALIGN_TOP_LEFT, 4, 62);

    /* 右列：Type / Slot / Online / Health */
    s_screen.label_type = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_type, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_type, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_type, "Type: --");
    lv_obj_align(s_screen.label_type, LV_ALIGN_TOP_LEFT, 320, 2);

    s_screen.label_slot = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_slot, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_slot, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_slot, "Slot: --");
    lv_obj_align(s_screen.label_slot, LV_ALIGN_TOP_LEFT, 320, 22);

    s_screen.label_online = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_online, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_online, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_online, "Online: --");
    lv_obj_align(s_screen.label_online, LV_ALIGN_TOP_LEFT, 320, 42);

    s_screen.label_health = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_health, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_health, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_health, "Health: --");
    lv_obj_align(s_screen.label_health, LV_ALIGN_TOP_LEFT, 320, 62);

    /* 容量条 */
    s_screen.label_size = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_size, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_size, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_size, "Usage: --");
    lv_obj_align(s_screen.label_size, LV_ALIGN_TOP_LEFT, 4, 84);

    s_screen.bar_usage = lv_bar_create(content);
    lv_obj_set_size(s_screen.bar_usage, 380, 12);
    lv_bar_set_value(s_screen.bar_usage, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_screen.bar_usage, COLOR_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_screen.bar_usage, COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_screen.bar_usage, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen.bar_usage, 3, LV_PART_INDICATOR);
    lv_obj_align(s_screen.bar_usage, LV_ALIGN_TOP_LEFT, 80, 86);

    s_screen.label_usage = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_usage, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_usage, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_usage, "0%");
    lv_obj_align(s_screen.label_usage, LV_ALIGN_TOP_LEFT, 470, 84);

    /* 底部：Temp / Read / Write */
    s_screen.label_temp = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_temp, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_temp, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_temp, "Temp: --");
    lv_obj_align(s_screen.label_temp, LV_ALIGN_TOP_LEFT, 4, 104);

    s_screen.label_read = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_read, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_read, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_read, "Read: --");
    lv_obj_align(s_screen.label_read, LV_ALIGN_TOP_LEFT, 200, 104);

    s_screen.label_write = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_write, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_write, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_screen.label_write, "Write: --");
    lv_obj_align(s_screen.label_write, LV_ALIGN_TOP_LEFT, 380, 104);
}

void ui_Screen_DiskDetail_screen_init(uint8_t disk_index)
{
    if (ui_Screen_DiskDetail != NULL) {
        ui_Screen_DiskDetail_screen_destroy();
    }

    memset(&s_screen, 0, sizeof(s_screen));
    s_disk_index = disk_index;

    ui_Screen_DiskDetail = lv_obj_create(NULL);
    s_screen.screen = ui_Screen_DiskDetail;
    lv_obj_clear_flag(ui_Screen_DiskDetail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_DiskDetail, COLOR_BG, 0);

    create_status_bar(ui_Screen_DiskDetail);
    create_content(ui_Screen_DiskDetail);

    /* 标题 */
    char title[32];
    if (config_is_sata_slot(disk_index)) {
        snprintf(title, sizeof(title), "Disk - HDD%u", disk_index + 1);
    } else {
        uint8_t m2_idx = disk_index - g_config.sata_disk_count;
        snprintf(title, sizeof(title), "Disk - M.2%u", m2_idx + 1);
    }
    lv_label_set_text(s_screen.label_title, title);

    lv_obj_add_event_cb(ui_Screen_DiskDetail, gesture_cb, LV_EVENT_GESTURE, NULL);

    ESP_LOGI(TAG, "DiskDetail screen initialized (idx=%u)", disk_index);
}

void ui_Screen_DiskDetail_screen_destroy(void)
{
    if (ui_Screen_DiskDetail) {
        lv_obj_del(ui_Screen_DiskDetail);
        ui_Screen_DiskDetail = NULL;
    }
    memset(&s_screen, 0, sizeof(s_screen));
    ESP_LOGI(TAG, "DiskDetail screen destroyed");
}

void ui_Screen_DiskDetail_update_data(const NasData *data)
{
    if (!ui_Screen_DiskDetail || !data || !data->is_online) return;
    if (s_disk_index >= data->disk_slot_count) return;

    const NasDiskInfo *disk = &data->disks[s_disk_index];
    char buf[64];

    /* Name */
    snprintf(buf, sizeof(buf), "Name: %s", (disk->name[0]) ? disk->name : "--");
    lv_label_set_text(s_screen.label_name, buf);

    /* Model */
    snprintf(buf, sizeof(buf), "Model: %s", (disk->model_name[0]) ? disk->model_name : "--");
    lv_label_set_text(s_screen.label_model, buf);

    /* Device */
    snprintf(buf, sizeof(buf), "Device: %s", (disk->device[0]) ? disk->device : "--");
    lv_label_set_text(s_screen.label_device, buf);

    /* Mount */
    snprintf(buf, sizeof(buf), "Mount: %s", (disk->mount[0]) ? disk->mount : "--");
    lv_label_set_text(s_screen.label_mount, buf);

    /* Type */
    const char *type_str = (disk->disk_type[0]) ? disk->disk_type : "--";
    if (type_str[0] == 's' || type_str[0] == 'S') type_str = "SATA";
    else if (type_str[0] == 'm' || type_str[0] == 'M') type_str = "M.2";
    else if (type_str[0] == 'n' || type_str[0] == 'N') type_str = "NVMe";
    snprintf(buf, sizeof(buf), "Type: %s", type_str);
    lv_label_set_text(s_screen.label_type, buf);

    /* Slot */
    snprintf(buf, sizeof(buf), "Slot: %u", disk->slot_index);
    lv_label_set_text(s_screen.label_slot, buf);

    /* Online */
    lv_label_set_text(s_screen.label_online, disk->online ? "Online: YES" : "Online: NO");
    lv_obj_set_style_text_color(s_screen.label_online,
        disk->online ? COLOR_OK : COLOR_CRIT, 0);

    /* Health */
    const char *health_str;
    lv_color_t health_color;
    switch (disk->health) {
        case HEALTH_OK:      health_str = "OK";      health_color = COLOR_OK;      break;
        case HEALTH_WARNING: health_str = "WARN";    health_color = COLOR_WARN;     break;
        case HEALTH_CRITICAL:health_str = "CRIT";    health_color = COLOR_CRIT;     break;
        default:             health_str = "UNKNOWN"; health_color = COLOR_UNKNOWN; break;
    }
    snprintf(buf, sizeof(buf), "Health: %s", health_str);
    lv_label_set_text(s_screen.label_health, buf);
    lv_obj_set_style_text_color(s_screen.label_health, health_color, 0);

    /* Size + Usage */
    char size_buf[16], used_buf[16];
    format_size(disk->size_gb, size_buf, sizeof(size_buf));
    format_size(disk->used_gb, used_buf, sizeof(used_buf));
    snprintf(buf, sizeof(buf), "Usage: %s / %s", used_buf, size_buf);
    lv_label_set_text(s_screen.label_size, buf);

    int pct = (int)disk->used_pct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(s_screen.bar_usage, pct, LV_ANIM_ON);
    lv_color_t bar_color = COLOR_PRIMARY;
    if (pct >= 90) bar_color = COLOR_CRIT;
    else if (pct >= 75) bar_color = COLOR_WARN;
    lv_obj_set_style_bg_color(s_screen.bar_usage, bar_color, LV_PART_INDICATOR);

    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_screen.label_usage, buf);

    /* Temp */
    if (disk->temp > 0) {
        snprintf(buf, sizeof(buf), "Temp: %d°C", disk->temp);
    } else {
        snprintf(buf, sizeof(buf), "Temp: --");
    }
    lv_label_set_text(s_screen.label_temp, buf);

    /* Read / Write */
    char spd[16];
    format_speed(disk->read_kbps, spd, sizeof(spd));
    snprintf(buf, sizeof(buf), "Read: %s", spd);
    lv_label_set_text(s_screen.label_read, buf);

    format_speed(disk->write_kbps, spd, sizeof(spd));
    snprintf(buf, sizeof(buf), "Write: %s", spd);
    lv_label_set_text(s_screen.label_write, buf);
}
