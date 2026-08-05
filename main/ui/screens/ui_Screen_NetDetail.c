/* main/ui/screens/ui_Screen_NetDetail.c
 *
 * Network Detail 页面：显示上传/下载历史柱状图。
 *
 * 布局 (640x172)：
 *   ┌────────────────────────────────────────────────┐
 *   │ 状态栏 (35px): 标题/时间/上传/下载/IP/WiFi      │
 *   ├────────────────────────────────────────────────┤
 *   │ 下载区 (48px): 标题行 + 柱状图                  │
 *   ├────────────────────────────────────────────────┤
 *   │ 上传区 (48px): 标题行 + 柱状图                  │
 *   ├────────────────────────────────────────────────┤
 *   │ 网口卡片 (35px): 横向排列各接口简要信息         │
 *   └────────────────────────────────────────────────┘
 *
 * 数据流：
 *   netdetail_screen_update_network(tx_bps, rx_bps)
 *     → 写入环形缓冲区 (rx/tx)
 *     → 刷新两个 chart 的全部 60 个点
 *     → 更新状态栏速率文本（用 fmt_bps_arrow）
 *     → 更新 max/cur 标签（用 fmt_bps）
 *
 *   netdetail_screen_update_interfaces(interfaces, count)
 *     → 刷新底部网口卡片（名称/IP/速率/状态）
 *
 * 手势：右滑返回 Overview（screen 不设 GESTURE_BUBBLE，子容器都设）。
 */

#include "ui_Screen_NetDetail.h"
#include "../ui.h"
#include "esp_log.h"
#include "../../utils/format.h"
#include "../../utils/theme.h"

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_16);

static const char *TAG = "NetDetail";

/* 历史采样点数（约 5 分钟，每 5 秒采样一次） */
#define NET_HISTORY_LEN 60

/* 图表 y 轴下限（避免低速时图表过度放大） */
#define CHART_RANGE_FLOOR 10240

/* 图表值单位：KB/s = bps / 8 / 1024
 * 注意：lv_coord_t 为 int16_t（最大 32767），高速率会饱和但标签仍准确。 */
#define CHART_VALUE_MAX 32767

/* 环形缓冲区：head 指向下一个写入位置，count 为已写入数量 */
static uint32_t s_rx_history[NET_HISTORY_LEN];
static uint32_t s_tx_history[NET_HISTORY_LEN];
static int s_history_count = 0;
static int s_history_head = 0;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *btn_back;
    lv_obj_t *label_title;
    lv_obj_t *label_time;
    lv_obj_t *label_up;
    lv_obj_t *label_down;
    lv_obj_t *label_ip;
    lv_obj_t *icon_wifi;
    lv_obj_t *icon_bt;

    /* 下载区 */
    lv_obj_t *label_down_title;
    lv_obj_t *label_down_max;
    lv_obj_t *label_down_cur;
    lv_obj_t *chart_down;
    lv_chart_series_t *series_down;

    /* 上传区 */
    lv_obj_t *label_up_title;
    lv_obj_t *label_up_max;
    lv_obj_t *label_up_cur;
    lv_obj_t *chart_up;
    lv_chart_series_t *series_up;
} NetDetailScreen;

static NetDetailScreen s_screen = {0};
lv_obj_t *ui_Screen_NetDetail = NULL;

/* ============ 网口卡片静态控件指针 ============ */
/* 存储卡片对象和内部标签指针，避免内存泄漏 */
static lv_obj_t *s_iface_cards[MAX_NETWORK_INTERFACES];
static lv_obj_t *s_iface_labels[MAX_NETWORK_INTERFACES][4]; /* [name, ip, rx, tx] */
static lv_obj_t *s_iface_dots[MAX_NETWORK_INTERFACES];      /* 状态指示圆点 */

/* ============ 工具函数 ============ */

/* 将 bps 转为 KB/s，并钳制在 int16_t 范围内 */
static int16_t bps_to_chart_value(uint32_t bps)
{
    uint32_t kbs = bps / 8U / 1024U;
    if (kbs > (uint32_t)CHART_VALUE_MAX) kbs = (uint32_t)CHART_VALUE_MAX;
    return (int16_t)kbs;
}

