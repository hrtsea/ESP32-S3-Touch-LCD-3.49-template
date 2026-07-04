#include "ui_clock.h"

#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "user_config.h"

#define CLOCK_BG_PATH "/sdcard/clock_bg.bin"

static const char *TAG = "ui_clock";

/* Custom JetBrains Mono Bold fonts, generated via lv_font_conv. */
extern const lv_font_t font_jbmono_24;
extern const lv_font_t font_jbmono_48;
extern const lv_font_t font_jbmono_64;
extern const lv_font_t font_jbmono_96;

/* Tileview-based UI: hello | clock (start) | sunmap. Swipe horizontally. */
lv_obj_t  *g_clock_time_label = NULL;
lv_obj_t  *g_clock_ms_label   = NULL;
lv_obj_t  *g_clock_date_label = NULL;
lv_obj_t  *g_clock_tz_label   = NULL;
lv_obj_t  *g_sunmap_canvas    = NULL;
lv_color_t *g_sunmap_buf      = NULL;
int        g_sunmap_w         = 0;
int        g_sunmap_h         = 0;
lv_timer_t *g_clock_timer     = NULL;
lv_timer_t *g_clock_ms_timer  = NULL;
lv_timer_t *g_sunmap_timer    = NULL;
lv_obj_t  *g_clock_wifi_icon  = NULL;
lv_obj_t  *g_clock_bt_icon    = NULL;

/* ---------------------- Clock tile ---------------------- */

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

static void clock_ms_update_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_clock_ms_label) return;
    /* Hide ms when the whole clock is off, when a custom text is set
       (the .mmm field doesn't apply to "Hello world"), or when show_ms
       is disabled. */
    if (!g_cfg.show_clock || g_cfg.clock_text[0] || !g_cfg.show_ms) {
        if (!lv_obj_has_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (lv_obj_has_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN))
        lv_obj_clear_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int ms = (int)(tv.tv_usec / 1000);
    if (ms < 0) ms = 0;
    if (ms > 999) ms = 999;
    char buf[8];
    snprintf(buf, sizeof(buf), ".%03d", ms);
    lv_label_set_text(g_clock_ms_label, buf);
}

