#ifndef APP_CFG_H
#define APP_CFG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NVS 命名空间定义 */
#define NVS_NS_CFG   "cfg"      /* 配置存储命名空间 */
#define NVS_NS_WIFI  "wifi"     /* WiFi 凭证存储命名空间 */
#define CFG_VERSION  7u         /* 配置版本号，用于配置迁移 */

/* 默认 WiFi 凭证（可通过 wifi_secret.h 覆盖） */
#define DEFAULT_WIFI_SSID  ""
#define DEFAULT_WIFI_PASS  ""

/**
 * @brief 应用配置结构体
 * 
 * 存储所有用户可配置的参数，通过 NVS 持久化保存
 */
typedef struct {
    uint8_t  version;           /* 配置版本号 */
    uint16_t tz_idx;            /* 时区城市索引 */
    uint8_t  brightness;        /* 背光亮度 (0-255) */
    uint16_t dim_s;             /* 自动变暗延迟时间（秒） */
    uint16_t off_s;             /* 自动关闭延迟时间（秒） */
    char     last_ssid[33];     /* 最后连接的 WiFi SSID */
    uint8_t  hour24;            /* 24小时制标志 (0=12小时制, 1=24小时制) */
    uint8_t  date_fmt;          /* 日期格式 (0=YYYY-MM-DD, 1=MM/DD/YYYY, 2=DD/MM/YYYY) */
    uint8_t  show_seconds;      /* 显示秒数标志 */
    uint8_t  show_ms;           /* 显示毫秒标志 */
    uint8_t  audio_enable;      /* 音频使能标志 */
    uint8_t  audio_volume;      /* 音频音量 (0-100) */
    uint8_t  theme;             /* 主题索引 */
    uint8_t  show_fps;          /* 显示 FPS 标志 */
    uint8_t  wifi_autoconnect;  /* WiFi 自动连接标志 */
    uint8_t  lang;              /* 语言索引 */
    int16_t  clock_x;           /* 时钟水平偏移位置 */
    int16_t  clock_y;           /* 时钟垂直偏移位置 */
    uint8_t  clock_size;        /* 时钟字号大小 (0-3) */
    uint32_t clock_rgba;        /* 时钟文字颜色 (RGBA) */
    uint8_t  show_clock;        /* 显示时钟标志 */
    char     clock_text[33];    /* 自定义时钟文本 */
    uint8_t  bg_mode;           /* 背景模式 (0=深色, 1=浅色, 2=图片, 3=纯色) */
    uint16_t bg_refresh_s;      /* 背景刷新间隔（秒） */
    char     bg_url[128];       /* 背景图片 URL */
    uint32_t bg_color;          /* 背景纯色 (RGBA) */
    char     quotes_sym_l[16];  /* 左侧行情符号（如 xauusd） */
    char     quotes_sym_r[16];  /* 右侧行情符号（如 xagusd） */
    uint16_t quotes_refresh_s;  /* 行情刷新间隔（秒） */
    uint32_t quotes_up_rgba;    /* 行情上涨颜色 (RGBA) */
    uint32_t quotes_down_rgba;  /* 行情下跌颜色 (RGBA) */
} app_cfg_t;

/**
 * @brief 配置变更回调函数结构体
 * 
 * 当配置发生变化时，通过回调通知相关模块进行响应
 */
typedef struct {
    void (*on_quotes_changed)(void);              /* 行情配置变更回调 */
    void (*on_backlight_changed)(uint8_t brightness); /* 背光亮度变更回调 */
    void (*on_bg_fetch_ensure)(void);             /* 确保背景图片获取回调 */
    void (*on_wifi_connect)(const char *ssid, const char *pass); /* WiFi 连接回调 */
} app_cfg_callbacks_t;

/* 全局配置实例 */
extern app_cfg_t g_cfg;

/* 核心 API */
void app_cfg_init(void);                                            /* 初始化配置模块，从 NVS 加载配置 */
void app_cfg_load(void);                                            /* 加载配置（调用 app_cfg_init） */
void app_cfg_save(void);                                            /* 保存配置到 NVS */
void app_cfg_save_ssid_pass(const char *ssid, const char *pass);    /* 保存 WiFi SSID 和密码到 NVS */
bool app_cfg_get_ssid_pass(const char *ssid, char *pass, size_t pass_len); /* 获取指定 SSID 的密码 */
void app_cfg_register_callbacks(const app_cfg_callbacks_t *cb);     /* 注册配置变更回调函数 */