/* 取历史最大值（bps） */
static uint32_t history_max(const uint32_t *hist)
{
    uint32_t m = 0;
    for (int i = 0; i < s_history_count; i++) {
        if (hist[i] > m) m = hist[i];
    }
    return m;
}

/* 写入一个采样点到环形缓冲区 */
static void history_push(uint32_t rx_bps, uint32_t tx_bps)
{
    s_rx_history[s_history_head] = rx_bps;
    s_tx_history[s_history_head] = tx_bps;
    s_history_head = (s_history_head + 1) % NET_HISTORY_LEN;
    if (s_history_count < NET_HISTORY_LEN) s_history_count++;
}

/* 用历史数据刷新图表（从最旧到最新顺序填入 60 个点） */
static void chart_refresh(lv_obj_t *chart, lv_chart_series_t *ser, const uint32_t *hist)
{
    if (s_history_count < NET_HISTORY_LEN) {
        /* 未满：前面 count 个为有效数据，后面填 0 */
        for (int i = 0; i < NET_HISTORY_LEN; i++) {
            int16_t v = (i < s_history_count) ? bps_to_chart_value(hist[i]) : 0;
            lv_chart_set_value_by_id(chart, ser, (uint16_t)i, v);
        }
    } else {
        /* 已满：head 是最旧位置 */
        for (int i = 0; i < NET_HISTORY_LEN; i++) {
            int idx = (s_history_head + i) % NET_HISTORY_LEN;
            lv_chart_set_value_by_id(chart, ser, (uint16_t)i,
                                     bps_to_chart_value(hist[idx]));
        }
    }
    lv_chart_refresh(chart);
}

/* 根据历史最大值自动调整图表 y 轴范围 */
static void chart_autoscale(lv_obj_t *chart, const uint32_t *hist)
{
    uint32_t max_bps = history_max(hist);
    /* 转成 KB/s 后 × 1.1（整数实现：max + max/10） */
    uint32_t max_kbs = max_bps / 8U / 1024U;
    uint32_t range_max = max_kbs + max_kbs / 10U;
    if (range_max < (uint32_t)CHART_RANGE_FLOOR) range_max = (uint32_t)CHART_RANGE_FLOOR;
    if (range_max > (uint32_t)CHART_VALUE_MAX) range_max = (uint32_t)CHART_VALUE_MAX;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, (int16_t)range_max);
}

/* ============ 事件回调 ============ */

static void back_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_Screen_Overview != NULL) {
        lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }
}

/* 手势回调在 ui_events.c 中实现：ui_event_Screen_NetDetail_gesture */

/* ============ 状态栏（与其他 Detail 页一致） ============ */

static void set_default_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, theme_get().bg, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    /* 子容器需要 GESTURE_BUBBLE 让手势冒泡到 screen */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static void create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *status_bar = lv_obj_create(parent);
    lv_obj_set_size(status_bar, 640, 35);
    set_default_style(status_bar);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);

    /* 返回按钮 */
    s_screen.btn_back = lv_btn_create(status_bar);
    lv_obj_set_size(s_screen.btn_back, 50, 24);
    lv_obj_set_style_bg_color(s_screen.btn_back, theme_get().inactive, 0);
    lv_obj_set_style_radius(s_screen.btn_back, 3, 0);
    lv_obj_align(s_screen.btn_back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_add_event_cb(s_screen.btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(s_screen.btn_back);
    lv_label_set_text(lbl_back, "<");
    lv_obj_set_style_text_color(lbl_back, theme_get().text, 0);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_back);

    /* 标题 */
    s_screen.label_title = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_title, "NAS Monitor");
    lv_obj_set_style_text_color(s_screen.label_title, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_title, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_title, LV_ALIGN_LEFT_MID, 55, 0);

    /* 时间 */
    s_screen.label_time = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_time, "--:--:--");
    lv_obj_set_style_text_color(s_screen.label_time, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_time, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_time, LV_ALIGN_LEFT_MID, 160, 0);

    /* 上传速度（状态栏内显示，用 fmt_bps_arrow 格式化） */
    s_screen.label_up = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_up, "^ 0B/s");
    lv_obj_set_style_text_color(s_screen.label_up, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_up, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_up, LV_ALIGN_LEFT_MID, 250, 0);

    /* 下载速度 */
    s_screen.label_down = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_down, "v 0B/s");
    lv_obj_set_style_text_color(s_screen.label_down, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_down, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_down, LV_ALIGN_LEFT_MID, 350, 0);

    /* IP */
    s_screen.label_ip = lv_label_create(status_bar);
    lv_label_set_text(s_screen.label_ip, "IP: --");
    lv_obj_set_style_text_color(s_screen.label_ip, theme_get().text, 0);
    lv_obj_set_style_text_font(s_screen.label_ip, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.label_ip, LV_ALIGN_LEFT_MID, 445, 0);

    /* WiFi 图标 */
    s_screen.icon_wifi = lv_label_create(status_bar);
    lv_label_set_text(s_screen.icon_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_screen.icon_wifi, theme_get().dim, 0);
    lv_obj_set_style_text_font(s_screen.icon_wifi, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.icon_wifi, LV_ALIGN_LEFT_MID, 594, 0);

    /* 蓝牙图标 */
    s_screen.icon_bt = lv_label_create(status_bar);
    lv_label_set_text(s_screen.icon_bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_screen.icon_bt, theme_get().dim, 0);
    lv_obj_set_style_text_font(s_screen.icon_bt, &lv_font_montserrat_12, 0);
    lv_obj_align(s_screen.icon_bt, LV_ALIGN_RIGHT_MID, -5, 0);

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

