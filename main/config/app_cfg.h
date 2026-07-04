#ifndef APP_CFG_H
#define APP_CFG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_NS_CFG   "cfg"
#define NVS_NS_WIFI  "wifi"
#define CFG_VERSION  7u

#define DEFAULT_WIFI_SSID  ""
#define DEFAULT_WIFI_PASS  ""

typedef struct {
    uint8_t  version;
    uint16_t tz_idx;
    uint8_t  brightness;
    uint16_t dim_s;
    uint16_t off_s;
    char     last_ssid[33];
    uint8_t  hour24;
    uint8_t  date_fmt;
    uint8_t  show_seconds;
    uint8_t  show_ms;
    uint8_t  audio_enable;
    uint8_t  audio_volume;
    uint8_t  theme;
    uint8_t  show_fps;
    uint8_t  wifi_autoconnect;
    uint8_t  lang;
    int16_t  clock_x;
    int16_t  clock_y;
    uint8_t  clock_size;
    uint32_t clock_rgba;
    uint8_t  show_clock;
    char     clock_text[33];
    uint8_t  bg_mode;
    uint16_t bg_refresh_s;
    char     bg_url[128];
    uint32_t bg_color;
    char     quotes_sym_l[16];
    char     quotes_sym_r[16];
    uint16_t quotes_refresh_s;
    uint32_t quotes_up_rgba;
    uint32_t quotes_down_rgba;
} app_cfg_t;

typedef struct {
    void (*on_clock_layout_changed)(void);
    void (*on_clock_bg_changed)(void);
    void (*on_quotes_changed)(void);
    void (*on_backlight_changed)(uint8_t brightness);
    void (*on_bg_fetch_ensure)(void);
    void (*on_wifi_connect)(const char *ssid, const char *pass);
} app_cfg_callbacks_t;

extern app_cfg_t g_cfg;

void app_cfg_init(void);
void app_cfg_load(void);
void app_cfg_save(void);
void app_cfg_save_ssid_pass(const char *ssid, const char *pass);
bool app_cfg_get_ssid_pass(const char *ssid, char *pass, size_t pass_len);
void app_cfg_register_callbacks(const app_cfg_callbacks_t *cb);

int  app_cfg_get_lang(void);
int  app_cfg_get_brightness(void);
int  app_cfg_get_dim_s(void);
int  app_cfg_get_off_s(void);

int  app_cfg_get_clock_x(void);
int  app_cfg_get_clock_y(void);
int  app_cfg_get_clock_size(void);
uint32_t app_cfg_get_clock_rgba(void);
int  app_cfg_get_show_ms(void);
int  app_cfg_get_show_seconds(void);
void app_cfg_set_show_seconds(int show);
int  app_cfg_get_show_clock(void);
void app_cfg_set_show_clock(int show);
const char *app_cfg_get_clock_text(void);

int  app_cfg_get_bg_mode(void);
int  app_cfg_get_bg_refresh_s(void);
const char *app_cfg_get_bg_url(void);
int  app_cfg_get_canvas_w(void);
int  app_cfg_get_canvas_h(void);
void app_cfg_set_bg_mode(int m);
void app_cfg_set_bg_url(const char *url);
uint32_t app_cfg_get_bg_color(void);
void app_cfg_set_bg_color(uint32_t rgba);
void app_cfg_set_bg_refresh_s(int s);
void app_cfg_clock_bg_reload(void);
void app_cfg_bg_fetch_now(void);

const char *app_cfg_get_quotes_sym_l(void);
const char *app_cfg_get_quotes_sym_r(void);
int  app_cfg_get_quotes_refresh_s(void);
uint32_t app_cfg_get_quotes_up_rgba(void);
uint32_t app_cfg_get_quotes_down_rgba(void);
void app_cfg_set_quotes_sym_l(const char *s);
void app_cfg_set_quotes_sym_r(const char *s);
void app_cfg_set_quotes_refresh_s(int s);
void app_cfg_set_quotes_up_rgba(uint32_t v);
void app_cfg_set_quotes_down_rgba(uint32_t v);

void app_cfg_set_clock_text(const char *s);
void app_cfg_set_clock_pos(int x, int y);
void app_cfg_set_clock_size(int sz);
void app_cfg_set_clock_rgba(uint32_t rgba);
void app_cfg_set_show_ms(int show);
void app_cfg_set_lang(int lang);
void app_cfg_set_brightness(int v);
void app_cfg_set_dim_off(int dim_s, int off_s);
void app_cfg_wifi_connect_save(const char *ssid, const char *pass);
void app_cfg_set_active_tile(int idx);

#ifdef __cplusplus
}
#endif

#endif