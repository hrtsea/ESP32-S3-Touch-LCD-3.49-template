#include "ui_Screen_SystemDetail.h"
#include "../ui.h"
#include "esp_log.h"
#include "../config/app_info.h"

LV_FONT_DECLARE(lv_font_montserrat_12);

static const char *TAG = "SysDetail";

#define COLOR_BG        lv_color_hex(0x000000)
#define COLOR_TEXT      lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DIM  lv_color_hex(0xA0A0A0)
#define COLOR_PRIMARY   lv_color_hex(0x40E0D0)
#define COLOR_INACTIVE  lv_color_hex(0x333333)
#define COLOR_WARN      lv_color_hex(0xFFA500)
#define COLOR_CRIT      lv_color_hex(0xFF0000)

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *btn_back;
    lv_obj_t *label_title;
    lv_obj_t *btn_refresh;

    lv_obj_t *label_cpu_total;
    lv_obj_t *label_load_avg;
    lv_obj_t *core_container;
    lv_obj_t *core_bars[MAX_CPU_CORES];
    lv_obj_t *core_labels[MAX_CPU_CORES];
    lv_obj_t *label_cpu_temp;
    lv_obj_t *label_sys_temp;
    lv_obj_t *label_uptime;
    lv_obj_t *label_hostname;
    lv_obj_t *label_model;

    lv_obj_t *label_ram_total;
    lv_obj_t *bar_ram;
    lv_obj_t *label_ram_pct;
    lv_obj_t *label_ram_free;
    lv_obj_t *label_ram_cached;
    lv_obj_t *label_swap_total;
    lv_obj_t *bar_swap;
    lv_obj_t *label_swap_pct;
    lv_obj_t *label_disk_total;
    lv_obj_t *bar_disk;
    lv_obj_t *label_disk_pct;
} SystemDetailScreen;

static SystemDetailScreen s_screen = {0};
lv_obj_t *ui_Screen_SystemDetail = NULL;
static SystemDetailMode s_mode = SYS_DETAIL_CPU;

static void format_uptime(uint32_t sec, char *buf, size_t buf_size)
{
    uint32_t days = sec / 86400;
    uint32_t hours = (sec % 86400) / 3600;
    uint32_t mins = (sec % 3600) / 60;
    if (days > 0) {
        snprintf(buf, buf_size, "%lud %luh", (unsigned long)days, (unsigned long)hours);
    } else {
        snprintf(buf, buf_size, "%luh %lum", (unsigned long)hours, (unsigned long)mins);
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

static void create_status_bar(lv_obj_t *parent, const char *title)
{
    lv_obj_t *status_bar = lv_obj_create(parent);
    lv_obj_set_size(status_bar, 640, 30);
    lv_obj_set_style_bg_color(status_bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);

    s_screen.btn_back = lv_btn_create(status_bar);
    lv_obj_set_size(s_screen.btn_back, 50, 24);
    lv_obj_set_style_bg_color(s_screen.btn_back, COLOR_INACTIVE, 0);
    lv_obj_set_style_radius(s_screen.btn_back, 3, 0);
    lv_obj_align(s_screen.btn_back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_add_event_cb(s_screen.btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(s_screen.btn_back);
    lv_label_set_text(lbl_back, "<");
    lv_obj_set_style_text_color(lbl_back, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_back);

    s_screen.label_title = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_title, title);
    lv_obj_set_style_text_color(s_screen.label_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_title, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_title, LV_ALIGN_LEFT_MID, 55, 0);

    s_screen.btn_refresh = lv_btn_create(status_bar);
    lv_obj_set_size(s_screen.btn_refresh, 50, 24);
    lv_obj_set_style_bg_color(s_screen.btn_refresh, COLOR_INACTIVE, 0);
    lv_obj_set_style_radius(s_screen.btn_refresh, 3, 0);
    lv_obj_align(s_screen.btn_refresh, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_add_event_cb(s_screen.btn_refresh, refresh_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_ref = lv_label_create(s_screen.btn_refresh);
    lv_label_set_text(lbl_ref, "↻");
    lv_obj_set_style_text_color(lbl_ref, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_ref, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_ref);

    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_set_size(divider, 640, 1);
    lv_obj_set_style_bg_color(divider, COLOR_INACTIVE, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 30);
}

static void create_cpu_mode(lv_obj_t *parent)
{
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_set_size(content, 640, 140);
    lv_obj_set_style_bg_color(content, COLOR_BG, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 2, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 32);

    int line_y = 0;
    const int line_h = 20;

    s_screen.label_cpu_total = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_cpu_total, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_cpu_total, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_cpu_total, "CPU: --");
    lv_obj_align(s_screen.label_cpu_total, LV_ALIGN_TOP_LEFT, 4, line_y);

    s_screen.label_load_avg = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_load_avg, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_screen.label_load_avg, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_load_avg, "Load: --");
    lv_obj_align(s_screen.label_load_avg, LV_ALIGN_TOP_LEFT, 150, line_y);
    line_y += line_h;

    s_screen.core_container = lv_obj_create(content);
    lv_obj_set_size(s_screen.core_container, 632, 36);
    lv_obj_set_style_bg_color(s_screen.core_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(s_screen.core_container, 0, 0);
    lv_obj_set_style_radius(s_screen.core_container, 0, 0);
    lv_obj_set_style_pad_all(s_screen.core_container, 2, 0);
    lv_obj_set_flex_flow(s_screen.core_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_screen.core_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_screen.core_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_screen.core_container, LV_ALIGN_TOP_LEFT, 4, line_y);
    line_y += 40;

    s_screen.label_cpu_temp = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_cpu_temp, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_cpu_temp, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_cpu_temp, "--°C");
    lv_obj_align(s_screen.label_cpu_temp, LV_ALIGN_TOP_LEFT, 4, line_y);

    s_screen.label_sys_temp = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_sys_temp, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_sys_temp, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_sys_temp, "--°C");
    lv_obj_align(s_screen.label_sys_temp, LV_ALIGN_TOP_LEFT, 80, line_y);

    s_screen.label_uptime = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_uptime, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_screen.label_uptime, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_uptime, "--");
    lv_obj_align(s_screen.label_uptime, LV_ALIGN_TOP_LEFT, 180, line_y);
    line_y += line_h;

    s_screen.label_hostname = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_hostname, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_screen.label_hostname, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_hostname, "--");
    lv_obj_align(s_screen.label_hostname, LV_ALIGN_TOP_LEFT, 4, line_y);

    s_screen.label_model = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_model, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_screen.label_model, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_model, "--");
    lv_obj_align(s_screen.label_model, LV_ALIGN_TOP_LEFT, 250, line_y);
}