/* ============ 单个速率区（下载或上传） ============ */

static void create_net_section(lv_obj_t *parent,
                               lv_coord_t y_offset,
                               const char *title,
                               lv_color_t color,
                               lv_obj_t **out_title,
                               lv_obj_t **out_max,
                               lv_obj_t **out_cur,
                               lv_obj_t **out_chart,
                               lv_chart_series_t **out_series)
{
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_set_size(section, 640, 48);
    set_default_style(section);
    lv_obj_align(section, LV_ALIGN_TOP_MID, 0, y_offset);

    /* 标题行："↓ 下载" 左对齐 */
    lv_obj_t *lbl_title = lv_label_create(section);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, color, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 4, 2);
    *out_title = lbl_title;

    /* max 标签（居中） */
    lv_obj_t *lbl_max = lv_label_create(section);
    lv_label_set_text(lbl_max, "max --");
    lv_obj_set_style_text_color(lbl_max, theme_get().text_dim, 0);
    lv_obj_set_style_text_font(lbl_max, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_max, LV_ALIGN_TOP_MID, 0, 2);
    *out_max = lbl_max;

    /* 当前值标签（右侧，大字号） */
    lv_obj_t *lbl_cur = lv_label_create(section);
    lv_label_set_text(lbl_cur, "--");
    lv_obj_set_style_text_color(lbl_cur, theme_get().text, 0);
    lv_obj_set_style_text_font(lbl_cur, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_cur, LV_ALIGN_TOP_RIGHT, -4, 0);
    *out_cur = lbl_cur;

    /* 柱状图 */
    lv_obj_t *chart = lv_chart_create(section);
    lv_obj_set_size(chart, 632, 32);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(chart, NET_HISTORY_LEN);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_RANGE_FLOOR);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

    lv_obj_set_style_bg_color(chart, theme_get().bg, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* 隐藏图表内置的刻度线/网格点 */
    lv_obj_set_style_line_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(chart, 1, 0);

    lv_chart_series_t *ser = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, ser, 0);

    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, -2);
    *out_chart = chart;
    *out_series = ser;
}

/* ============ 网口详情卡片 ============ */

/* 在两个图表下方创建网口信息卡片（横向排列）
 *
 * 布局（y=133 起，高 35px）：
 *   ┌─────────────────────┐
 *   │ ● eth0              │  ← 名称行（圆点 + 接口名）
 *   │ 192.168.1.100       │  ← IP 地址
 *   │ ↓ 12.0MB/s ↑ 2.0MB/s│  ← 下载/上传速率
 *   └─────────────────────┘
 *
 * 4 张卡片横向排列，每张 150×35，间隔 8px，整体居中。
 */
