#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "tz_cities.h"
#include "i18n.h"
#include "user_config.h"

#include "app_cfg.h"

#if __has_include("wifi_secret.h")
#  include "wifi_secret.h"
#endif

#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID  ""
#endif
#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS  ""
#endif

static const char *TAG = "app_cfg";

extern lv_obj_t *g_tileview;
extern const char *tz_current_city_name(void);
extern bool lvgl_lock(int ms);
extern void lvgl_unlock(void);
extern int g_canvas_w;
extern int g_canvas_h;

app_cfg_t g_cfg = {
    .version           = CFG_VERSION,
    .tz_idx            = TZ_DEFAULT_CITY_INDEX,
    .brightness        = 255,
    .dim_s             = 8 * 3600,
    .off_s             = 8 * 3600,
    .last_ssid         = {0},
    .hour24            = 1,
    .date_fmt          = 0,
    .show_seconds      = 1,
    .show_ms           = 1,
    .audio_enable      = 1,
    .audio_volume      = 70,
    .theme             = 0,
    .show_fps          = 1,
    .wifi_autoconnect  = 1,
    .lang              = 0,
    .clock_x           = -52,
    .clock_y           = 0,
    .clock_size        = 3,
    .clock_rgba        = 0xFFFFFFFF,
    .show_clock        = 1,
    .clock_text        = {0},
    .bg_mode           = 0,
    .bg_refresh_s      = 0,
    .bg_url            = {0},
    .bg_color          = 0x202020FFu,
    .quotes_sym_l      = "xauusd",
    .quotes_sym_r      = "xagusd",
    .quotes_refresh_s  = 60,
    .quotes_up_rgba    = 0x33DD66FFu,
    .quotes_down_rgba  = 0xFF4040FFu,
};

static struct {
    void (*on_clock_layout_changed)(void);
    void (*on_clock_bg_changed)(void);
    void (*on_quotes_changed)(void);
    void (*on_backlight_changed)(uint8_t brightness);
    void (*on_bg_fetch_ensure)(void);
    void (*on_wifi_connect)(const char *ssid, const char *pass);
} s_callbacks = {0};

void app_cfg_register_callbacks(const app_cfg_callbacks_t *cb)
{
    if (!cb) return;
    s_callbacks.on_clock_layout_changed = cb->on_clock_layout_changed;
    s_callbacks.on_clock_bg_changed = cb->on_clock_bg_changed;
    s_callbacks.on_quotes_changed = cb->on_quotes_changed;
    s_callbacks.on_backlight_changed = cb->on_backlight_changed;
    s_callbacks.on_bg_fetch_ensure = cb->on_bg_fetch_ensure;
    s_callbacks.on_wifi_connect = cb->on_wifi_connect;
}

static void cfg_validate(void)
{
    if (g_cfg.tz_idx >= TZ_CITY_COUNT) g_cfg.tz_idx = TZ_DEFAULT_CITY_INDEX;
    if (g_cfg.date_fmt > 2) g_cfg.date_fmt = 0;
    if (g_cfg.theme > 2) g_cfg.theme = 0;
    if (g_cfg.audio_volume > 100) g_cfg.audio_volume = 100;
    if (g_cfg.clock_size > 3) g_cfg.clock_size = 3;
    if (g_cfg.bg_mode > 3) g_cfg.bg_mode = 0;
    if ((g_cfg.clock_rgba & 0xFF) == 0) g_cfg.clock_rgba = 0xFFFFFFFFu;
    g_cfg.show_clock = g_cfg.show_clock ? 1 : 0;
    g_cfg.hour24 = g_cfg.hour24 ? 1 : 0;
    g_cfg.show_seconds = g_cfg.show_seconds ? 1 : 0;
    g_cfg.show_ms = g_cfg.show_ms ? 1 : 0;
    g_cfg.audio_enable = g_cfg.audio_enable ? 1 : 0;
    g_cfg.show_fps = g_cfg.show_fps ? 1 : 0;
    g_cfg.wifi_autoconnect = g_cfg.wifi_autoconnect ? 1 : 0;
}

static void cfg_migrate(uint8_t from_ver)
{
    if (from_ver < 7) {
        ESP_LOGI(TAG, "cfg: migrate v%u -> v%u", (unsigned)from_ver, (unsigned)CFG_VERSION);
    }
    g_cfg.version = CFG_VERSION;
}

