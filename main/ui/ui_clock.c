#include "ui_clock.h"

#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "user_config.h"
#include "event_bus.h"

#define CLOCK_BG_PATH "/sdcard/clock_bg.bin"

static const char *TAG = "ui_clock";

/* Custom JetBrains Mono Bold fonts, generated via lv_font_conv. */
extern const lv_font_t font_jbmono_24;
extern const lv_font_t font_jbmono_48;
extern const lv_font_t font_jbmono_64;
extern const lv_font_t font_jbmono_96;

/* ===== 1. 对象定义 ===== */
lv_obj_t *ui_Clock                = NULL;
lv_obj_t *ui_Clock_label_time      = NULL;
lv_obj_t *ui_Clock_label_ms        = NULL;
lv_obj_t *ui_Clock_label_date      = NULL;
lv_obj_t *ui_Clock_label_tz        = NULL;
lv_obj_t *ui_Clock_canvas_sunmap   = NULL;
lv_obj_t *ui_Clock_icon_wifi       = NULL;
lv_obj_t *ui_Clock_icon_bt         = NULL;

/* ===== 2. 静态样式变量 ===== */
static lv_style_t style_tile_bg;
static lv_style_t style_date;
static lv_style_t style_tz;
static lv_style_t style_icon;
static bool styles_inited = false;

/* 内部状态变量 */
static lv_color_t *g_sunmap_buf    = NULL;
static int         g_sunmap_w      = 0;
static int         g_sunmap_h      = 0;
static lv_timer_t *g_clock_timer   = NULL;
static lv_timer_t *g_clock_ms_timer = NULL;
static lv_timer_t *g_sunmap_timer  = NULL;

static void init_styles(void)
{
    if (styles_inited) return;

    lv_style_init(&style_tile_bg);
    lv_style_set_bg_color(&style_tile_bg, lv_color_black());
    lv_style_set_bg_opa(&style_tile_bg, LV_OPA_COVER);
    lv_style_set_pad_all(&style_tile_bg, 0);

    lv_style_init(&style_date);
    lv_style_set_text_color(&style_date, lv_color_make(0xd0, 0xd0, 0xd0));
    lv_style_set_bg_color(&style_date, lv_color_black());
    lv_style_set_bg_opa(&style_date, LV_OPA_70);
    lv_style_set_pad_hor(&style_date, 6);
    lv_style_set_radius(&style_date, 3);

    lv_style_init(&style_tz);
    lv_style_set_text_color(&style_tz, lv_color_make(0xa0, 0xa0, 0xa0));
    lv_style_set_bg_color(&style_tz, lv_color_black());
    lv_style_set_bg_opa(&style_tz, LV_OPA_60);
    lv_style_set_pad_hor(&style_tz, 4);

    lv_style_init(&style_icon);
    lv_style_set_bg_color(&style_icon, lv_color_black());
    lv_style_set_bg_opa(&style_icon, LV_OPA_60);
    lv_style_set_pad_hor(&style_icon, 3);

    styles_inited = true;
}

/* ===== 业务辅助函数（UI 相关，供回调/定时器调用） ===== */

static const lv_font_t *clock_size_to_font(uint8_t size)
{
    switch (size) {
        case 0: return &font_jbmono_24;
        case 1: return &font_jbmono_48;
        case 2: return &font_jbmono_64;
        default:return &font_jbmono_96;
    }
}

void tz_apply_current(void)
{
    uint16_t i = g_cfg.tz_idx;
    if (i >= TZ_CITY_COUNT) i = TZ_DEFAULT_CITY_INDEX;
    setenv("TZ", k_tz_cities[i].posix_tz, 1);
    tzset();
}

const char *tz_current_city_name(void)
{
    uint16_t i = g_cfg.tz_idx;
    if (i >= TZ_CITY_COUNT) i = TZ_DEFAULT_CITY_INDEX;
    return k_tz_cities[i].name;
}

static void get_display_time(struct tm *out)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t t = tv.tv_sec;
    localtime_r(&t, out);
}

/* ===== 定时器回调 ===== */