static void create_interface_cards(lv_obj_t *parent)
{
    const lv_coord_t card_w = 150;
    const lv_coord_t card_h = 35;
    const lv_coord_t card_y = 133;  /* 在两个 48px 图表区域之下 */
    const lv_coord_t gap = 8;
    /* 居中：4 卡片 + 3 间隔 = 4*150 + 3*8 = 624，起始 x = (640-624)/2 = 8 */
    const lv_coord_t start_x = (640 -
        (MAX_NETWORK_INTERFACES * card_w + (MAX_NETWORK_INTERFACES - 1) * gap)) / 2;

    for (int i = 0; i < MAX_NETWORK_INTERFACES; i++) {
        /* 卡片容器 */
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_align(card, LV_ALIGN_TOP_LEFT, start_x + i * (card_w + gap), card_y);

        /* 卡片样式：深色背景、圆角 4px、边框默认灰色（离线） */
        lv_obj_set_style_bg_color(card, theme_get().card_bg, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, theme_get().inactive, 0);
        lv_obj_set_style_radius(card, 4, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        /* 子容器需要 GESTURE_BUBBLE 让手势冒泡到 screen */
        lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);

        /* 状态指示圆点（6×6 像素，活跃=青色，离线=灰色） */
        lv_obj_t *dot = lv_obj_create(card);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_set_style_bg_color(dot, theme_get().dim, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 4, 4);
        s_iface_dots[i] = dot;

        /* 第 1 行：接口名（圆点右侧） */
        lv_obj_t *lbl_name = lv_label_create(card);
        lv_label_set_text(lbl_name, "--");
        lv_obj_set_style_text_color(lbl_name, theme_get().text, 0);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 14, 0);
        s_iface_labels[i][0] = lbl_name;

        /* 第 2 行：IP 地址 */
        lv_obj_t *lbl_ip = lv_label_create(card);
        lv_label_set_text(lbl_ip, "IP: --");
        lv_obj_set_style_text_color(lbl_ip, theme_get().text_dim, 0);
        lv_obj_set_style_text_font(lbl_ip, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_ip, LV_ALIGN_TOP_LEFT, 4, 12);
        s_iface_labels[i][1] = lbl_ip;

        /* 第 3 行左：下载速率（↓ 青色） */
        lv_obj_t *lbl_rx = lv_label_create(card);
        lv_label_set_text(lbl_rx, "\xE2\x86\x93 --"); /* "↓ --" */
        lv_obj_set_style_text_color(lbl_rx, theme_get().accent, 0);
        lv_obj_set_style_text_font(lbl_rx, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_rx, LV_ALIGN_TOP_LEFT, 4, 23);
        s_iface_labels[i][2] = lbl_rx;

        /* 第 3 行右：上传速率（↑ 黄色） */
        lv_obj_t *lbl_tx = lv_label_create(card);
        lv_label_set_text(lbl_tx, "\xE2\x86\x91 --"); /* "↑ --" */
        lv_obj_set_style_text_color(lbl_tx, theme_get().up_accent, 0);
        lv_obj_set_style_text_font(lbl_tx, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_tx, LV_ALIGN_TOP_LEFT, 80, 23);
        s_iface_labels[i][3] = lbl_tx;

        s_iface_cards[i] = card;

        /* 默认隐藏，等数据更新时显示 */
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ============ 公开 API ============ */

void ui_Screen_NetDetail_screen_init(void)
{
    if (ui_Screen_NetDetail != NULL) {
        ui_Screen_NetDetail_screen_destroy();
    }

    memset(&s_screen, 0, sizeof(s_screen));
    /* 重置网口卡片控件指针 */
    memset(s_iface_cards, 0, sizeof(s_iface_cards));
    memset(s_iface_labels, 0, sizeof(s_iface_labels));
    memset(s_iface_dots, 0, sizeof(s_iface_dots));
    /* 重置历史缓冲区，避免上次会话数据残留 */
    s_history_count = 0;
    s_history_head = 0;
    memset(s_rx_history, 0, sizeof(s_rx_history));
    memset(s_tx_history, 0, sizeof(s_tx_history));

    ui_Screen_NetDetail = lv_obj_create(NULL);
    s_screen.screen = ui_Screen_NetDetail;
    ESP_LOGW(TAG, "[step1] screen created");
    lv_obj_set_size(ui_Screen_NetDetail, 640, 172);
    lv_obj_clear_flag(ui_Screen_NetDetail, LV_OBJ_FLAG_SCROLLABLE);
    /* 重要：screen 不设 GESTURE_BUBBLE，让其能接收子对象冒泡上来的手势 */
    lv_obj_set_style_bg_color(ui_Screen_NetDetail, theme_get().bg, 0);
    lv_obj_set_style_pad_all(ui_Screen_NetDetail, 0, 0);

    create_status_bar(ui_Screen_NetDetail);
    ESP_LOGW(TAG, "[step2] status bar created");

    /* 下载区 (y=37, 高 48px) */
    create_net_section(ui_Screen_NetDetail, 37,
                       "\xE2\x86\x93 \xE4\xB8\x8B\xE8\xBD\xBD" /* "↓ 下载" UTF-8 */,
                       theme_get().accent,
                       &s_screen.label_down_title,
                       &s_screen.label_down_max,
                       &s_screen.label_down_cur,
                       &s_screen.chart_down,
                       &s_screen.series_down);
    ESP_LOGW(TAG, "[step3] down section created");

    /* 上传区 (y=85, 高 48px) */
    create_net_section(ui_Screen_NetDetail, 85,
                       "\xE2\x86\x91 \xE4\xB8\x8A\xE4\xBC\xA0" /* "↑ 上传" UTF-8 */,
                       theme_get().up_accent,
                       &s_screen.label_up_title,
                       &s_screen.label_up_max,
                       &s_screen.label_up_cur,
                       &s_screen.chart_up,
                       &s_screen.series_up);
    ESP_LOGW(TAG, "[step4] up section created");

    /* 网口详情卡片 (y=133, 高 35px) */
    create_interface_cards(ui_Screen_NetDetail);
    ESP_LOGW(TAG, "[step5] interface cards created");

    /* 注册手势：右滑返回 Overview（实现在 ui_events.c） */
    lv_obj_add_event_cb(ui_Screen_NetDetail, ui_event_Screen_NetDetail_gesture, LV_EVENT_GESTURE, NULL);
    ESP_LOGW(TAG, "[step6] gesture registered");

    ESP_LOGI(TAG, "NetDetail screen initialized");
}

void ui_Screen_NetDetail_screen_destroy(void)
{
    if (ui_Screen_NetDetail) {
        lv_obj_del(ui_Screen_NetDetail);
        ui_Screen_NetDetail = NULL;
    }
    memset(&s_screen, 0, sizeof(s_screen));
    /* 清空网口卡片控件指针（对象已随 screen 一起删除） */
    memset(s_iface_cards, 0, sizeof(s_iface_cards));
    memset(s_iface_labels, 0, sizeof(s_iface_labels));
    memset(s_iface_dots, 0, sizeof(s_iface_dots));
    ESP_LOGI(TAG, "NetDetail screen destroyed");
}

void netdetail_screen_update_time(const char *time_str)
{
    if (s_screen.label_time && time_str) {
        lv_label_set_text(s_screen.label_time, time_str);
    }
}

void netdetail_screen_update_network(uint32_t tx_bps, uint32_t rx_bps)
{
    if (!ui_Screen_NetDetail) return;

    /* 1. 更新状态栏速率文本（带方向箭头） */
    if (s_screen.label_up) {
        static char up_str[24];
        fmt_bps_arrow(up_str, sizeof(up_str), tx_bps, true);
        lv_label_set_text(s_screen.label_up, up_str);
    }
    if (s_screen.label_down) {
        static char down_str[24];
        fmt_bps_arrow(down_str, sizeof(down_str), rx_bps, false);
        lv_label_set_text(s_screen.label_down, down_str);
    }

    /* 2. 追加到历史环形缓冲区 */
    history_push(rx_bps, tx_bps);

    /* 3. 刷新两个图表 */
    if (s_screen.chart_down && s_screen.series_down) {
        chart_autoscale(s_screen.chart_down, s_rx_history);
        chart_refresh(s_screen.chart_down, s_screen.series_down, s_rx_history);
    }
    if (s_screen.chart_up && s_screen.series_up) {
        chart_autoscale(s_screen.chart_up, s_tx_history);
        chart_refresh(s_screen.chart_up, s_screen.series_up, s_tx_history);
    }

    /* 4. 更新 max 标签 */
    if (s_screen.label_down_max) {
        uint32_t max_rx = history_max(s_rx_history);
        static char max_rx_str[24];
        fmt_bps(max_rx_str, sizeof(max_rx_str), max_rx);
        static char max_rx_lbl[32];
        snprintf(max_rx_lbl, sizeof(max_rx_lbl), "max %s", max_rx_str);
        lv_label_set_text(s_screen.label_down_max, max_rx_lbl);
    }
    if (s_screen.label_up_max) {
        uint32_t max_tx = history_max(s_tx_history);
        static char max_tx_str[24];
        fmt_bps(max_tx_str, sizeof(max_tx_str), max_tx);
        static char max_tx_lbl[32];
        snprintf(max_tx_lbl, sizeof(max_tx_lbl), "max %s", max_tx_str);
        lv_label_set_text(s_screen.label_up_max, max_tx_lbl);
    }

    /* 5. 更新当前值标签 */
    if (s_screen.label_down_cur) {
        static char cur_rx_str[24];
        fmt_bps(cur_rx_str, sizeof(cur_rx_str), rx_bps);
        lv_label_set_text(s_screen.label_down_cur, cur_rx_str);
    }
    if (s_screen.label_up_cur) {
        static char cur_tx_str[24];
        fmt_bps(cur_tx_str, sizeof(cur_tx_str), tx_bps);
        lv_label_set_text(s_screen.label_up_cur, cur_tx_str);
    }
}

void netdetail_screen_update_ip(const char *ip_str)
{
    if (s_screen.label_ip && ip_str) {
        static char ip_buf[24];
        snprintf(ip_buf, sizeof(ip_buf), "IP: %s", ip_str);
        lv_label_set_text(s_screen.label_ip, ip_buf);
    }
}

void netdetail_screen_update_wifi(bool connected)
{
    if (s_screen.icon_wifi) {
        lv_obj_set_style_text_color(s_screen.icon_wifi,
            connected ? theme_get().ok : theme_get().dim, 0);
    }
}

void netdetail_screen_update_interfaces(const NasInterfaceInfo *interfaces, int count)
{
    if (!ui_Screen_NetDetail) return;
    if (!interfaces || count < 0) return;
    if (count > MAX_NETWORK_INTERFACES) count = MAX_NETWORK_INTERFACES;

    for (int i = 0; i < MAX_NETWORK_INTERFACES; i++) {
        lv_obj_t *card = s_iface_cards[i];
        if (!card) continue;

        /* 仅当接口存在且有名称时才显示卡片 */
        if (i < count && interfaces[i].name[0] != '\0') {
            lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);

            const NasInterfaceInfo *iface = &interfaces[i];

            /* 边框颜色：活跃=青色，离线=灰色 */
            lv_obj_set_style_border_color(card,
                iface->active ? theme_get().accent : theme_get().inactive, 0);

            /* 状态圆点颜色 */
            if (s_iface_dots[i]) {
                lv_obj_set_style_bg_color(s_iface_dots[i],
                    iface->active ? theme_get().accent : theme_get().dim, 0);
            }

            /* 接口名 */
            if (s_iface_labels[i][0]) {
                lv_label_set_text(s_iface_labels[i][0], iface->name);
            }

            /* IP 地址 */
            if (s_iface_labels[i][1]) {
                lv_label_set_text(s_iface_labels[i][1], iface->ip);
            }

            /* 下载速率（用 fmt_bps 格式化，加 ↓ 前缀） */
            if (s_iface_labels[i][2]) {
                static char rx_str[24];
                char bps_str[16];
                fmt_bps(bps_str, sizeof(bps_str), iface->rx_bps);
                snprintf(rx_str, sizeof(rx_str), "\xE2\x86\x93 %s", bps_str); /* "↓ xxx" */
                lv_label_set_text(s_iface_labels[i][2], rx_str);
            }

            /* 上传速率（用 fmt_bps 格式化，加 ↑ 前缀） */
            if (s_iface_labels[i][3]) {
                static char tx_str[24];
                char bps_str[16];
                fmt_bps(bps_str, sizeof(bps_str), iface->tx_bps);
                snprintf(tx_str, sizeof(tx_str), "\xE2\x86\x91 %s", bps_str); /* "↑ xxx" */
                lv_label_set_text(s_iface_labels[i][3], tx_str);
            }
        } else {
            /* 无此接口，隐藏卡片 */
            lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