static void create_mem_mode(lv_obj_t *parent)
{
    lv_obj_t *content = lv_obj_create(parent);
    lv_obj_set_size(content, 640, 140);
    lv_obj_set_style_bg_color(content, COLOR_BG, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 2, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 32);

    int line_y = 0;
    const int line_h = 24;

    s_screen.label_ram_total = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_ram_total, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_ram_total, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_ram_total, "RAM");
    lv_obj_align(s_screen.label_ram_total, LV_ALIGN_TOP_LEFT, 4, line_y);

    s_screen.bar_ram = lv_bar_create(content);
    lv_obj_set_size(s_screen.bar_ram, 400, 12);
    lv_bar_set_value(s_screen.bar_ram, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_screen.bar_ram, COLOR_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_screen.bar_ram, COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_screen.bar_ram, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen.bar_ram, 3, LV_PART_INDICATOR);
    lv_obj_align(s_screen.bar_ram, LV_ALIGN_TOP_LEFT, 60, line_y + 4);

    s_screen.label_ram_pct = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_ram_pct, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_ram_pct, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_ram_pct, "0%");
    lv_obj_align(s_screen.label_ram_pct, LV_ALIGN_TOP_LEFT, 470, line_y);

    s_screen.label_ram_free = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_ram_free, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_screen.label_ram_free, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_ram_free, "--");
    lv_obj_align(s_screen.label_ram_free, LV_ALIGN_TOP_LEFT, 520, line_y);

    s_screen.label_ram_cached = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_ram_cached, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_screen.label_ram_cached, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_ram_cached, "--");
    lv_obj_align(s_screen.label_ram_cached, LV_ALIGN_TOP_LEFT, 600, line_y);
    line_y += line_h;

    s_screen.label_swap_total = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_swap_total, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_swap_total, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_swap_total, "Swap");
    lv_obj_align(s_screen.label_swap_total, LV_ALIGN_TOP_LEFT, 4, line_y);

    s_screen.bar_swap = lv_bar_create(content);
    lv_obj_set_size(s_screen.bar_swap, 400, 12);
    lv_bar_set_value(s_screen.bar_swap, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_screen.bar_swap, COLOR_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_screen.bar_swap, COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_screen.bar_swap, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen.bar_swap, 3, LV_PART_INDICATOR);
    lv_obj_align(s_screen.bar_swap, LV_ALIGN_TOP_LEFT, 60, line_y + 4);

    s_screen.label_swap_pct = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_swap_pct, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_swap_pct, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_swap_pct, "--");
    lv_obj_align(s_screen.label_swap_pct, LV_ALIGN_TOP_LEFT, 470, line_y);
    line_y += line_h;

    s_screen.label_disk_total = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_disk_total, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_disk_total, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_disk_total, "Disk");
    lv_obj_align(s_screen.label_disk_total, LV_ALIGN_TOP_LEFT, 4, line_y);

    s_screen.bar_disk = lv_bar_create(content);
    lv_obj_set_size(s_screen.bar_disk, 400, 12);
    lv_bar_set_value(s_screen.bar_disk, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_screen.bar_disk, COLOR_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_screen.bar_disk, COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_screen.bar_disk, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen.bar_disk, 3, LV_PART_INDICATOR);
    lv_obj_align(s_screen.bar_disk, LV_ALIGN_TOP_LEFT, 60, line_y + 4);

    s_screen.label_disk_pct = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_disk_pct, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_screen.label_disk_pct, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_disk_pct, "0%");
    lv_obj_align(s_screen.label_disk_pct, LV_ALIGN_TOP_LEFT, 470, line_y);
    line_y += line_h;

    s_screen.label_hostname = lv_label_create(content);
    lv_obj_set_style_text_color(s_screen.label_hostname, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_screen.label_hostname, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_screen.label_hostname, "--");
    lv_obj_align(s_screen.label_hostname, LV_ALIGN_TOP_LEFT, 4, line_y);
}

