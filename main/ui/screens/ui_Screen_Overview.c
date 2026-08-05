#include "../ui.h"
#include "ui_Screen_Overview.h"
#include "../ui_widgets.h"
#include "theme.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi_config.h"
#include "wifi_adapter.h"

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_32);
LV_FONT_DECLARE(lv_font_montserrat_16);

/* 网络速率迷你趋势线 (sparkline) 配置 */
#define COLOR_SPARK_RX    page_accent_color(PAGE_IDX_OVERVIEW)  /* 下载: 青色 */
#define COLOR_SPARK_TX    theme_get().up_accent                  /* 上传: 黄色 */
#define SPARK_W           30
#define SPARK_H           12
#define SPARK_HISTORY_LEN 30

#define THRESHOLD_CPU    2
#define THRESHOLD_TEMP   1
#define THRESHOLD_MEM    2
#define THRESHOLD_DISK   1

typedef struct {
    lv_obj_t *screen;

    lv_obj_t *label_time;
    lv_obj_t *label_up;
    lv_obj_t *label_down;
    lv_obj_t *label_ip;
    lv_obj_t *icon_wifi;
    lv_obj_t *icon_bt;

    /* 网络速率迷你趋势线 (sparkline) 画布与历史环形缓冲区 */
    lv_obj_t *canvas_rx;   /* 下载速率 sparkline (青色) */
    lv_obj_t *canvas_tx;   /* 上传速率 sparkline (黄色) */
    float rx_history[SPARK_HISTORY_LEN];
    float tx_history[SPARK_HISTORY_LEN];
    int history_count;     /* 已采样点数 (<= SPARK_HISTORY_LEN) */
    int history_head;      /* 下一个写入位置 */

    lv_obj_t *meter_cpu;
    lv_obj_t *meter_temp;
    lv_obj_t *label_cpu_percent;
    lv_obj_t *label_temp_val;
    lv_meter_indicator_t *cpu_arc_val;
    lv_meter_indicator_t *cpu_needle;
    lv_meter_indicator_t *temp_arc_val;
    lv_meter_indicator_t *temp_needle;

    lv_obj_t *bar_mem;
    lv_obj_t *label_mem_percent;
    lv_obj_t *bar_disk;
    lv_obj_t *label_disk_percent;

    lv_obj_t *hdd_leds[MAX_DISKS];
    lv_obj_t *hdd_labels[MAX_DISKS];
    lv_obj_t *hdd_buttons[MAX_DISKS];
    lv_obj_t *hdd_container;

    struct {
        int cpu_pct;
        int temp;
        int mem_pct;
        int disk_pct;
        int hdd_health[MAX_DISKS];
        bool hdd_online[MAX_DISKS];
        char hdd_names[MAX_DISKS][32];
    } last_values;
} OverviewScreen;

static OverviewScreen s_screen = {0};

/* sparkline canvas 独立静态缓冲区 (30x12 RGB565)，避免与 ui_widget_mini_wave 共享冲突 */
static uint8_t s_rx_canvas_buf[SPARK_W * SPARK_H * 2];
static uint8_t s_tx_canvas_buf[SPARK_W * SPARK_H * 2];

/* 将 hdd_health 初始化为 -1，确保首次数据到达时必定触发颜色更新
 * （HEALTH_OK=0, HEALTH_WARNING=1, HEALTH_CRITICAL=2，均与 -1 不同） */
static void overview_screen_init_last_values(void) {
    for (int i = 0; i < MAX_DISKS; i++) {
        s_screen.last_values.hdd_health[i] = -1;
    }
}
lv_obj_t *ui_Screen_Overview = NULL;

