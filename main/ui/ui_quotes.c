#include "ui_quotes.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "radio.h"
#include "i18n.h"
#include "user_config.h"
#include "event_bus.h"

static const char *TAG = "ui_quotes";

/* Custom JetBrains Mono Bold font, generated via lv_font_conv. */
extern const lv_font_t font_jbmono_48;

/* ---------------------- Quotes tile ----------------------
   Two-pane price ticker. Left and right symbols are fetched as JSON
   from https://pchat.photonicat.com/q/<symbol>, e.g. /q/xauusd. A
   background task polls every quotes_refresh_s and updates the LVGL
   labels under the LVGL mutex. Color flips between up/down per the
   user-configurable RGBA values. */

struct quote_data {
    bool     ok;             /* last fetch succeeded */
    char     name[48];       /* "伦敦金 (现货黄金)" etc., or symbol fallback */
    char     symbol[16];     /* input symbol echoed back */
    double   price;
    double   change;
    double   change_pct;
    char     currency[8];
};

/* ===== 1. 对象定义 ===== */
lv_obj_t *ui_Quotes                = NULL;
lv_obj_t *ui_Quotes_label_name_l    = NULL;
lv_obj_t *ui_Quotes_label_name_r    = NULL;
lv_obj_t *ui_Quotes_label_sym_l     = NULL;
lv_obj_t *ui_Quotes_label_sym_r     = NULL;
lv_obj_t *ui_Quotes_label_price_l   = NULL;
lv_obj_t *ui_Quotes_label_price_r   = NULL;
lv_obj_t *ui_Quotes_label_chg_l     = NULL;
lv_obj_t *ui_Quotes_label_chg_r     = NULL;
lv_obj_t *ui_Quotes_label_status    = NULL;
lv_obj_t *ui_Quotes_icon_wifi       = NULL;
lv_obj_t *ui_Quotes_icon_bt         = NULL;
lv_obj_t *ui_Quotes_label_clock     = NULL;

/* ===== 2. 静态样式变量 ===== */
static lv_style_t style_tile_bg;
static lv_style_t style_pane;
static lv_style_t style_name;
static lv_style_t style_sym;
static lv_style_t style_price;
static lv_style_t style_chg;
static lv_style_t style_status;
static lv_style_t style_icon;
static lv_style_t style_clock_lbl;
static lv_style_t style_divider;
static bool styles_inited = false;

/* 内部状态变量 */
static TaskHandle_t  s_quotes_task    = NULL;
static volatile bool s_quotes_kick    = false;
static struct quote_data s_qd_l = {0};
static struct quote_data s_qd_r = {0};
static volatile bool s_qd_have_new    = false;
static lv_timer_t   *s_status_timer   = NULL;

static void init_styles(void)
{
    if (styles_inited) return;

    lv_style_init(&style_tile_bg);
    lv_style_set_bg_color(&style_tile_bg, lv_color_black());
    lv_style_set_bg_opa(&style_tile_bg, LV_OPA_COVER);
    lv_style_set_pad_all(&style_tile_bg, 0);

    lv_style_init(&style_pane);
    lv_style_set_bg_color(&style_pane, lv_color_black());
    lv_style_set_bg_opa(&style_pane, LV_OPA_COVER);
    lv_style_set_pad_all(&style_pane, 8);

    lv_style_init(&style_name);
    lv_style_set_text_color(&style_name, lv_color_make(0xc8, 0xc8, 0xe0));

    lv_style_init(&style_sym);
    lv_style_set_text_color(&style_sym, lv_color_make(0x80, 0x80, 0xa0));

    lv_style_init(&style_price);
    lv_style_set_text_color(&style_price, lv_color_make(0x60, 0x60, 0x60));
    lv_style_set_text_font(&style_price, &font_jbmono_48);

    lv_style_init(&style_chg);
    lv_style_set_text_color(&style_chg, lv_color_make(0x80, 0x80, 0x80));

    lv_style_init(&style_status);
    lv_style_set_text_color(&style_status, lv_color_make(0x60, 0x60, 0x80));

    lv_style_init(&style_icon);
    lv_style_set_bg_color(&style_icon, lv_color_black());
    lv_style_set_bg_opa(&style_icon, LV_OPA_60);
    lv_style_set_pad_hor(&style_icon, 3);

    lv_style_init(&style_clock_lbl);
    lv_style_set_text_color(&style_clock_lbl, lv_color_make(0xd0, 0xd0, 0xd0));
    lv_style_set_bg_color(&style_clock_lbl, lv_color_black());
    lv_style_set_bg_opa(&style_clock_lbl, LV_OPA_60);
    lv_style_set_pad_hor(&style_clock_lbl, 3);

    lv_style_init(&style_divider);
    lv_style_set_bg_color(&style_divider, lv_color_make(0x30, 0x30, 0x40));
    lv_style_set_bg_opa(&style_divider, LV_OPA_COVER);

    styles_inited = true;
}