static void cfg_read_nvs(nvs_handle_t h)
{
    nvs_get_u8 (h, "ver",       &g_cfg.version);
    nvs_get_u16(h, "tz_idx",    &g_cfg.tz_idx);
    nvs_get_u8 (h, "bri",       &g_cfg.brightness);
    nvs_get_u16(h, "dim_s",     &g_cfg.dim_s);
    nvs_get_u16(h, "off_s",     &g_cfg.off_s);
    nvs_get_u8 (h, "h24",       &g_cfg.hour24);
    nvs_get_u8 (h, "date_fmt",  &g_cfg.date_fmt);
    nvs_get_u8 (h, "show_sec",  &g_cfg.show_seconds);
    nvs_get_u8 (h, "show_ms",   &g_cfg.show_ms);
    nvs_get_u8 (h, "aud_en",    &g_cfg.audio_enable);
    nvs_get_u8 (h, "aud_vol",   &g_cfg.audio_volume);
    nvs_get_u8 (h, "theme",     &g_cfg.theme);
    nvs_get_u8 (h, "show_fps",  &g_cfg.show_fps);
    nvs_get_u8 (h, "wifi_ac",   &g_cfg.wifi_autoconnect);
    nvs_get_u8 (h, "lang",      &g_cfg.lang);
    nvs_get_i16(h, "clk_x",     &g_cfg.clock_x);
    nvs_get_i16(h, "clk_y",     &g_cfg.clock_y);
    nvs_get_u8 (h, "clk_sz",    &g_cfg.clock_size);
    nvs_get_u32(h, "clk_rgba",  &g_cfg.clock_rgba);
    nvs_get_u8 (h, "clk_show",  &g_cfg.show_clock);
    nvs_get_u8 (h, "bg_mode",   &g_cfg.bg_mode);
    nvs_get_u16(h, "bg_refr",   &g_cfg.bg_refresh_s);
    nvs_get_u32(h, "bg_color",  &g_cfg.bg_color);

    size_t sl = sizeof(g_cfg.last_ssid);
    nvs_get_str(h, "last_ssid", g_cfg.last_ssid, &sl);

    size_t ctl = sizeof(g_cfg.clock_text);
    nvs_get_str(h, "clk_text",  g_cfg.clock_text, &ctl);

    size_t bgul = sizeof(g_cfg.bg_url);
    nvs_get_str(h, "bg_url",    g_cfg.bg_url, &bgul);

    size_t qsll = sizeof(g_cfg.quotes_sym_l);
    size_t qsrl = sizeof(g_cfg.quotes_sym_r);
    nvs_get_str(h, "q_sl",      g_cfg.quotes_sym_l, &qsll);
    nvs_get_str(h, "q_sr",      g_cfg.quotes_sym_r, &qsrl);

    uint16_t qrs = g_cfg.quotes_refresh_s;
    nvs_get_u16(h, "q_refr",    &qrs);
    g_cfg.quotes_refresh_s = qrs;

    uint32_t qu = g_cfg.quotes_up_rgba;
    uint32_t qd = g_cfg.quotes_down_rgba;
    nvs_get_u32(h, "q_up",      &qu);
    nvs_get_u32(h, "q_dn",      &qd);
    g_cfg.quotes_up_rgba = qu;
    g_cfg.quotes_down_rgba = qd;
}

void app_cfg_init(void)
{
    esp_err_t er = nvs_flash_init();
    if (er == ESP_ERR_NVS_NO_FREE_PAGES || er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        er = nvs_flash_init();
    }
    ESP_ERROR_CHECK(er);

    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READONLY, &h) != ESP_OK) return;
    cfg_read_nvs(h);
    nvs_close(h);

    uint8_t loaded_ver = g_cfg.version;
    cfg_validate();

    if (loaded_ver < CFG_VERSION) {
        cfg_migrate(loaded_ver);
        if (DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0]) {
            strncpy(g_cfg.last_ssid, DEFAULT_WIFI_SSID, sizeof(g_cfg.last_ssid) - 1);
            app_cfg_save_ssid_pass(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
        }
        app_cfg_save();
    }

    const char *city_name = tz_current_city_name();
    const char *tz_posix = k_tz_cities[g_cfg.tz_idx].posix_tz;
    ESP_LOGI(TAG, "cfg: tz=%s (%s) bri=%u dim=%us off=%us last_ssid=%s",
             city_name ? city_name : "(unknown)",
             tz_posix ? tz_posix : "(unknown)",
             g_cfg.brightness, g_cfg.dim_s, g_cfg.off_s,
             g_cfg.last_ssid[0] ? g_cfg.last_ssid : "(none)");
}