static void set_default_style(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, theme_get().bg, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/* 绘制网络速率迷你趋势线 (sparkline)
 * history/count/head: 环形缓冲区指针与长度
 * 按时间顺序取出最近 count 个采样点，自动缩放后以折线绘制到 canvas 上 */
static void overview_draw_sparkline(lv_obj_t *canvas,
                                     const float *history, int count, int head,
                                     lv_color_t color) {
    if (!canvas) return;

    /* 清空 canvas（透明背景，便于叠加在状态栏上） */
    lv_canvas_fill_bg(canvas, theme_get().bg, LV_OPA_TRANSP);
    if (count <= 0) return;

    /* 按时间顺序取出可见采样点 */
    int n = count < SPARK_HISTORY_LEN ? count : SPARK_HISTORY_LEN;
    int start = (head - n + SPARK_HISTORY_LEN) % SPARK_HISTORY_LEN;
    float visible[SPARK_HISTORY_LEN];
    for (int i = 0; i < n; i++) {
        visible[i] = history[(start + i) % SPARK_HISTORY_LEN];
    }

    /* 自动缩放：取历史最大值 × 1.1，避免折线顶到画布边缘 */
    float max_val = 0.0f;
    for (int i = 0; i < n; i++) {
        if (visible[i] > max_val) max_val = visible[i];
    }
    max_val *= 1.1f;
    if (max_val < 1.0f) max_val = 1.0f;

    /* 画折线连接各点 */
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = 1;
    line_dsc.opa = LV_OPA_COVER;

    for (int i = 0; i < n - 1; i++) {
        lv_point_t p1, p2;
        p1.x = (int)((float)i * SPARK_W / n);
        p2.x = (int)((float)(i + 1) * SPARK_W / n);
        p1.y = SPARK_H - 1 - (int)(visible[i] / max_val * (SPARK_H - 1));
        p2.y = SPARK_H - 1 - (int)(visible[i + 1] / max_val * (SPARK_H - 1));
        if (p1.y < 0) p1.y = 0;
        if (p1.y >= SPARK_H) p1.y = SPARK_H - 1;
        if (p2.y < 0) p2.y = 0;
        if (p2.y >= SPARK_H) p2.y = SPARK_H - 1;
        lv_point_t pts[2] = { p1, p2 };
        lv_canvas_draw_line(canvas, pts, 2, &line_dsc);
    }
}

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

    lv_obj_t *label_time = lv_label_create(status_bar);
    s_screen.label_time = label_time;
    static char time_str[9];
    time_t now_t = time(NULL);
    struct tm *tm_now = localtime(&now_t);
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    lv_label_set_text(label_time, time_str);
    lv_obj_set_style_text_color(label_time, theme_get().text, 0);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_12, 0);
    lv_obj_align(label_time, LV_ALIGN_LEFT_MID, 110, 0);

    lv_obj_t *label_up = lv_label_create(status_bar);
    static char up_str[16];
    snprintf(up_str, sizeof(up_str), "^ 0.00KB/s");
    lv_label_set_text(label_up, up_str);
    lv_obj_set_style_text_color(label_up, theme_get().text, 0);
    lv_obj_set_style_text_font(label_up, &lv_font_montserrat_12, 0);
    lv_obj_align(label_up, LV_ALIGN_LEFT_MID, 250, 0);
    s_screen.label_up = label_up;
    /* 点击上传速率区域 → 进入 NetDetail 页面 */
    lv_obj_add_flag(label_up, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label_up, ui_event_Screen_Overview_net_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_down = lv_label_create(status_bar);
    static char down_str[16];
    snprintf(down_str, sizeof(down_str), "v 0.00KB/s");
    lv_label_set_text(label_down, down_str);
    lv_obj_set_style_text_color(label_down, theme_get().text, 0);
    lv_obj_set_style_text_font(label_down, &lv_font_montserrat_12, 0);
    lv_obj_align(label_down, LV_ALIGN_LEFT_MID, 330, 0);
    s_screen.label_down = label_down;
    /* 点击下载速率区域 → 进入 NetDetail 页面 */
    lv_obj_add_flag(label_down, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label_down, ui_event_Screen_Overview_net_clicked, LV_EVENT_CLICKED, NULL);

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

    /* 网络速率迷你趋势线 (sparkline)：位于上传/下载速率标签右侧，并排显示
     * 下载(青色)在左，上传(黄色)在右；canvas 透明背景，不遮挡状态栏 */
    s_screen.canvas_rx = lv_canvas_create(status_bar);
    lv_canvas_set_buffer(s_screen.canvas_rx, s_rx_canvas_buf, SPARK_W, SPARK_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(s_screen.canvas_rx, SPARK_W, SPARK_H);
    lv_obj_set_pos(s_screen.canvas_rx, 410, 12);
    lv_obj_clear_flag(s_screen.canvas_rx, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen.canvas_rx, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_screen.canvas_tx = lv_canvas_create(status_bar);
    lv_canvas_set_buffer(s_screen.canvas_tx, s_tx_canvas_buf, SPARK_W, SPARK_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(s_screen.canvas_tx, SPARK_W, SPARK_H);
    lv_obj_set_pos(s_screen.canvas_tx, 442, 12);
    lv_obj_clear_flag(s_screen.canvas_tx, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen.canvas_tx, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_set_size(divider, 640, 2);
    lv_obj_set_style_bg_color(divider, theme_get().inactive, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 35);
}

static void create_cpu_module(lv_obj_t *parent) {
    lv_obj_t *cpu_container = lv_obj_create(parent);
    lv_obj_set_size(cpu_container, 240, 100);
    set_default_style(cpu_container);
    lv_obj_align(cpu_container, LV_ALIGN_TOP_LEFT, 0, 37);
    lv_obj_set_flex_flow(cpu_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cpu_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(cpu_container, 12, 0);

    s_screen.meter_cpu = lv_meter_create(cpu_container);
    lv_obj_set_size(s_screen.meter_cpu, 90, 90);

    lv_meter_scale_t *scale_cpu = lv_meter_add_scale(s_screen.meter_cpu);
    lv_meter_set_scale_range(s_screen.meter_cpu, scale_cpu, 0, 100, 240, 150);

    lv_meter_indicator_t *cpu_arc_bg = lv_meter_add_arc(s_screen.meter_cpu, scale_cpu, 6, theme_get().inactive, 0);
    lv_meter_set_indicator_start_value(s_screen.meter_cpu, cpu_arc_bg, 0);
    lv_meter_set_indicator_end_value(s_screen.meter_cpu, cpu_arc_bg, 100);

    s_screen.cpu_arc_val = lv_meter_add_arc(s_screen.meter_cpu, scale_cpu, 6, theme_get().accent, 0);
    lv_meter_set_indicator_start_value(s_screen.meter_cpu, s_screen.cpu_arc_val, 0);
    lv_meter_set_indicator_end_value(s_screen.meter_cpu, s_screen.cpu_arc_val, 70);

    s_screen.cpu_needle = lv_meter_add_needle_line(s_screen.meter_cpu, scale_cpu, 2, theme_get().accent, 0);
    lv_meter_set_indicator_value(s_screen.meter_cpu, s_screen.cpu_needle, 70);

    lv_obj_set_style_bg_color(s_screen.meter_cpu, theme_get().bg, 0);
    lv_obj_set_style_border_width(s_screen.meter_cpu, 0, 0);
    lv_obj_set_style_pad_all(s_screen.meter_cpu, 0, 0);

    s_screen.label_cpu_percent = lv_label_create(s_screen.meter_cpu);
    lv_label_set_text(s_screen.label_cpu_percent, "70%");
    lv_obj_set_style_text_color(s_screen.label_cpu_percent, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_cpu_percent, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_cpu_percent, LV_ALIGN_CENTER, 0, 20);

    s_screen.meter_temp = lv_meter_create(cpu_container);
    lv_obj_set_size(s_screen.meter_temp, 90, 90);

    lv_meter_scale_t *scale_temp = lv_meter_add_scale(s_screen.meter_temp);
    lv_meter_set_scale_range(s_screen.meter_temp, scale_temp, 0, 100, 240, 150);

    lv_meter_indicator_t *temp_arc_bg = lv_meter_add_arc(s_screen.meter_temp, scale_temp, 6, theme_get().inactive, 0);
    lv_meter_set_indicator_start_value(s_screen.meter_temp, temp_arc_bg, 0);
    lv_meter_set_indicator_end_value(s_screen.meter_temp, temp_arc_bg, 100);

    s_screen.temp_arc_val = lv_meter_add_arc(s_screen.meter_temp, scale_temp, 6, theme_get().warn, 0);
    lv_meter_set_indicator_start_value(s_screen.meter_temp, s_screen.temp_arc_val, 0);
    lv_meter_set_indicator_end_value(s_screen.meter_temp, s_screen.temp_arc_val, 58);

    s_screen.temp_needle = lv_meter_add_needle_line(s_screen.meter_temp, scale_temp, 2, theme_get().warn, 0);
    lv_meter_set_indicator_value(s_screen.meter_temp, s_screen.temp_needle, 58);

    lv_obj_set_style_bg_color(s_screen.meter_temp, theme_get().bg, 0);
    lv_obj_set_style_border_width(s_screen.meter_temp, 0, 0);
    lv_obj_set_style_pad_all(s_screen.meter_temp, 0, 0);

    s_screen.label_temp_val = lv_label_create(s_screen.meter_temp);
    lv_label_set_text(s_screen.label_temp_val, "58°C");
    lv_obj_set_style_text_color(s_screen.label_temp_val, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_temp_val, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_temp_val, LV_ALIGN_CENTER, 0, 20);

    /* CPU meter 可点击，进入 SystemDetail CPU 模式 */
    lv_obj_add_flag(s_screen.meter_cpu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen.meter_cpu, ui_event_Screen_Overview_cpu_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_screen.meter_temp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen.meter_temp, ui_event_Screen_Overview_cpu_clicked, LV_EVENT_CLICKED, NULL);
}

static void create_mem_disk_module(lv_obj_t *parent) {
    lv_obj_t *md_container = lv_obj_create(parent);
    lv_obj_set_size(md_container, 380, 100);
    set_default_style(md_container);
    lv_obj_set_style_pad_hor(md_container, 12, 0);
    lv_obj_align(md_container, LV_ALIGN_TOP_RIGHT, 0, 37);

    lv_obj_set_flex_flow(md_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(md_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *mem_container = lv_obj_create(md_container);
    lv_obj_set_size(mem_container, LV_PCT(100), LV_PCT(30));
    set_default_style(mem_container);
    lv_obj_set_flex_flow(mem_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mem_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(mem_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mem_container, ui_event_Screen_Overview_mem_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_mem_title = lv_label_create(mem_container);
    lv_label_set_text(label_mem_title, "MEMORY\n8GB");
    lv_obj_set_style_text_color(label_mem_title, theme_get().text, 0);
    lv_obj_set_style_text_font(label_mem_title, &lv_font_montserrat_12, 0);
    lv_obj_set_width(label_mem_title, LV_PCT(20));

    s_screen.bar_mem = lv_bar_create(mem_container);
    lv_obj_set_size(s_screen.bar_mem, LV_PCT(60), 18);
    lv_bar_set_value(s_screen.bar_mem, 72, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_screen.bar_mem, theme_get().inactive, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_screen.bar_mem, theme_get().accent, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_screen.bar_mem, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen.bar_mem, 5, LV_PART_INDICATOR);

    s_screen.label_mem_percent = lv_label_create(mem_container);
    lv_label_set_text(s_screen.label_mem_percent, "72%");
    lv_obj_set_style_text_color(s_screen.label_mem_percent, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_mem_percent, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_screen.label_mem_percent, LV_PCT(10));
    lv_obj_set_style_text_align(s_screen.label_mem_percent, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *disk_container = lv_obj_create(md_container);
    lv_obj_set_size(disk_container, LV_PCT(100), LV_PCT(30));
    set_default_style(disk_container);
    lv_obj_set_flex_flow(disk_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(disk_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_disk_title = lv_label_create(disk_container);
    lv_label_set_text(label_disk_title, "DISK\n2048GB");
    lv_obj_set_style_text_color(label_disk_title, theme_get().text, 0);
    lv_obj_set_style_text_font(label_disk_title, &lv_font_montserrat_12, 0);
    lv_obj_set_width(label_disk_title, LV_PCT(20));

    s_screen.bar_disk = lv_bar_create(disk_container);
    lv_obj_set_size(s_screen.bar_disk, LV_PCT(60), 18);
    lv_bar_set_value(s_screen.bar_disk, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_screen.bar_disk, theme_get().inactive, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_screen.bar_disk, theme_get().accent, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_screen.bar_disk, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_screen.bar_disk, 5, LV_PART_INDICATOR);

    s_screen.label_disk_percent = lv_label_create(disk_container);
    lv_label_set_text(s_screen.label_disk_percent, "50%");
    lv_obj_set_style_text_color(s_screen.label_disk_percent, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_disk_percent, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_screen.label_disk_percent, LV_PCT(10));
    lv_obj_set_style_text_align(s_screen.label_disk_percent, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_screen.label_disk_percent, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void create_hdd_indicators(lv_obj_t *parent) {
    s_screen.hdd_container = lv_obj_create(parent);
    lv_obj_set_size(s_screen.hdd_container, 640, 35);
    set_default_style(s_screen.hdd_container);
    lv_obj_align(s_screen.hdd_container, LV_ALIGN_BOTTOM_MID, 0, -2);

    uint8_t total_disks = config_get_total_disk_slots();
    if (total_disks == 0) total_disks = 1;

    /* 设计规范: 无间距紧密排列，每个槽位 flex:1 均分宽度 */
    int slot_width = 640 / total_disks;

    static uint8_t s_hdd_indices[MAX_DISKS];

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
        s_hdd_indices[i] = i;
        lv_obj_add_event_cb(btn_hdd, ui_event_Screen_Overview_hdd_clicked, LV_EVENT_CLICKED, &s_hdd_indices[i]);

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

void ui_Screen_Overview_screen_init(void) {
    if (ui_Screen_Overview != NULL) {
        ESP_LOGW("Overview", "Screen already initialized, destroying first");
        ui_Screen_Overview_screen_destroy();
    }

    memset(&s_screen, 0, sizeof(s_screen));
    overview_screen_init_last_values();

    ui_Screen_Overview = lv_obj_create(NULL);
    s_screen.screen = ui_Screen_Overview;
    lv_obj_clear_flag(ui_Screen_Overview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_Overview, theme_get().bg, 0);
    lv_obj_set_style_pad_all(ui_Screen_Overview, 0, 0);

    create_status_bar(ui_Screen_Overview);
    create_cpu_module(ui_Screen_Overview);
    create_mem_disk_module(ui_Screen_Overview);
    create_hdd_indicators(ui_Screen_Overview);

    lv_obj_add_event_cb(ui_Screen_Overview, ui_event_Screen_Overview_gesture, LV_EVENT_GESTURE, NULL);

    ui_helpers_create_page_dots(ui_Screen_Overview, 4, 1);

    ESP_LOGI("Overview", "Overview screen initialized");
}

void ui_Screen_Overview_screen_destroy(void) {
    if (ui_Screen_Overview) {
        lv_obj_del(ui_Screen_Overview);
        ui_Screen_Overview = NULL;
    }
    memset(&s_screen, 0, sizeof(s_screen));
    ESP_LOGI("Overview", "Overview screen destroyed");
}

void overview_screen_update_time(const char *time_str) {
    if (s_screen.label_time) {
        lv_label_set_text(s_screen.label_time, time_str);
    }
}

void overview_screen_update_network(int upload_kbps, int download_kbps) {
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

    /* 追加 rx/tx 速率到环形缓冲区并刷新 sparkline */
    s_screen.rx_history[s_screen.history_head] = (float)download_kbps;
    s_screen.tx_history[s_screen.history_head] = (float)upload_kbps;
    s_screen.history_head = (s_screen.history_head + 1) % SPARK_HISTORY_LEN;
    if (s_screen.history_count < SPARK_HISTORY_LEN) s_screen.history_count++;

    overview_draw_sparkline(s_screen.canvas_rx,
                             s_screen.rx_history, s_screen.history_count,
                             s_screen.history_head, COLOR_SPARK_RX);
    overview_draw_sparkline(s_screen.canvas_tx,
                             s_screen.tx_history, s_screen.history_count,
                             s_screen.history_head, COLOR_SPARK_TX);
}

void overview_screen_update_ip(const char *ip_str) {
    if (s_screen.label_ip) {
        static char ip_buf[24];
        snprintf(ip_buf, sizeof(ip_buf), "IP: %s", ip_str);
        lv_label_set_text(s_screen.label_ip, ip_buf);
    }
}

void overview_screen_update_wifi(bool connected) {
    if (s_screen.icon_wifi) {
        lv_obj_set_style_text_color(s_screen.icon_wifi,
            connected ? theme_get().ok : theme_get().dim, 0);
    }
}

void overview_screen_update_cpu(int cpu_pct) {
    if (!s_screen.meter_cpu) return;
    cpu_pct = (cpu_pct < 0) ? 0 : (cpu_pct > 100) ? 100 : cpu_pct;
    if (abs(cpu_pct - s_screen.last_values.cpu_pct) >= THRESHOLD_CPU) {
        lv_meter_set_indicator_end_value(s_screen.meter_cpu, s_screen.cpu_arc_val, cpu_pct);
        lv_meter_set_indicator_value(s_screen.meter_cpu, s_screen.cpu_needle, cpu_pct);
        static char cpu_str[8];
        snprintf(cpu_str, sizeof(cpu_str), "%d%%", cpu_pct);
        lv_label_set_text(s_screen.label_cpu_percent, cpu_str);
        s_screen.last_values.cpu_pct = cpu_pct;
    }
}

void overview_screen_update_temp(int temp) {
    if (!s_screen.meter_temp) return;
    temp = (temp < 0) ? 0 : (temp > 100) ? 100 : temp;
    if (abs(temp - s_screen.last_values.temp) >= THRESHOLD_TEMP) {
        lv_meter_set_indicator_end_value(s_screen.meter_temp, s_screen.temp_arc_val, temp);
        lv_meter_set_indicator_value(s_screen.meter_temp, s_screen.temp_needle, temp);
        static char temp_str[10];
        snprintf(temp_str, sizeof(temp_str), "%d°C", temp);
        lv_label_set_text(s_screen.label_temp_val, temp_str);
        s_screen.last_values.temp = temp;
    }
}

void overview_screen_update_mem(int mem_pct) {
    if (!s_screen.bar_mem) return;
    mem_pct = (mem_pct < 0) ? 0 : (mem_pct > 100) ? 100 : mem_pct;
    if (abs(mem_pct - s_screen.last_values.mem_pct) >= THRESHOLD_MEM) {
        lv_bar_set_value(s_screen.bar_mem, mem_pct, LV_ANIM_ON);
        static char mem_str[8];
        snprintf(mem_str, sizeof(mem_str), "%d%%", mem_pct);
        lv_label_set_text(s_screen.label_mem_percent, mem_str);
        s_screen.last_values.mem_pct = mem_pct;
    }
}

void overview_screen_update_disk(int disk_pct) {
    if (!s_screen.bar_disk) return;
    disk_pct = (disk_pct < 0) ? 0 : (disk_pct > 100) ? 100 : disk_pct;
    if (abs(disk_pct - s_screen.last_values.disk_pct) >= THRESHOLD_DISK) {
        lv_bar_set_value(s_screen.bar_disk, disk_pct, LV_ANIM_ON);
        static char disk_str[8];
        snprintf(disk_str, sizeof(disk_str), "%d%%", disk_pct);
        lv_label_set_text(s_screen.label_disk_percent, disk_str);
        s_screen.last_values.disk_pct = disk_pct;
    }
}

void overview_screen_update_hdd_led(int index, bool online, int health) {
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
            /* 从离线变为在线: 恢复实心背景 */
            if (s_screen.hdd_buttons[index]) {
                lv_obj_set_style_bg_color(s_screen.hdd_buttons[index], theme_get().inactive, 0);
                lv_obj_set_style_border_width(s_screen.hdd_buttons[index], 0, 0);
            }
            s_screen.last_values.hdd_online[index] = true;
        }
    } else {
        if (s_screen.last_values.hdd_online[index]) {
            lv_obj_set_style_bg_color(s_screen.hdd_leds[index], theme_get().dim, 0);
            /* 从在线变为离线: 透明背景 + 边框（模拟画布虚线边框效果） */
            if (s_screen.hdd_buttons[index]) {
                lv_obj_set_style_bg_color(s_screen.hdd_buttons[index], theme_get().bg, 0);
                lv_obj_set_style_border_color(s_screen.hdd_buttons[index], theme_get().dim, 0);
                lv_obj_set_style_border_width(s_screen.hdd_buttons[index], 1, 0);
            }
            s_screen.last_values.hdd_online[index] = false;
        }
    }
}

void overview_screen_update_hdd_name(int index, const char *name) {
    if (index >= MAX_DISKS || !s_screen.hdd_labels[index] || !name) return;
    /* name 为空时保留初始槽位标签（HDD1/M.21），不覆盖为默认 "HDD" */
    if (!name[0]) return;
    if (strcmp(name, s_screen.last_values.hdd_names[index]) != 0) {
        lv_label_set_text(s_screen.hdd_labels[index], name);
        strncpy(s_screen.last_values.hdd_names[index], name, sizeof(s_screen.last_values.hdd_names[index]) - 1);
    }
}