/* ===== 业务辅助函数（数据相关） ===== */

static lv_color_t quotes_rgba_to_color(uint32_t rgba)
{
    return lv_color_make((uint8_t)(rgba >> 24),
                         (uint8_t)(rgba >> 16),
                         (uint8_t)(rgba >> 8));
}

/* Tiny JSON value extractor for our fixed response shape. Finds
   "key":<value> and copies the value text up to the next , or }. Strips
   surrounding quotes. Returns 0/1 for success. */
static int json_extract(const char *src, const char *key,
                        char *out, size_t out_sz)
{
    if (!src || !key || !out || out_sz == 0) return 0;
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(src, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    bool quoted = (*p == '"');
    if (quoted) p++;
    size_t o = 0;
    while (*p && o < out_sz - 1) {
        char c = *p++;
        if (quoted) {
            if (c == '"') break;
        } else {
            if (c == ',' || c == '}' || c == ' ' || c == '\n' || c == '\r') break;
        }
        out[o++] = c;
    }
    out[o] = 0;
    return o > 0;
}

static int quotes_parse_json(const char *json, struct quote_data *q)
{
    if (!json || !q) return -1;
    char buf[64];
    if (!json_extract(json, "price", buf, sizeof(buf))) return -1;
    q->price = strtod(buf, NULL);
    if (json_extract(json, "change", buf, sizeof(buf)))      q->change     = strtod(buf, NULL);
    if (json_extract(json, "change_pct", buf, sizeof(buf)))  q->change_pct = strtod(buf, NULL);
    if (json_extract(json, "currency", buf, sizeof(buf))) {
        strncpy(q->currency, buf, sizeof(q->currency) - 1);
        q->currency[sizeof(q->currency) - 1] = 0;
    }
    if (json_extract(json, "name", buf, sizeof(buf))) {
        strncpy(q->name, buf, sizeof(q->name) - 1);
        q->name[sizeof(q->name) - 1] = 0;
    }
    if (json_extract(json, "input_symbol", buf, sizeof(buf))) {
        strncpy(q->symbol, buf, sizeof(q->symbol) - 1);
        q->symbol[sizeof(q->symbol) - 1] = 0;
    }
    q->ok = true;
    return 0;
}

/* Fetch one symbol synchronously. */
static int quotes_fetch_one(const char *sym, struct quote_data *out)
{
    if (!sym || !sym[0]) return -1;
    char url[160];
    snprintf(url, sizeof(url), "https://pchat.photonicat.com/q/%s", sym);
    esp_http_client_config_t cfg = {0};
    cfg.url = url;
    cfg.timeout_ms = 8000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    esp_err_t e = esp_http_client_open(c, 0);
    if (e != ESP_OK) { esp_http_client_cleanup(c); return -1; }
    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    if (status != 200) {
        ESP_LOGW(TAG, "quotes %s -> HTTP %d", sym, status);
        esp_http_client_close(c); esp_http_client_cleanup(c);
        return -1;
    }
    char body[768];
    int got = 0;
    while (got < (int)sizeof(body) - 1) {
        int n = esp_http_client_read(c, body + got, sizeof(body) - 1 - got);
        if (n <= 0) break;
        got += n;
    }
    body[got] = 0;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (got <= 0) return -1;
    memset(out, 0, sizeof(*out));
    /* keep the user's symbol if the server doesn't echo input_symbol */
    strncpy(out->symbol, sym, sizeof(out->symbol) - 1);
    return quotes_parse_json(body, out);
}

static void quotes_repaint_one(struct quote_data *q,
                               lv_obj_t *name_lbl, lv_obj_t *sym_lbl,
                               lv_obj_t *price_lbl, lv_obj_t *chg_lbl)
{
    if (!name_lbl || !price_lbl || !chg_lbl) return;
    if (!q->ok) {
        lv_label_set_text(name_lbl, "");
        if (sym_lbl) lv_label_set_text(sym_lbl, q->symbol[0] ? q->symbol : "-");
        /* Keep glyphs ASCII-only -- jbmono_48 doesn't carry em-dashes
           and would render unknown-glyph boxes. */
        lv_label_set_text(price_lbl, "---.--");
        lv_obj_set_style_text_color(price_lbl, lv_color_make(0x60, 0x60, 0x60), 0);
        lv_label_set_text(chg_lbl, "no data");
        lv_obj_set_style_text_color(chg_lbl, lv_color_make(0x80, 0x80, 0x80), 0);
        return;
    }
    /* Headline: uppercased symbol + currency (ASCII, always renders).
       The upstream "name" field is often a CJK string (伦敦金 etc.) and
       this build's CJK font subset doesn't cover all those code points,
       so it tofus -- use a friendly ASCII name table for common metals
       and fall back to the raw symbol otherwise. */
    char usym[20] = {0};
    const char *s = q->symbol[0] ? q->symbol : "";
    for (int i = 0; s[i] && i < (int)sizeof(usym) - 1; i++) {
        usym[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    }
    const char *pretty = NULL;
    if      (!strcmp(usym, "XAUUSD")) pretty = "GOLD";
    else if (!strcmp(usym, "XAGUSD")) pretty = "SILVER";
    else if (!strcmp(usym, "XPTUSD")) pretty = "PLATINUM";
    else if (!strcmp(usym, "XPDUSD")) pretty = "PALLADIUM";
    else if (!strcmp(usym, "BTCUSD")) pretty = "BITCOIN";
    else if (!strcmp(usym, "ETHUSD")) pretty = "ETHEREUM";
    lv_label_set_text(name_lbl, pretty ? pretty : usym);
    if (sym_lbl) {
        char sb[24];
        snprintf(sb, sizeof(sb), "%s%s%s", usym,
                 q->currency[0] ? "  " : "",
                 q->currency[0] ? q->currency : "");
        lv_label_set_text(sym_lbl, sb);
    }
    /* Price: choose decimals based on magnitude so XAU/XAG/FX all look
       sensible. LVGL's bundled vsnprintf in this build is compiled
       without %f support (LV_SPRINTF_USE_FLOAT=n), so format floats
       with stdio first and feed the resulting string into LVGL. */
    int dec = (q->price >= 1000) ? 2 : (q->price >= 100 ? 2 : 4);
    char pbuf[32];
    snprintf(pbuf, sizeof(pbuf), "%.*f", dec, q->price);
    lv_label_set_text(price_lbl, pbuf);
    bool up = q->change >= 0.0;
    uint32_t rgba = up ? g_cfg.quotes_up_rgba : g_cfg.quotes_down_rgba;
    lv_color_t col = quotes_rgba_to_color(rgba);
    lv_obj_set_style_text_color(price_lbl, col, 0);
    lv_obj_set_style_text_color(chg_lbl,   col, 0);
    char cbuf[48];
    snprintf(cbuf, sizeof(cbuf), "%s%.2f  (%s%.3f%%)",
             up ? "+" : "", q->change,
             up ? "+" : "", q->change_pct);
    lv_label_set_text(chg_lbl, cbuf);
}

/* Runs on the LVGL task via lv_timer, picks up data the fetcher task
   stashed into s_qd_*. */
static void quotes_apply_pending(void)
{
    if (!s_qd_have_new) return;
    /* Bail if the tile labels haven't been built yet, or were torn
       down by a rotation rebuild (the fetcher task is independent of
       the LVGL UI lifecycle). */
    if (!ui_Quotes_label_name_l || !ui_Quotes_label_price_l || !ui_Quotes_label_chg_l ||
        !ui_Quotes_label_name_r || !ui_Quotes_label_price_r || !ui_Quotes_label_chg_r) return;
    s_qd_have_new = false;
    quotes_repaint_one(&s_qd_l, ui_Quotes_label_name_l, ui_Quotes_label_sym_l,
                       ui_Quotes_label_price_l, ui_Quotes_label_chg_l);
    quotes_repaint_one(&s_qd_r, ui_Quotes_label_name_r, ui_Quotes_label_sym_r,
                       ui_Quotes_label_price_r, ui_Quotes_label_chg_r);
    if (ui_Quotes_label_status) {
        time_t now = time(NULL);
        struct tm tm_local;
        localtime_r(&now, &tm_local);
        char tbuf[16];
        strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm_local);
        lv_label_set_text_fmt(ui_Quotes_label_status, "updated %s", tbuf);
    }
}

/* ===== 业务辅助函数（fetcher task） ===== */

void quotes_kick(void)
{
    s_quotes_kick = true;
}

static void quotes_task(void *arg)
{
    (void)arg;
    /* Hold off the first fetch until Wi-Fi is up and the radio engine
       had time to claim I2S / codec resources. The HTTPS handshake's
       internal-RAM allocations are big enough to starve the radio
       engine's I2S DMA descriptors if we race it on a fragmented heap.
       Waiting up to ~12s for the IP costs nothing because nothing else
       is consuming this task. */
    for (int i = 0; i < 120; i++) {
        if (wifi_is_connected()) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* Plus a small extra so radio_engine_warm_at_boot has time to finish
       its codec init on the other core. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    while (1) {
        struct quote_data ql = {0}, qr = {0};
        if (g_cfg.quotes_sym_l[0]) {
            if (quotes_fetch_one(g_cfg.quotes_sym_l, &ql) == 0) s_qd_l = ql;
            else { s_qd_l.ok = false;
                   size_t len = strlen(g_cfg.quotes_sym_l);
                   if (len >= sizeof(s_qd_l.symbol)) len = sizeof(s_qd_l.symbol) - 1;
                   memcpy(s_qd_l.symbol, g_cfg.quotes_sym_l, len);
                   s_qd_l.symbol[len] = '\0'; }
        }
        if (g_cfg.quotes_sym_r[0]) {
            if (quotes_fetch_one(g_cfg.quotes_sym_r, &qr) == 0) s_qd_r = qr;
            else { s_qd_r.ok = false;
                   size_t len = strlen(g_cfg.quotes_sym_r);
                   if (len >= sizeof(s_qd_r.symbol)) len = sizeof(s_qd_r.symbol) - 1;
                   memcpy(s_qd_r.symbol, g_cfg.quotes_sym_r, len);
                   s_qd_r.symbol[len] = '\0'; }
        }
        s_qd_have_new = true;
        ESP_LOGI(TAG, "quotes: %s=%.4f %+.3f%%  %s=%.4f %+.3f%%",
                 s_qd_l.symbol, s_qd_l.price, s_qd_l.change_pct,
                 s_qd_r.symbol, s_qd_r.price, s_qd_r.change_pct);

        int wait_s = g_cfg.quotes_refresh_s > 0 ? g_cfg.quotes_refresh_s : 60;
        for (int i = 0; i < wait_s * 10; i++) {
            if (s_quotes_kick) { s_quotes_kick = false; break; }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void quotes_ensure(void)
{
    if (s_quotes_task) { s_quotes_kick = true; return; }
    xTaskCreatePinnedToCoreWithCaps(
        quotes_task, "quotes", 6 * 1024, NULL, 4, &s_quotes_task, 0,
        MALLOC_CAP_SPIRAM);
}

/* ===== 定时器回调 ===== */

static void quotes_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    /* Drain pending fetch results into the LVGL labels (we're on the
       LVGL task here, so direct label updates are safe). */
    quotes_apply_pending();
    /* Repaint the floating clock label, top-right. */
    if (ui_Quotes_label_clock) {
        time_t now = time(NULL);
        struct tm tm_local;
        localtime_r(&now, &tm_local);
        char buf[16];
        strftime(buf, sizeof(buf), g_cfg.hour24 ? "%H:%M" : "%I:%M %p", &tm_local);
        lv_label_set_text(ui_Quotes_label_clock, buf);
    }
    /* Wi-Fi/BT colors mirror what the clock tile shows via
       status_timer_cb (which reads the same globals). */
    if (ui_Quotes_icon_wifi) {
        char ssid_buf[33];
        wifi_get_curr_ssid(ssid_buf, sizeof(ssid_buf));
        lv_color_t c = wifi_is_connected()
            ? lv_color_make(0x80, 0xff, 0x80)
            : (ssid_buf[0]
                ? lv_color_make(0xff, 0xa0, 0x40)
                : lv_color_make(0x40, 0x40, 0x40));
        lv_obj_set_style_text_color(ui_Quotes_icon_wifi, c, 0);
    }
    if (ui_Quotes_icon_bt) {
        lv_obj_set_style_text_color(ui_Quotes_icon_bt,
            lv_color_make(0x40, 0x40, 0x40), 0);
    }
}

/* ===== 事件总线 handler ===== */

static void on_quotes_changed_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    /* 行情配置变更（符号/颜色/刷新间隔），立即触发一次重新拉取 */
    quotes_kick();
}

/* ===== 公共 UI API ===== */

void ui_Quotes_cleanup(void)
{
    /* 取消事件订阅 */
    event_bus_unsubscribe(EVENT_QUOTES_CHANGED, on_quotes_changed_evt);

    /* 删除定时器（原代码漏存句柄导致泄漏，此处修复） */
    if (s_status_timer) { lv_timer_del(s_status_timer); s_status_timer = NULL; }

    ui_Quotes                = NULL;
    ui_Quotes_label_name_l    = NULL;
    ui_Quotes_label_name_r    = NULL;
    ui_Quotes_label_sym_l     = NULL;
    ui_Quotes_label_sym_r     = NULL;
    ui_Quotes_label_price_l   = NULL;
    ui_Quotes_label_price_r   = NULL;
    ui_Quotes_label_chg_l     = NULL;
    ui_Quotes_label_chg_r     = NULL;
    ui_Quotes_label_status    = NULL;
    ui_Quotes_icon_wifi       = NULL;
    ui_Quotes_icon_bt         = NULL;
    ui_Quotes_label_clock     = NULL;
}

/* ===== 4. tile 创建函数 ===== */

/* Build one pane on the quotes tile. */
static void build_quotes_pane(lv_obj_t *parent, int x, int w,
                              lv_obj_t **name_out, lv_obj_t **sym_out,
                              lv_obj_t **price_out, lv_obj_t **chg_out)
{
    lv_obj_t *pane = lv_obj_create(parent);
    lv_obj_remove_style_all(pane);
    lv_obj_set_size(pane, w, disp_driver_get_canvas_h());
    lv_obj_set_pos(pane, x, 0);
    lv_obj_add_style(pane, &style_pane, 0);
    lv_obj_clear_flag(pane, LV_OBJ_FLAG_SCROLLABLE);

    /* Pretty name. Pushed right on the left pane so the global FPS pill
       (anchored top-left of the screen, ~64 px wide with padding)
       doesn't overlap. */
    bool is_left_pane = (x == 0);
    int name_dx = is_left_pane ? 76 : 0;
    lv_obj_t *name = lv_label_create(pane);
    lv_obj_set_width(name, w - 20 - name_dx);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, "");
    lv_obj_add_style(name, &style_name, 0);
    lv_obj_set_style_text_font(name, i18n_font(), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, name_dx, 0);

    /* Symbol + currency, slightly dimmer, beneath the name. */
    lv_obj_t *sym = lv_label_create(pane);
    lv_label_set_text(sym, "");
    lv_obj_add_style(sym, &style_sym, 0);
    lv_obj_set_style_text_font(sym, i18n_font(), 0);
    lv_obj_align(sym, LV_ALIGN_TOP_LEFT, name_dx, 16);

    /* Price -- big jbmono. Use 48px so two panes fit side by side at
       640px wide (each pane ~316px). Stick to ASCII chars only: this
       font ships with the digits + . + - and not much else. */
    lv_obj_t *price = lv_label_create(pane);
    lv_label_set_text(price, "---.--");
    lv_obj_add_style(price, &style_price, 0);
    lv_obj_align(price, LV_ALIGN_LEFT_MID, 0, 8);

    /* Change line beneath the price, colored. */
    lv_obj_t *chg = lv_label_create(pane);
    lv_label_set_text(chg, "");
    lv_obj_add_style(chg, &style_chg, 0);
    lv_obj_set_style_text_font(chg, i18n_font(), 0);
    lv_obj_align(chg, LV_ALIGN_BOTTOM_LEFT, 0, -16);

    *name_out  = name;
    *sym_out   = sym;
    *price_out = price;
    *chg_out   = chg;
}

void ui_Quotes_create(lv_obj_t *parent)
{
    init_styles();

    /* tile 容器 */
    ui_Quotes = parent;
    lv_obj_clear_flag(ui_Quotes, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(ui_Quotes, &style_tile_bg, 0);

    int cw = disp_driver_get_canvas_w();
    int half = cw / 2;
    build_quotes_pane(ui_Quotes, 0, half,
                      &ui_Quotes_label_name_l, &ui_Quotes_label_sym_l,
                      &ui_Quotes_label_price_l, &ui_Quotes_label_chg_l);
    build_quotes_pane(ui_Quotes, half, cw - half,
                      &ui_Quotes_label_name_r, &ui_Quotes_label_sym_r,
                      &ui_Quotes_label_price_r, &ui_Quotes_label_chg_r);

    /* Thin vertical divider. */
    lv_obj_t *div = lv_obj_create(ui_Quotes);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, 1, disp_driver_get_canvas_h() - 20);
    lv_obj_set_pos(div, half, 10);
    lv_obj_add_style(div, &style_divider, 0);

    /* Status line at the very bottom, centered. */
    ui_Quotes_label_status = lv_label_create(ui_Quotes);
    lv_label_set_text(ui_Quotes_label_status, "fetching…");
    lv_obj_add_style(ui_Quotes_label_status, &style_status, 0);
    lv_obj_set_style_text_font(ui_Quotes_label_status, i18n_font(), 0);
    lv_obj_align(ui_Quotes_label_status, LV_ALIGN_BOTTOM_MID, 0, -2);

    /* Floating status overlay (top-right): wifi, bt, hh:mm. */
    ui_Quotes_icon_wifi = lv_label_create(ui_Quotes);
    lv_label_set_text(ui_Quotes_icon_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(ui_Quotes_icon_wifi,
                                 lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_add_style(ui_Quotes_icon_wifi, &style_icon, 0);
    lv_obj_set_style_text_font(ui_Quotes_icon_wifi, i18n_font(), 0);
    lv_obj_align(ui_Quotes_icon_wifi, LV_ALIGN_TOP_RIGHT, -4, 4);

    ui_Quotes_icon_bt = lv_label_create(ui_Quotes);
    lv_label_set_text(ui_Quotes_icon_bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(ui_Quotes_icon_bt,
                                 lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_add_style(ui_Quotes_icon_bt, &style_icon, 0);
    lv_obj_set_style_text_font(ui_Quotes_icon_bt, i18n_font(), 0);
    lv_obj_align(ui_Quotes_icon_bt, LV_ALIGN_TOP_RIGHT, -28, 4);

    ui_Quotes_label_clock = lv_label_create(ui_Quotes);
    lv_label_set_text(ui_Quotes_label_clock, "--:--");
    lv_obj_add_style(ui_Quotes_label_clock, &style_clock_lbl, 0);
    lv_obj_set_style_text_font(ui_Quotes_label_clock, i18n_font(), 0);
    lv_obj_align(ui_Quotes_label_clock, LV_ALIGN_TOP_RIGHT, -52, 4);

    /* 500 ms repaint timer (drains pending fetch results, refreshes
       the clock and the wifi/bt color). 保存句柄以便 cleanup 删除。 */
    s_status_timer = lv_timer_create(quotes_status_timer_cb, 500, NULL);

    /* Spin up the fetcher. */
    quotes_ensure();

    /* 订阅事件总线：行情配置变更时触发重新拉取 */
    event_bus_subscribe(EVENT_QUOTES_CHANGED, on_quotes_changed_evt, NULL);
}

/* ===== 5. tile 清理函数 ===== */
/* (已在上方定义 ui_Quotes_cleanup) */