void app_cfg_load(void)
{
    app_cfg_init();
}

void app_cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8 (h, "ver",       g_cfg.version);
    nvs_set_u16(h, "tz_idx",    g_cfg.tz_idx);
    nvs_set_u8 (h, "bri",       g_cfg.brightness);
    nvs_set_u8 (h, "h24",       g_cfg.hour24);
    nvs_set_u8 (h, "date_fmt",  g_cfg.date_fmt);
    nvs_set_u8 (h, "show_sec",  g_cfg.show_seconds);
    nvs_set_u8 (h, "show_ms",   g_cfg.show_ms);
    nvs_set_u8 (h, "aud_en",    g_cfg.audio_enable);
    nvs_set_u8 (h, "aud_vol",   g_cfg.audio_volume);
    nvs_set_u8 (h, "theme",     g_cfg.theme);
    nvs_set_u8 (h, "show_fps",  g_cfg.show_fps);
    nvs_set_u8 (h, "wifi_ac",   g_cfg.wifi_autoconnect);
    nvs_set_u8 (h, "lang",      g_cfg.lang);
    nvs_set_u16(h, "dim_s",     g_cfg.dim_s);
    nvs_set_u16(h, "off_s",     g_cfg.off_s);
    nvs_set_i16(h, "clk_x",     g_cfg.clock_x);
    nvs_set_i16(h, "clk_y",     g_cfg.clock_y);
    nvs_set_u8 (h, "clk_sz",    g_cfg.clock_size);
    nvs_set_u32(h, "clk_rgba",  g_cfg.clock_rgba);
    nvs_set_u8 (h, "clk_show",  g_cfg.show_clock);
    nvs_set_str(h, "clk_text",  g_cfg.clock_text);
    nvs_set_u8 (h, "bg_mode",   g_cfg.bg_mode);
    nvs_set_u16(h, "bg_refr",   g_cfg.bg_refresh_s);
    nvs_set_str(h, "bg_url",    g_cfg.bg_url);
    nvs_set_u32(h, "bg_color",  g_cfg.bg_color);
    nvs_set_str(h, "q_sl",      g_cfg.quotes_sym_l);
    nvs_set_str(h, "q_sr",      g_cfg.quotes_sym_r);
    nvs_set_u16(h, "q_refr",    g_cfg.quotes_refresh_s);
    nvs_set_u32(h, "q_up",      g_cfg.quotes_up_rgba);
    nvs_set_u32(h, "q_dn",      g_cfg.quotes_down_rgba);
    nvs_set_str(h, "last_ssid", g_cfg.last_ssid);
    nvs_commit(h);
    nvs_close(h);
}

void app_cfg_save_ssid_pass(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    char key[16] = {0};
    strncpy(key, ssid, 15);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
}

bool app_cfg_get_ssid_pass(const char *ssid, char *pass, size_t pass_len)
{
    if (!ssid || !*ssid || !pass) return false;
    char key[16] = {0};
    strncpy(key, ssid, 15);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = pass_len;
    esp_err_t er = nvs_get_str(h, key, pass, &l);
    nvs_close(h);
    return er == ESP_OK;
}

int app_cfg_get_lang(void) { return g_cfg.lang; }
int app_cfg_get_brightness(void) { return g_cfg.brightness; }
int app_cfg_get_dim_s(void) { return g_cfg.dim_s; }
int app_cfg_get_off_s(void) { return g_cfg.off_s; }

int app_cfg_get_clock_x(void) { return g_cfg.clock_x; }
int app_cfg_get_clock_y(void) { return g_cfg.clock_y; }
int app_cfg_get_clock_size(void) { return g_cfg.clock_size; }
uint32_t app_cfg_get_clock_rgba(void) { return g_cfg.clock_rgba; }
int app_cfg_get_show_ms(void) { return g_cfg.show_ms; }
int app_cfg_get_show_seconds(void) { return g_cfg.show_seconds; }