void ui_Screen_SystemDetail_screen_init(SystemDetailMode mode)
{
    if (ui_Screen_SystemDetail != NULL) {
        ui_Screen_SystemDetail_screen_destroy();
    }

    memset(&s_screen, 0, sizeof(s_screen));
    s_mode = mode;

    ui_Screen_SystemDetail = lv_obj_create(NULL);
    s_screen.screen = ui_Screen_SystemDetail;
    lv_obj_set_size(ui_Screen_SystemDetail, 640, 172);
    lv_obj_clear_flag(ui_Screen_SystemDetail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_SystemDetail, COLOR_BG, 0);

    const char *title = (mode == SYS_DETAIL_CPU) ? "CPU Detail" : "Memory Detail";
    create_status_bar(ui_Screen_SystemDetail, title);

    if (mode == SYS_DETAIL_CPU) {
        create_cpu_mode(ui_Screen_SystemDetail);
    } else {
        create_mem_mode(ui_Screen_SystemDetail);
    }

    lv_obj_add_event_cb(ui_Screen_SystemDetail, gesture_cb, LV_EVENT_GESTURE, NULL);

    ESP_LOGI(TAG, "SystemDetail screen initialized (mode=%d)", mode);
}

void ui_Screen_SystemDetail_screen_destroy(void)
{
    if (ui_Screen_SystemDetail) {
        lv_obj_del(ui_Screen_SystemDetail);
        ui_Screen_SystemDetail = NULL;
    }
    memset(&s_screen, 0, sizeof(s_screen));
    ESP_LOGI(TAG, "SystemDetail screen destroyed");
}