static void clock_ms_update_cb(lv_timer_t *t)
{
    (void)t;
    if (!ui_Clock_label_ms) return;
    if (!g_cfg.show_clock || g_cfg.clock_text[0] || !g_cfg.show_ms) {
        if (!lv_obj_has_flag(ui_Clock_label_ms, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(ui_Clock_label_ms, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (lv_obj_has_flag(ui_Clock_label_ms, LV_OBJ_FLAG_HIDDEN))
        lv_obj_clear_flag(ui_Clock_label_ms, LV_OBJ_FLAG_HIDDEN);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int ms = (int)(tv.tv_usec / 1000);
    if (ms < 0) ms = 0;
    if (ms > 999) ms = 999;
    char buf[8];
    snprintf(buf, sizeof(buf), ".%03d", ms);
    lv_label_set_text(ui_Clock_label_ms, buf);
}

void clock_update_cb(lv_timer_t *t)
{
    (void)t;
    if (!ui_Clock_label_time || !ui_Clock_label_date) return;
    if (!g_cfg.show_clock) {
        if (!lv_obj_has_flag(ui_Clock_label_time, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(ui_Clock_label_time, LV_OBJ_FLAG_HIDDEN);
    } else if (lv_obj_has_flag(ui_Clock_label_time, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(ui_Clock_label_time, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_cfg.clock_text[0]) {
        lv_label_set_text(ui_Clock_label_time, g_cfg.clock_text);
    }
    struct tm tm;
    get_display_time(&tm);
    char buf[64];
    int yyyy = tm.tm_year + 1900;
    int mm   = tm.tm_mon + 1;
    int dd   = tm.tm_mday;
    if (yyyy < 0) yyyy = 0;
    if (yyyy > 9999) yyyy = 9999;
    if (mm < 1) mm = 1;
    if (mm > 12) mm = 12;
    if (dd < 1) dd = 1;
    if (dd > 31) dd = 31;
    switch (g_cfg.date_fmt) {
    case 1:  snprintf(buf, sizeof(buf), "%02d.%02d.%04d", dd, mm, yyyy); break;
    case 2:  snprintf(buf, sizeof(buf), "%02d.%02d.%04d", mm, dd, yyyy); break;
    default: snprintf(buf, sizeof(buf), "%04d.%02d.%02d", yyyy, mm, dd); break;
    }
    lv_label_set_text(ui_Clock_label_date, buf);
    int hh = tm.tm_hour, mi = tm.tm_min, ss = tm.tm_sec;
    if (hh < 0) hh = 0;
    if (hh > 23) hh = 23;
    if (mi < 0) mi = 0;
    if (mi > 59) mi = 59;
    if (ss < 0) ss = 0;
    if (ss > 60) ss = 60;
    int disp_h = hh;
    if (!g_cfg.hour24) {
        disp_h = hh % 12;
        if (disp_h == 0) disp_h = 12;
    }
    if (!g_cfg.clock_text[0]) {
        if (g_cfg.show_seconds) {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", disp_h, mi, ss);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d",       disp_h, mi);
        }
        lv_label_set_text(ui_Clock_label_time, buf);
    }
}

/* Forward decl: sunmap update callback. */
static void sunmap_update_cb(lv_timer_t *t);

/* ===== 3. 事件回调函数 ===== */
/* (clock tile 当前无 LVGL 事件回调，状态更新通过定时器和外部 API 完成) */

/* ===== 公共 UI API ===== */

/* 事件总线 handler：时钟布局变更 */
static void on_clock_layout_changed_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    if (lvgl_lock(50)) { clock_apply_layout(); lvgl_unlock(); }
}

/* 事件总线 handler：背景变更 */
static void on_clock_bg_changed_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    if (lvgl_lock(50)) { clock_bg_apply(); lvgl_unlock(); }
}

/* 事件总线 handler：时间格式变更（时区/时制/秒/毫秒/日期格式） */
static void on_clock_time_fmt_changed_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    clock_update_tz_label();
    clock_update_cb(NULL);
}

void ui_Clock_cleanup(void)
{
    /* 取消事件订阅 */
    event_bus_unsubscribe(EVENT_CLOCK_LAYOUT_CHANGED, on_clock_layout_changed_evt);
    event_bus_unsubscribe(EVENT_CLOCK_BG_CHANGED, on_clock_bg_changed_evt);
    event_bus_unsubscribe(EVENT_CLOCK_TIME_FORMAT_CHANGED, on_clock_time_fmt_changed_evt);

    if (g_clock_timer)     { lv_timer_del(g_clock_timer);     g_clock_timer     = NULL; }
    if (g_clock_ms_timer)  { lv_timer_del(g_clock_ms_timer);  g_clock_ms_timer  = NULL; }
    if (g_sunmap_timer)    { lv_timer_del(g_sunmap_timer);    g_sunmap_timer    = NULL; }
    if (g_sunmap_buf)      { lv_mem_free(g_sunmap_buf);       g_sunmap_buf      = NULL; }
    ui_Clock                = NULL;
    ui_Clock_label_time     = NULL;
    ui_Clock_label_ms       = NULL;
    ui_Clock_label_date     = NULL;
    ui_Clock_label_tz       = NULL;
    ui_Clock_canvas_sunmap  = NULL;
    ui_Clock_icon_wifi      = NULL;
    ui_Clock_icon_bt        = NULL;
    g_sunmap_w = 0;
    g_sunmap_h = 0;
}

void clock_ms_timer_pause(void)
{
    if (g_clock_ms_timer) lv_timer_pause(g_clock_ms_timer);
}

void clock_ms_timer_resume(void)
{
    if (g_clock_ms_timer) lv_timer_resume(g_clock_ms_timer);
}

void clock_update_tz_label(void)
{
    if (ui_Clock_label_tz) lv_label_set_text(ui_Clock_label_tz, tz_current_city_name());
}

void clock_set_wifi_icon_color(lv_color_t color)
{
    if (ui_Clock_icon_wifi) lv_obj_set_style_text_color(ui_Clock_icon_wifi, color, 0);
}

void clock_set_bt_icon_color(lv_color_t color)
{
    if (ui_Clock_icon_bt) lv_obj_set_style_text_color(ui_Clock_icon_bt, color, 0);
}

void clock_apply_layout(void)
{
    if (!ui_Clock_label_time || !ui_Clock_label_ms) return;
    if (!g_cfg.show_clock) {
        lv_obj_add_flag(ui_Clock_label_time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_Clock_label_ms,   LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(ui_Clock_label_time, LV_OBJ_FLAG_HIDDEN);
    const lv_font_t *time_font = clock_size_to_font(g_cfg.clock_size);
    const lv_font_t *ms_font = clock_size_to_font(
        g_cfg.clock_size > 0 ? (uint8_t)(g_cfg.clock_size - 1) : 0);
    if (g_cfg.clock_size == 0) ms_font = &lv_font_montserrat_12;

    uint32_t v = g_cfg.clock_rgba;
    uint8_t  rr = (uint8_t)(v >> 24);
    uint8_t  gg = (uint8_t)(v >> 16);
    uint8_t  bb = (uint8_t)(v >> 8);
    uint8_t  aa = (uint8_t)(v);
    lv_color_t col = lv_color_make(rr, gg, bb);
    lv_obj_set_style_text_font(ui_Clock_label_time, time_font, 0);
    lv_obj_set_style_text_color(ui_Clock_label_time, col, 0);
    lv_obj_set_style_text_opa(ui_Clock_label_time, aa ? aa : 0xFF, 0);

    lv_color_t ms_col = lv_color_make((uint8_t)(rr * 3 / 4),
                                      (uint8_t)(gg * 3 / 4),
                                      (uint8_t)(bb * 3 / 4));
    lv_obj_set_style_text_font(ui_Clock_label_ms, ms_font, 0);
    lv_obj_set_style_text_color(ui_Clock_label_ms, ms_col, 0);
    lv_obj_set_style_text_opa(ui_Clock_label_ms, aa ? aa : 0xFF, 0);

    bool showing_ms = g_cfg.show_ms && !g_cfg.clock_text[0];
    lv_obj_align(ui_Clock_label_time, LV_ALIGN_CENTER,
                 g_cfg.clock_x, g_cfg.clock_y);
    if (showing_ms) {
        lv_obj_align_to(ui_Clock_label_ms, ui_Clock_label_time,
                        LV_ALIGN_OUT_RIGHT_BOTTOM, 0,
                        g_cfg.clock_size >= 3 ? -8 : -2);
        lv_obj_clear_flag(ui_Clock_label_ms, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui_Clock_label_ms, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ===== 4. tile 创建函数 ===== */

void ui_Clock_create(lv_obj_t *parent)
{
    init_styles();

    /* tile 容器 */
    ui_Clock = parent;
    lv_obj_clear_flag(ui_Clock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(ui_Clock, &style_tile_bg, 0);

    /* 日光地图背景 */
    int W = disp_driver_get_canvas_w();
    int H = disp_driver_get_canvas_h();
    g_sunmap_w = W;
    g_sunmap_h = H;
    if (g_sunmap_buf) { lv_mem_free(g_sunmap_buf); g_sunmap_buf = NULL; }
    g_sunmap_buf = (lv_color_t *)lv_mem_alloc((size_t)W * H * sizeof(lv_color_t));
    if (g_sunmap_buf) {
        ui_Clock_canvas_sunmap = lv_canvas_create(ui_Clock);
        lv_canvas_set_buffer(ui_Clock_canvas_sunmap, g_sunmap_buf, W, H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(ui_Clock_canvas_sunmap, LV_ALIGN_CENTER, 0, 0);
        clock_bg_apply();
        if (!g_sunmap_timer) {
            g_sunmap_timer = lv_timer_create(sunmap_update_cb, SUNMAP_RECOMPUTE_MS, NULL);
        }
    }

    /* 日期标签 */
    ui_Clock_label_date = lv_label_create(ui_Clock);
    lv_label_set_text(ui_Clock_label_date, "----.--.--");
    lv_obj_add_style(ui_Clock_label_date, &style_date, 0);
    lv_obj_set_style_text_font(ui_Clock_label_date, i18n_font(), 0);
    lv_obj_align(ui_Clock_label_date, LV_ALIGN_TOP_MID, 0, 4);

    /* 时间标签 */
    ui_Clock_label_time = lv_label_create(ui_Clock);
    lv_label_set_text(ui_Clock_label_time, "--:--:--");
    lv_obj_set_style_bg_opa(ui_Clock_label_time, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ui_Clock_label_time, 0, 0);

    /* 毫秒标签 */
    ui_Clock_label_ms = lv_label_create(ui_Clock);
    lv_label_set_text(ui_Clock_label_ms, ".000");
    lv_obj_set_style_bg_opa(ui_Clock_label_ms, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ui_Clock_label_ms, 0, 0);

    clock_apply_layout();

    /* 时区标签 */
    ui_Clock_label_tz = lv_label_create(ui_Clock);
    lv_label_set_text(ui_Clock_label_tz, tz_current_city_name());
    lv_obj_add_style(ui_Clock_label_tz, &style_tz, 0);
    lv_obj_set_style_text_font(ui_Clock_label_tz, i18n_font(), 0);
    lv_obj_align(ui_Clock_label_tz, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

    /* Wi-Fi 图标 */
    ui_Clock_icon_wifi = lv_label_create(ui_Clock);
    lv_label_set_text(ui_Clock_icon_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(ui_Clock_icon_wifi,
                                 lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_add_style(ui_Clock_icon_wifi, &style_icon, 0);
    lv_obj_set_style_text_font(ui_Clock_icon_wifi, i18n_font(), 0);
    lv_obj_align(ui_Clock_icon_wifi, LV_ALIGN_TOP_RIGHT, -4, 4);

    /* 蓝牙图标 */
    ui_Clock_icon_bt = lv_label_create(ui_Clock);
    lv_label_set_text(ui_Clock_icon_bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(ui_Clock_icon_bt,
                                 lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_add_style(ui_Clock_icon_bt, &style_icon, 0);
    lv_obj_set_style_text_font(ui_Clock_icon_bt, i18n_font(), 0);
    lv_obj_align(ui_Clock_icon_bt, LV_ALIGN_TOP_RIGHT, -28, 4);

    if (!g_clock_timer) {
        g_clock_timer = lv_timer_create(clock_update_cb, 500, NULL);
    }
    if (!g_clock_ms_timer) {
        g_clock_ms_timer = lv_timer_create(clock_ms_update_cb, 16, NULL);
    }
    clock_update_cb(NULL);
    clock_ms_update_cb(NULL);

    /* 订阅事件总线：配置变更时刷新 UI */
    event_bus_subscribe(EVENT_CLOCK_LAYOUT_CHANGED, on_clock_layout_changed_evt, NULL);
    event_bus_subscribe(EVENT_CLOCK_BG_CHANGED,     on_clock_bg_changed_evt,     NULL);
    event_bus_subscribe(EVENT_CLOCK_TIME_FORMAT_CHANGED, on_clock_time_fmt_changed_evt, NULL);
}

/* ===== 5. tile 清理函数 ===== */
/* (已在上方定义 ui_Clock_cleanup，保留 clock_cleanup 别名供外部调用) */

/* ===== Sunmap 业务逻辑 ===== */

void sunmap_redraw(void)
{
    if (!g_sunmap_buf || !ui_Clock_canvas_sunmap) return;
    const int W = g_sunmap_w;
    const int H = g_sunmap_h;
    theme_palette_t pal = theme_get();
    const lv_color_t c_water_n = pal.sunmap_water_n;
    const lv_color_t c_water_d = pal.sunmap_water_d;
    const lv_color_t c_land_n  = pal.sunmap_land_n;
    const lv_color_t c_land_d  = pal.sunmap_land_d;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm tm;
    gmtime_r(&now, &tm);
    int doy = tm.tm_yday;
    float utc_hours = tm.tm_hour + tm.tm_min / 60.0f + tm.tm_sec / 3600.0f;
    float sun_lon_deg = -15.0f * (utc_hours - 12.0f);
    float sun_lat_deg = 23.44f * sinf(2.0f * (float)M_PI * (doy - 81) / 365.0f);
    float sl     = sun_lat_deg * (float)M_PI / 180.0f;
    float so     = sun_lon_deg * (float)M_PI / 180.0f;
    float sin_sl = sinf(sl);
    float cos_sl = cosf(sl);

    for (int y = 0; y < H; y++) {
        float lat = (90.0f - (y + 0.5f) * (180.0f / H)) * (float)M_PI / 180.0f;
        float sin_p = sinf(lat);
        float cos_p = cosf(lat);
        const float lon0 = -(float)M_PI;
        const float dlon = 2.0f * (float)M_PI / W;
        for (int x = 0; x < W; x++) {
            float lon = lon0 + (x + 0.5f) * dlon;
            float c = sin_sl * sin_p + cos_sl * cos_p * cosf(lon - so);
            int land = landmask_get(x, y);
            lv_color_t color;
            if (c > 0) color = land ? c_land_d : c_water_d;
            else       color = land ? c_land_n : c_water_n;
            g_sunmap_buf[y * W + x] = color;
        }
    }

    lv_obj_invalidate(ui_Clock_canvas_sunmap);
}

static void sunmap_update_cb(lv_timer_t *t)
{
    (void)t;
    if (g_cfg.bg_mode == 0) sunmap_redraw();
}

/* ===== 背景加载 / 应用 ===== */

static int clock_bg_load_raw(const char *path)
{
    if (!g_sunmap_buf || g_sunmap_w == 0) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "bg load: open %s failed", path);
        return -1;
    }
    size_t need = (size_t)g_sunmap_w * g_sunmap_h * sizeof(uint16_t);
    size_t got = fread(g_sunmap_buf, 1, need, f);
    fclose(f);
    if (got != need) {
        ESP_LOGW(TAG, "bg load: short read %zu/%zu", got, need);
        return -1;
    }
    if (ui_Clock_canvas_sunmap) lv_obj_invalidate(ui_Clock_canvas_sunmap);
    return 0;
}

void clock_bg_apply(void)
{
    if (!g_sunmap_buf) return;
    switch (g_cfg.bg_mode) {
        case 1:
            if (clock_bg_load_raw(CLOCK_BG_PATH) != 0) {
                sunmap_redraw();
            }
            break;
        case 2:
            if (clock_bg_load_raw(CLOCK_BG_PATH) != 0) {
                sunmap_redraw();
            }
            break;
        case 3: {
            uint8_t r = (uint8_t)(g_cfg.bg_color >> 24);
            uint8_t gn= (uint8_t)(g_cfg.bg_color >> 16);
            uint8_t b = (uint8_t)(g_cfg.bg_color >> 8);
            uint16_t rgb565 = (uint16_t)(((r & 0xF8) << 8) |
                                          ((gn & 0xFC) << 3) |
                                          ((b & 0xF8) >> 3));
            uint16_t v = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
            uint16_t *p = (uint16_t *)g_sunmap_buf;
            int n = g_sunmap_w * g_sunmap_h;
            for (int i = 0; i < n; i++) p[i] = v;
            if (ui_Clock_canvas_sunmap) lv_obj_invalidate(ui_Clock_canvas_sunmap);
            break;
        }
        default:
            sunmap_redraw();
            break;
    }
}