void clock_update_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_clock_time_label || !g_clock_date_label) return;
    /* If the user hid the clock entirely, hide the time label and
       let the sun map fill the tile. The date and tz labels stay
       (they're separate widgets). */
    if (!g_cfg.show_clock) {
        if (!lv_obj_has_flag(g_clock_time_label, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(g_clock_time_label, LV_OBJ_FLAG_HIDDEN);
    } else if (lv_obj_has_flag(g_clock_time_label, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(g_clock_time_label, LV_OBJ_FLAG_HIDDEN);
    }
    /* If a custom text is set, show that instead of the time digits.
       The date label still updates so the user keeps the calendar. */
    if (g_cfg.clock_text[0]) {
        lv_label_set_text(g_clock_time_label, g_cfg.clock_text);
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
    lv_label_set_text(g_clock_date_label, buf);
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
        lv_label_set_text(g_clock_time_label, buf);
    }
}

/* Forward decl: sunmap functions used by clock tile. */
static void sunmap_update_cb(lv_timer_t *t);

static const lv_font_t *clock_size_to_font(uint8_t size)
{
    /* All four tiers use jbmono so digits + custom Latin text share
       the same bold mono look. Sizes were generated with the full
       ASCII printable set so user text renders correctly. */
    switch (size) {
        case 0: return &font_jbmono_24;   /* XS */
        case 1: return &font_jbmono_48;   /* S  */
        case 2: return &font_jbmono_64;   /* M  */
        default:return &font_jbmono_96;   /* L  */
    }
}

/* Re-apply size, color, and position from g_cfg to the time + ms
   labels. Called after build_clock_tile and whenever the user
   updates one of the clock_* fields via the webui. The centering
   rule: when ms is hidden, center the time at (clock_x, clock_y);
   when shown, push the time left of (clock_x, clock_y) to leave
   room for the .mmm field on the right. */
void clock_apply_layout(void)
{
    if (!g_clock_time_label || !g_clock_ms_label) return;
    /* show_clock=0 hides the whole face; the sun map keeps drawing. */
    if (!g_cfg.show_clock) {
        lv_obj_add_flag(g_clock_time_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_clock_ms_label,   LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(g_clock_time_label, LV_OBJ_FLAG_HIDDEN);
    const lv_font_t *time_font = clock_size_to_font(g_cfg.clock_size);
    /* ms font is one tier smaller than the time, clamped at 12. */
    const lv_font_t *ms_font = clock_size_to_font(
        g_cfg.clock_size > 0 ? (uint8_t)(g_cfg.clock_size - 1) : 0);
    if (g_cfg.clock_size == 0) ms_font = &lv_font_montserrat_12;

    /* Color: 0xRRGGBBAA. lv_color_make is RGB888 -> native; LVGL 8 has
       no per-style alpha for text, so we apply the value as opacity. */
    uint32_t v = g_cfg.clock_rgba;
    uint8_t  rr = (uint8_t)(v >> 24);
    uint8_t  gg = (uint8_t)(v >> 16);
    uint8_t  bb = (uint8_t)(v >> 8);
    uint8_t  aa = (uint8_t)(v);
    lv_color_t col = lv_color_make(rr, gg, bb);
    lv_obj_set_style_text_font(g_clock_time_label, time_font, 0);
    lv_obj_set_style_text_color(g_clock_time_label, col, 0);
    lv_obj_set_style_text_opa(g_clock_time_label, aa ? aa : 0xFF, 0);

    /* The ms label is dimmer than the time (matches the original
       0xc0 vs 0xff feel); just apply the same opa. */
    lv_color_t ms_col = lv_color_make((uint8_t)(rr * 3 / 4),
                                      (uint8_t)(gg * 3 / 4),
                                      (uint8_t)(bb * 3 / 4));
    lv_obj_set_style_text_font(g_clock_ms_label, ms_font, 0);
    lv_obj_set_style_text_color(g_clock_ms_label, ms_col, 0);
    lv_obj_set_style_text_opa(g_clock_ms_label, aa ? aa : 0xFF, 0);

    /* Position. ms field gets a slot to the right of the time label
       only when (a) we're showing the time digits (no custom text)
       and (b) show_ms is on. Otherwise the time label centers on
       (clock_x, clock_y) by itself. */
    bool showing_ms = g_cfg.show_ms && !g_cfg.clock_text[0];
    lv_obj_align(g_clock_time_label, LV_ALIGN_CENTER,
                 g_cfg.clock_x, g_cfg.clock_y);
    if (showing_ms) {
        lv_obj_align_to(g_clock_ms_label, g_clock_time_label,
                        LV_ALIGN_OUT_RIGHT_BOTTOM, 0,
                        g_cfg.clock_size >= 3 ? -8 : -2);
        lv_obj_clear_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void build_clock_tile(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* Daylight map: stretch to fill the full screen as the clock
       background. Aspect may not match 2:1 (real Earth) -- we accept
       the squash because the map's job here is mostly atmospheric. */
    int W = canvas_w;
    int H = canvas_h;
    g_sunmap_w = W;
    g_sunmap_h = H;
    if (g_sunmap_buf) { lv_mem_free(g_sunmap_buf); g_sunmap_buf = NULL; }
    g_sunmap_buf = (lv_color_t *)lv_mem_alloc((size_t)W * H * sizeof(lv_color_t));
    if (g_sunmap_buf) {
        g_sunmap_canvas = lv_canvas_create(parent);
        lv_canvas_set_buffer(g_sunmap_canvas, g_sunmap_buf, W, H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(g_sunmap_canvas, LV_ALIGN_CENTER, 0, 0);
        clock_bg_apply();
        if (!g_sunmap_timer) {
            g_sunmap_timer = lv_timer_create(sunmap_update_cb, SUNMAP_RECOMPUTE_MS, NULL);
        }
    }

    /* Date at the top center, transparent bar over the map. */
    g_clock_date_label = lv_label_create(parent);
    lv_label_set_text(g_clock_date_label, "----.--.--");
    lv_obj_set_style_text_color(g_clock_date_label, lv_color_make(0xd0, 0xd0, 0xd0), 0);
    lv_obj_set_style_text_font(g_clock_date_label, i18n_font(), 0);
    lv_obj_set_style_bg_color(g_clock_date_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_date_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(g_clock_date_label, 6, 0);
    lv_obj_set_style_radius(g_clock_date_label, 3, 0);
    lv_obj_align(g_clock_date_label, LV_ALIGN_TOP_MID, 0, 4);

    /* Time + ms labels. Style + position is applied by clock_apply_layout
       which reads g_cfg.clock_{size,x,y,rgba} and the show_ms flag. */
    g_clock_time_label = lv_label_create(parent);
    lv_label_set_text(g_clock_time_label, "--:--:--");
    lv_obj_set_style_bg_opa(g_clock_time_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_clock_time_label, 0, 0);

    g_clock_ms_label = lv_label_create(parent);
    lv_label_set_text(g_clock_ms_label, ".000");
    lv_obj_set_style_bg_opa(g_clock_ms_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_clock_ms_label, 0, 0);

    clock_apply_layout();

    /* Timezone hint, bottom right -- shows the active city name. */
    g_clock_tz_label = lv_label_create(parent);
    lv_label_set_text(g_clock_tz_label, tz_current_city_name());
    lv_obj_set_style_text_color(g_clock_tz_label, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(g_clock_tz_label, i18n_font(), 0);
    lv_obj_set_style_bg_color(g_clock_tz_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_tz_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(g_clock_tz_label, 4, 0);
    lv_obj_align(g_clock_tz_label, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

    /* Wi-Fi + BT icons, top right (left of FPS pill which sits at -4,4). */
    g_clock_wifi_icon = lv_label_create(parent);
    lv_label_set_text(g_clock_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(g_clock_wifi_icon,
                                 lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_text_font(g_clock_wifi_icon, i18n_font(), 0);
    lv_obj_set_style_bg_color(g_clock_wifi_icon, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_wifi_icon, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(g_clock_wifi_icon, 3, 0);
    lv_obj_align(g_clock_wifi_icon, LV_ALIGN_TOP_RIGHT, -4, 4);

    g_clock_bt_icon = lv_label_create(parent);
    lv_label_set_text(g_clock_bt_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(g_clock_bt_icon,
                                 lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_text_font(g_clock_bt_icon, i18n_font(), 0);
    lv_obj_set_style_bg_color(g_clock_bt_icon, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_bt_icon, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(g_clock_bt_icon, 3, 0);
    lv_obj_align(g_clock_bt_icon, LV_ALIGN_TOP_RIGHT, -28, 4);

    if (!g_clock_timer) {
        g_clock_timer = lv_timer_create(clock_update_cb, 500, NULL);
    }
    if (!g_clock_ms_timer) {
        /* 16 ms tick = ~60 Hz, drives the visible refresh rate. */
        g_clock_ms_timer = lv_timer_create(clock_ms_update_cb, 16, NULL);
    }
    clock_update_cb(NULL);
    clock_ms_update_cb(NULL);
}

/* ---------------------- Sunmap tile ---------------------- */

/* Continents are sourced from main/landmask.h, a 1-bit equirectangular
   raster (640x172, stretched to fill the canvas) generated offline from
   Natural Earth 1:110m land polygons. See scripts/gen_landmask.py. */

void sunmap_redraw(void)
{
    if (!g_sunmap_buf || !g_sunmap_canvas) return;
    const int W = g_sunmap_w;
    const int H = g_sunmap_h;
    /* Four-tone palette: ocean/land x night/day. The mask is sized for
       the canvas (LANDMASK_W x LANDMASK_H = 640 x 172) and indexed 1:1. */
    theme_palette_t pal = theme_get();
    const lv_color_t c_water_n = pal.sunmap_water_n;
    const lv_color_t c_water_d = pal.sunmap_water_d;
    const lv_color_t c_land_n  = pal.sunmap_land_n;
    const lv_color_t c_land_d  = pal.sunmap_land_d;

    /* Subsolar point. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm tm;
    gmtime_r(&now, &tm);
    int doy = tm.tm_yday;  /* 0..365 */
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

    lv_obj_invalidate(g_sunmap_canvas);
}

static void sunmap_update_cb(lv_timer_t *t)
{
    (void)t;
    /* Only redraw the daylight map when the user actually wants it.
       Custom image / URL modes paint once on apply and don't need a
       periodic recompute. */
    if (g_cfg.bg_mode == 0) sunmap_redraw();
}

/* Load a raw RGB565 image of size canvas_w*canvas_h*2 from the SD
   card into the sunmap canvas buffer. The framebuffer uses the panel
   byte order (LV_COLOR_16_SWAP) -- the caller must save the file in
   that same byte order. Returns 0 on success. */
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
    if (g_sunmap_canvas) lv_obj_invalidate(g_sunmap_canvas);
    return 0;
}

/* Apply the background mode: paint the canvas from whichever source
   the user picked. Caller must hold the lvgl mutex. */
void clock_bg_apply(void)
{
    if (!g_sunmap_buf) return;
    switch (g_cfg.bg_mode) {
        case 1:
            if (clock_bg_load_raw(CLOCK_BG_PATH) != 0) {
                /* Fall back to sun map if the file is missing/short. */
                sunmap_redraw();
            }
            break;
        case 2:
            /* URL mode paints whatever the fetcher last wrote. */
            if (clock_bg_load_raw(CLOCK_BG_PATH) != 0) {
                sunmap_redraw();
            }
            break;
        case 3: {
            /* Solid color fill. Pack RGBA888 -> RGB565 in panel byte
               order (LV_COLOR_16_SWAP), then memset-style fill the
               canvas buffer with that 16-bit value. */
            uint8_t r = (uint8_t)(g_cfg.bg_color >> 24);
            uint8_t gn= (uint8_t)(g_cfg.bg_color >> 16);
            uint8_t b = (uint8_t)(g_cfg.bg_color >> 8);
            uint16_t rgb565 = (uint16_t)(((r & 0xF8) << 8) |
                                          ((gn & 0xFC) << 3) |
                                          ((b & 0xF8) >> 3));
            /* Swap bytes for the panel order. */
            uint16_t v = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
            uint16_t *p = (uint16_t *)g_sunmap_buf;
            int n = g_sunmap_w * g_sunmap_h;
            for (int i = 0; i < n; i++) p[i] = v;
            if (g_sunmap_canvas) lv_obj_invalidate(g_sunmap_canvas);
            break;
        }
        default:
            sunmap_redraw();
            break;
    }
}