void app_cfg_set_show_seconds(int show)
{
    g_cfg.show_seconds = show ? 1 : 0;
    app_cfg_save();
}

int app_cfg_get_show_clock(void) { return g_cfg.show_clock; }

void app_cfg_set_show_clock(int show)
{
    g_cfg.show_clock = show ? 1 : 0;
    if (s_callbacks.on_clock_layout_changed) {
        s_callbacks.on_clock_layout_changed();
    }
    app_cfg_save();
}

const char *app_cfg_get_clock_text(void) { return g_cfg.clock_text; }

int app_cfg_get_bg_mode(void) { return g_cfg.bg_mode; }
int app_cfg_get_bg_refresh_s(void) { return g_cfg.bg_refresh_s; }
const char *app_cfg_get_bg_url(void) { return g_cfg.bg_url; }
int app_cfg_get_canvas_w(void) { return g_canvas_w; }
int app_cfg_get_canvas_h(void) { return g_canvas_h; }

void app_cfg_set_bg_mode(int m)
{
    if (m < 0) m = 0;
    if (m > 3) m = 3;
    g_cfg.bg_mode = (uint8_t)m;
    if (s_callbacks.on_clock_bg_changed) {
        s_callbacks.on_clock_bg_changed();
    }
    app_cfg_save();
    if (m == 2 && s_callbacks.on_bg_fetch_ensure) {
        s_callbacks.on_bg_fetch_ensure();
    }
}

void app_cfg_set_bg_url(const char *url)
{
    if (!url) url = "";
    strncpy(g_cfg.bg_url, url, sizeof(g_cfg.bg_url) - 1);
    g_cfg.bg_url[sizeof(g_cfg.bg_url) - 1] = 0;
    app_cfg_save();
    if (g_cfg.bg_mode == 2 && s_callbacks.on_bg_fetch_ensure) {
        s_callbacks.on_bg_fetch_ensure();
    }
}

uint32_t app_cfg_get_bg_color(void) { return g_cfg.bg_color; }

void app_cfg_set_bg_color(uint32_t rgba)
{
    g_cfg.bg_color = rgba ? rgba : 0x202020FFu;
    if (g_cfg.bg_mode == 3 && s_callbacks.on_clock_bg_changed) {
        s_callbacks.on_clock_bg_changed();
    }
    app_cfg_save();
}

void app_cfg_set_bg_refresh_s(int s)
{
    if (s < 0) s = 0;
    if (s > 24 * 3600) s = 24 * 3600;
    g_cfg.bg_refresh_s = (uint16_t)s;
    app_cfg_save();
}

void app_cfg_clock_bg_reload(void)
{
    if (s_callbacks.on_clock_bg_changed) {
        s_callbacks.on_clock_bg_changed();
    }
}

void app_cfg_bg_fetch_now(void)
{
    if (g_cfg.bg_mode != 2 || !g_cfg.bg_url[0]) return;
    if (s_callbacks.on_bg_fetch_ensure) {
        s_callbacks.on_bg_fetch_ensure();
    }
}

const char *app_cfg_get_quotes_sym_l(void) { return g_cfg.quotes_sym_l; }
const char *app_cfg_get_quotes_sym_r(void) { return g_cfg.quotes_sym_r; }
int app_cfg_get_quotes_refresh_s(void) { return g_cfg.quotes_refresh_s; }
uint32_t app_cfg_get_quotes_up_rgba(void) { return g_cfg.quotes_up_rgba; }
uint32_t app_cfg_get_quotes_down_rgba(void) { return g_cfg.quotes_down_rgba; }

void app_cfg_set_quotes_sym_l(const char *s)
{
    if (!s) s = "";
    strncpy(g_cfg.quotes_sym_l, s, sizeof(g_cfg.quotes_sym_l) - 1);
    g_cfg.quotes_sym_l[sizeof(g_cfg.quotes_sym_l) - 1] = 0;
    app_cfg_save();
    if (s_callbacks.on_quotes_changed) {
        s_callbacks.on_quotes_changed();
    }
}