void ui_Screen_SystemDetail_update_data(const NasData *data)
{
    if (!ui_Screen_SystemDetail || !data || !data->is_online) return;

    const NasSystemInfo *sys = &data->system;
    char buf[64];

    if (s_mode == SYS_DETAIL_CPU) {
        snprintf(buf, sizeof(buf), "CPU: %d%%", (int)sys->cpu_pct);
        lv_label_set_text(s_screen.label_cpu_total, buf);

        snprintf(buf, sizeof(buf), "L: %.1f %.1f %.1f",
                 sys->load_avg[0], sys->load_avg[1], sys->load_avg[2]);
        lv_label_set_text(s_screen.label_load_avg, buf);

        if (s_screen.core_container) {
            if (lv_obj_get_child_cnt(s_screen.core_container) > 0) {
                lv_obj_clean(s_screen.core_container);
                for (int i = 0; i < MAX_CPU_CORES; i++) {
                    s_screen.core_bars[i] = NULL;
                    s_screen.core_labels[i] = NULL;
                }
            }

            uint8_t cores = sys->cpu_core_count;
            if (cores > MAX_CPU_CORES) cores = MAX_CPU_CORES;
            if (cores == 0) cores = 1;

            for (uint8_t i = 0; i < cores; i++) {
                lv_obj_t *bar = lv_bar_create(s_screen.core_container);
                lv_obj_set_size(bar, 56, 26);
                lv_bar_set_value(bar, (int32_t)sys->cpu_cores[i], LV_ANIM_OFF);
                lv_obj_set_style_bg_color(bar, COLOR_INACTIVE, LV_PART_MAIN);
                lv_obj_set_style_bg_color(bar, COLOR_PRIMARY, LV_PART_INDICATOR);
                lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
                lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);

                lv_obj_t *lbl = lv_label_create(bar);
                static char core_strs[MAX_CPU_CORES][8];
                snprintf(core_strs[i], sizeof(core_strs[i]), "%d%%", (int)sys->cpu_cores[i]);
                lv_label_set_text(lbl, core_strs[i]);
                lv_obj_set_style_text_color(lbl, COLOR_TEXT, 0);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
                lv_obj_center(lbl);

                s_screen.core_bars[i] = bar;
                s_screen.core_labels[i] = lbl;
            }
        }

        if (sys->temp_cpu > 0) {
            snprintf(buf, sizeof(buf), "%d°C", sys->temp_cpu);
        } else {
            snprintf(buf, sizeof(buf), "--°C");
        }
        lv_label_set_text(s_screen.label_cpu_temp, buf);

        if (sys->temp_sys > 0) {
            snprintf(buf, sizeof(buf), "%d°C", sys->temp_sys);
        } else {
            snprintf(buf, sizeof(buf), "--°C");
        }
        lv_label_set_text(s_screen.label_sys_temp, buf);

        char up_buf[24];
        format_uptime(sys->uptime_s, up_buf, sizeof(up_buf));
        snprintf(buf, sizeof(buf), "Up: %s", up_buf);
        lv_label_set_text(s_screen.label_uptime, buf);

        snprintf(buf, sizeof(buf), "%s", (sys->hostname[0]) ? sys->hostname : "--");
        lv_label_set_text(s_screen.label_hostname, buf);

        snprintf(buf, sizeof(buf), "%s", (sys->model[0]) ? sys->model : "--");
        lv_label_set_text(s_screen.label_model, buf);

    } else {
        uint32_t ram_used_mb = sys->ram_total_mb ? sys->ram_used_mb : (uint32_t)(sys->ram_total_mb * sys->ram_pct / 100.0f);
        snprintf(buf, sizeof(buf), "%lu/%luMB", (unsigned long)ram_used_mb, (unsigned long)sys->ram_total_mb);
        lv_label_set_text(s_screen.label_ram_total, buf);

        int ram_pct = (int)sys->ram_pct;
        if (ram_pct < 0) ram_pct = 0;
        if (ram_pct > 100) ram_pct = 100;
        lv_bar_set_value(s_screen.bar_ram, ram_pct, LV_ANIM_ON);
        lv_color_t ram_color = COLOR_PRIMARY;
        if (ram_pct >= 90) ram_color = COLOR_CRIT;
        else if (ram_pct >= 75) ram_color = COLOR_WARN;
        lv_obj_set_style_bg_color(s_screen.bar_ram, ram_color, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "%d%%", ram_pct);
        lv_label_set_text(s_screen.label_ram_pct, buf);

        snprintf(buf, sizeof(buf), "F:%lu", (unsigned long)sys->ram_free_mb);
        lv_label_set_text(s_screen.label_ram_free, buf);

        snprintf(buf, sizeof(buf), "C:%lu", (unsigned long)sys->ram_cached_mb);
        lv_label_set_text(s_screen.label_ram_cached, buf);

        if (sys->swap_total_mb > 0) {
            snprintf(buf, sizeof(buf), "%lu/%luMB", (unsigned long)sys->swap_used_mb, (unsigned long)sys->swap_total_mb);
            lv_label_set_text(s_screen.label_swap_total, buf);
            int swap_pct = (int)(sys->swap_used_mb * 100 / sys->swap_total_mb);
            if (swap_pct < 0) swap_pct = 0;
            if (swap_pct > 100) swap_pct = 100;
            lv_bar_set_value(s_screen.bar_swap, swap_pct, LV_ANIM_ON);
            snprintf(buf, sizeof(buf), "%d%%", swap_pct);
            lv_label_set_text(s_screen.label_swap_pct, buf);
        } else {
            lv_label_set_text(s_screen.label_swap_total, "Swap: N/A");
            lv_bar_set_value(s_screen.bar_swap, 0, LV_ANIM_OFF);
            lv_label_set_text(s_screen.label_swap_pct, "--");
        }

        int disk_pct = (int)sys->disk_pct;
        if (disk_pct < 0) disk_pct = 0;
        if (disk_pct > 100) disk_pct = 100;
        lv_bar_set_value(s_screen.bar_disk, disk_pct, LV_ANIM_ON);
        lv_color_t disk_color = COLOR_PRIMARY;
        if (disk_pct >= 90) disk_color = COLOR_CRIT;
        else if (disk_pct >= 75) disk_color = COLOR_WARN;
        lv_obj_set_style_bg_color(s_screen.bar_disk, disk_color, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "%d%%", disk_pct);
        lv_label_set_text(s_screen.label_disk_pct, buf);

        snprintf(buf, sizeof(buf), "%s", (sys->hostname[0]) ? sys->hostname : "--");
        lv_label_set_text(s_screen.label_hostname, buf);
    }
}