/* 获取器 API */
int  app_cfg_get_lang(void);                        /* 获取语言索引 */
int  app_cfg_get_brightness(void);                  /* 获取背光亮度 */
int  app_cfg_get_dim_s(void);                       /* 获取自动变暗延迟时间 */
int  app_cfg_get_off_s(void);                       /* 获取自动关闭延迟时间 */

int  app_cfg_get_clock_x(void);                     /* 获取时钟水平偏移 */
int  app_cfg_get_clock_y(void);                     /* 获取时钟垂直偏移 */
int  app_cfg_get_clock_size(void);                  /* 获取时钟字号大小 */
uint32_t app_cfg_get_clock_rgba(void);              /* 获取时钟文字颜色 */
int  app_cfg_get_show_ms(void);                     /* 获取是否显示毫秒 */
int  app_cfg_get_show_seconds(void);                /* 获取是否显示秒数 */
void app_cfg_set_show_seconds(int show);            /* 设置是否显示秒数 */
int  app_cfg_get_show_clock(void);                  /* 获取是否显示时钟 */
void app_cfg_set_show_clock(int show);              /* 设置是否显示时钟 */
const char *app_cfg_get_clock_text(void);           /* 获取自定义时钟文本 */

int  app_cfg_get_bg_mode(void);                     /* 获取背景模式 */
int  app_cfg_get_bg_refresh_s(void);                /* 获取背景刷新间隔 */
const char *app_cfg_get_bg_url(void);               /* 获取背景图片 URL */
int  app_cfg_get_canvas_w(void);                    /* 获取画布宽度 */
int  app_cfg_get_canvas_h(void);                    /* 获取画布高度 */
void app_cfg_set_bg_mode(int m);                    /* 设置背景模式 */
void app_cfg_set_bg_url(const char *url);           /* 设置背景图片 URL */
uint32_t app_cfg_get_bg_color(void);                /* 获取背景纯色 */
void app_cfg_set_bg_color(uint32_t rgba);           /* 设置背景纯色 */
void app_cfg_set_bg_refresh_s(int s);               /* 设置背景刷新间隔 */
void app_cfg_clock_bg_reload(void);                 /* 重新加载背景 */
void app_cfg_bg_fetch_now(void);                    /* 立即获取背景图片 */

const char *app_cfg_get_quotes_sym_l(void);         /* 获取左侧行情符号 */
const char *app_cfg_get_quotes_sym_r(void);         /* 获取右侧行情符号 */
int  app_cfg_get_quotes_refresh_s(void);            /* 获取行情刷新间隔 */
uint32_t app_cfg_get_quotes_up_rgba(void);          /* 获取行情上涨颜色 */
uint32_t app_cfg_get_quotes_down_rgba(void);        /* 获取行情下跌颜色 */
void app_cfg_set_quotes_sym_l(const char *s);       /* 设置左侧行情符号 */
void app_cfg_set_quotes_sym_r(const char *s);       /* 设置右侧行情符号 */
void app_cfg_set_quotes_refresh_s(int s);           /* 设置行情刷新间隔 */
void app_cfg_set_quotes_up_rgba(uint32_t v);        /* 设置行情上涨颜色 */
void app_cfg_set_quotes_down_rgba(uint32_t v);      /* 设置行情下跌颜色 */

void app_cfg_set_clock_text(const char *s);         /* 设置自定义时钟文本 */
void app_cfg_set_clock_pos(int x, int y);           /* 设置时钟位置偏移 */
void app_cfg_set_clock_size(int sz);                /* 设置时钟字号大小 */
void app_cfg_set_clock_rgba(uint32_t rgba);         /* 设置时钟文字颜色 */
void app_cfg_set_show_ms(int show);                 /* 设置是否显示毫秒 */
void app_cfg_set_lang(int lang);                    /* 设置语言索引 */
void app_cfg_set_brightness(int v);                 /* 设置背光亮度 */
void app_cfg_set_dim_off(int dim_s, int off_s);     /* 设置自动变暗和关闭时间 */
void app_cfg_wifi_connect_save(const char *ssid, const char *pass); /* 保存 WiFi 连接信息并触发连接 */
void app_cfg_set_active_tile(int idx);              /* 设置当前活动的 Tile 索引 */

#ifdef __cplusplus
}
#endif

#endif