void app_cfg_set_quotes_sym_r(const char *s)
{
    if (!s) s = "";
    strncpy(g_cfg.quotes_sym_r, s, sizeof(g_cfg.quotes_sym_r) - 1);
    g_cfg.quotes_sym_r[sizeof(g_cfg.quotes_sym_r) - 1] = 0;
    app_cfg_save();
    if (s_callbacks.on_quotes_changed) {
        s_callbacks.on_quotes_changed();
    }
}

void app_cfg_set_quotes_refresh_s(int s)
{
    if (s < 5) s = 5;
    if (s > 3600) s = 3600;
    g_cfg.quotes_refresh_s = (uint16_t)s;
    app_cfg_save();
}

void app_cfg_set_quotes_up_rgba(uint32_t v)
{
    g_cfg.quotes_up_rgba = v;
    app_cfg_save();
}

void app_cfg_set_quotes_down_rgba(uint32_t v)
{
    g_cfg.quotes_down_rgba = v;
    app_cfg_save();
}

void app_cfg_set_clock_text(const char *s)
{
    if (!s) s = "";
    strncpy(g_cfg.clock_text, s, sizeof(g_cfg.clock_text) - 1);
    g_cfg.clock_text[sizeof(g_cfg.clock_text) - 1] = 0;
    if (g_cfg.clock_text[0]) g_cfg.show_clock = 1;
    if (s_callbacks.on_clock_layout_changed) {
        s_callbacks.on_clock_layout_changed();
    }
    app_cfg_save();
}

void app_cfg_set_clock_pos(int x, int y)
{
    if (x < -512) x = -512;
    if (x > 512) x = 512;
    if (y < -256) y = -256;
    if (y > 256) y = 256;
    g_cfg.clock_x = (int16_t)x;
    g_cfg.clock_y = (int16_t)y;
    if (s_callbacks.on_clock_layout_changed) {
        s_callbacks.on_clock_layout_changed();
    }
    app_cfg_save();
}

void app_cfg_set_clock_size(int sz)
{
    if (sz < 0) sz = 0;
    if (sz > 3) sz = 3;
    g_cfg.clock_size = (uint8_t)sz;
    if (s_callbacks.on_clock_layout_changed) {
        s_callbacks.on_clock_layout_changed();
    }
    app_cfg_save();
}

void app_cfg_set_clock_rgba(uint32_t rgba)
{
    g_cfg.clock_rgba = rgba ? rgba : 0xFFFFFFFFu;
    if (s_callbacks.on_clock_layout_changed) {
        s_callbacks.on_clock_layout_changed();
    }
    app_cfg_save();
}

void app_cfg_set_show_ms(int show)
{
    g_cfg.show_ms = show ? 1 : 0;
    if (s_callbacks.on_clock_layout_changed) {
        s_callbacks.on_clock_layout_changed();
    }
    app_cfg_save();
}

void app_cfg_set_lang(int lang)
{
    if (lang < 0) lang = 0;
    if (lang >= I18N_LANG_COUNT) lang = 0;
    g_cfg.lang = (uint8_t)lang;
    app_cfg_save();
}

void app_cfg_set_brightness(int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_cfg.brightness = (uint8_t)v;
    if (s_callbacks.on_backlight_changed) {
        s_callbacks.on_backlight_changed(g_cfg.brightness);
    }
    app_cfg_save();
}

void app_cfg_set_dim_off(int dim_s, int off_s)
{
    if (dim_s < 0) dim_s = 0;
    if (off_s < 0) off_s = 0;
    g_cfg.dim_s = (uint16_t)dim_s;
    g_cfg.off_s = (uint16_t)off_s;
    if (s_callbacks.on_backlight_changed) {
        s_callbacks.on_backlight_changed(g_cfg.brightness);
    }
    app_cfg_save();
}

void app_cfg_wifi_connect_save(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    app_cfg_save_ssid_pass(ssid, pass ? pass : "");
    strncpy(g_cfg.last_ssid, ssid, sizeof(g_cfg.last_ssid) - 1);
    g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
    app_cfg_save();
    if (s_callbacks.on_wifi_connect) {
        s_callbacks.on_wifi_connect(ssid, pass ? pass : "");
    }
}

void app_cfg_set_active_tile(int idx)
{
    if (!g_tileview) return;
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    if (lvgl_lock(200)) {
        lv_obj_set_tile_id(g_tileview, idx, 0, LV_ANIM_OFF);
        lvgl_unlock();
    }
}