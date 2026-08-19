#ifndef APP_CFG_H
#define APP_CFG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fan_control.h"   /* FanConfig（从老 config 系统迁入） */

#ifdef __cplusplus
extern "C" {
#endif

/* NVS 命名空间定义 */
#define NVS_NS_CFG   "cfg"      /* 配置存储命名空间 */
#define CFG_VERSION  8u         /* 配置版本号，用于配置迁移（v8: 合并老 config 系统字段，升级即重置旧 nasmon 配置） */

/* 默认 WiFi 凭证（可通过 wifi_secret.h 覆盖） */
#define DEFAULT_WIFI_SSID  ""
#define DEFAULT_WIFI_PASS  ""

/**
 * @brief 配置字段枚举
 *
 * 用于 EVENT_CFG_CHANGED 事件标识具体变更的字段。
 * 订阅者可据此过滤关心的事件，避免全量刷新。
 */
typedef enum {
    CFG_FIELD_NONE = 0,
    CFG_FIELD_ALL,              /* 通配：app_cfg_save() 整体保存 */
    CFG_FIELD_TZ_IDX,           /* 时区城市索引 */
    CFG_FIELD_BRIGHTNESS,       /* 背光亮度 */
    CFG_FIELD_DIM_S,            /* 自动变暗延迟 */
    CFG_FIELD_OFF_S,            /* 自动关闭延迟 */
    CFG_FIELD_HOUR24,           /* 24小时制 */
    CFG_FIELD_DATE_FMT,         /* 日期格式 */
    CFG_FIELD_SHOW_SECONDS,     /* 显示秒数 */
    CFG_FIELD_SHOW_MS,          /* 显示毫秒 */
    CFG_FIELD_SHOW_FPS,         /* 显示 FPS */
    CFG_FIELD_AUDIO_ENABLE,     /* 音频使能 */
    CFG_FIELD_AUDIO_VOLUME,     /* 音频音量 */
    CFG_FIELD_THEME,            /* 主题索引 */
    CFG_FIELD_WIFI_AUTOCONNECT, /* WiFi 自动连接 */
    CFG_FIELD_LANG,             /* 语言索引 */
    CFG_FIELD_LAST_SSID,        /* 最后连接的 SSID（触发 WiFi 连接） */
    CFG_FIELD_BG_MODE,          /* 背景模式 */
    CFG_FIELD_BG_URL,           /* 背景图片 URL */
    CFG_FIELD_BG_COLOR,         /* 背景纯色 */
    CFG_FIELD_BG_REFRESH_S,     /* 背景刷新间隔 */
    CFG_FIELD_CLOCK_TEXT,       /* 自定义时钟文本 */
    CFG_FIELD_CLOCK_POS,        /* 时钟位置偏移 */
    CFG_FIELD_CLOCK_SIZE,       /* 时钟字号 */
    CFG_FIELD_CLOCK_RGBA,       /* 时钟文字颜色 */
    CFG_FIELD_SHOW_CLOCK,       /* 显示时钟 */
    CFG_FIELD_QUOTES_SYM_L,     /* 左侧行情符号 */
    CFG_FIELD_QUOTES_SYM_R,     /* 右侧行情符号 */
    CFG_FIELD_QUOTES_REFRESH_S, /* 行情刷新间隔 */
    CFG_FIELD_QUOTES_UP_RGBA,   /* 行情上涨颜色 */
    CFG_FIELD_QUOTES_DOWN_RGBA, /* 行情下跌颜色 */

    /* ---- 以下字段由老 config 系统 (config.c) 合并而来 (v8) ---- */
    CFG_FIELD_NAS_TYPE,         /* NAS 类型字符串 */
    CFG_FIELD_NAS_IP,           /* NAS IP */
    CFG_FIELD_NAS_PORT,         /* NAS 端口 */
    CFG_FIELD_NAS_USER,         /* NAS 用户名 */
    CFG_FIELD_NAS_PASS,         /* NAS 密码 */
    CFG_FIELD_NAS_HTTPS,        /* NAS 是否使用 HTTPS */
    CFG_FIELD_SNMP_COMM,        /* SNMP community */
    CFG_FIELD_SNMP_VER,         /* SNMP 版本 */
    CFG_FIELD_SERIAL_BAUD,      /* 串口波特率 */
    CFG_FIELD_POLL_SEC,         /* 轮询间隔（秒） */
    CFG_FIELD_ROTATION_ANGLE,   /* 屏幕旋转角度 */
    CFG_FIELD_AUTODIM,          /* 自动变暗标志 */
    CFG_FIELD_TIMEZONE,         /* 时区偏移（分钟，带符号） */
    CFG_FIELD_SATA_DISK_COUNT,  /* SATA 硬盘槽位数 */
    CFG_FIELD_M2_DISK_COUNT,    /* M.2 硬盘槽位数 */
    CFG_FIELD_WEATHER_API_KEY,  /* 天气 API key */
    CFG_FIELD_WEATHER_CITY,     /* 天气城市 */
    CFG_FIELD_AUTO_CYCLE_EN,    /* 自动轮播使能 */
    CFG_FIELD_AUTO_CYCLE_INT,   /* 自动轮播间隔（秒） */
    CFG_FIELD_FAN,              /* 风扇配置 (FanConfig) */
} cfg_field_t;

/**
 * @brief 日期格式枚举
 *
 * 与 ui_settings.c 下拉选项顺序、ui_clock.c 渲染顺序严格一致。
 * 分隔符统一使用 '.'（点号）。
 */
typedef enum {
    DATE_FMT_ISO = 0,    /* YYYY.MM.DD（ISO 格式） */
    DATE_FMT_DD_MM = 1,  /* DD.MM.YYYY（日月年） */
    DATE_FMT_MM_DD = 2,  /* MM.DD.YYYY（月日年） */
    DATE_FMT_MAX = 2,    /* 合法最大值（包含） */
} date_fmt_t;

/**
 * @brief 配置变更事件数据
 *
 * EVENT_CFG_CHANGED 事件携带此结构体，标识哪个字段发生了变化。
 * field == CFG_FIELD_ALL 表示整体保存（如 NVS 批量写入）。
 */
typedef struct {
    cfg_field_t field;
} cfg_change_info_t;

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
    uint8_t  date_fmt;          /* 日期格式 (date_fmt_t, 0=YYYY.MM.DD, 1=DD.MM.YYYY, 2=MM.DD.YYYY) */
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

    /* ---- 以下字段由老 config 系统 (config.c) 合并而来 (v8) ---- */
    char     nas_type[16];       /* NAS 类型字符串 (如 "unraid") */
    char     nas_ip[40];         /* NAS IP 地址 */
    uint16_t nas_port;           /* NAS 端口 */
    char     nas_user[32];       /* NAS 用户名 */
    char     nas_pass[65];       /* NAS 密码 */
    uint8_t  nas_https;          /* NAS 是否使用 HTTPS (0/1) */
    char     snmp_comm[32];      /* SNMP community 字符串 */
    uint8_t  snmp_ver;           /* SNMP 版本 (0=v1, 1=v2c) */
    uint32_t serial_baud;        /* 串口波特率 */
    uint8_t  poll_sec;           /* 轮询间隔（秒） */
    uint8_t  rotation_angle;     /* 屏幕旋转角度 */
    uint8_t  autodim;            /* 自动变暗标志 */
    int8_t   timezone;           /* 时区偏移（小时，带符号） */
    uint8_t  sata_disk_count;    /* SATA 硬盘槽位数 */
    uint8_t  m2_disk_count;      /* M.2 硬盘槽位数 */
    char     weather_api_key[65];/* 天气 API key */
    char     weather_city[32];   /* 天气城市 */
    uint8_t  auto_cycle_enabled; /* 自动轮播使能 */
    uint8_t  auto_cycle_interval_sec; /* 自动轮播间隔（秒） */
    FanConfig fan;               /* 风扇配置 */
} app_cfg_t;

/**
 * @brief 配置变更回调函数结构体
 *
 * @deprecated 已迁移到事件总线（EVENT_CFG_CHANGED），
 *             保留空结构体仅为兼容旧代码，未来版本将移除。
 */

/* 全局配置实例 */
extern app_cfg_t g_cfg;

/* 核心 API */
void app_cfg_init(void);                                            /* 初始化配置模块，从 NVS 加载配置 */
void app_cfg_load(void);                                            /* 加载配置（调用 app_cfg_init） */
void app_cfg_save(void);                                            /* 保存配置到 NVS（增量：只写脏字段） */
void app_cfg_flush(void);                                           /* 立即刷新脏字段到 NVS（同步阻塞，用于关机等场景） */
size_t app_cfg_get_last_ssid(char *buf, size_t buf_len);  /* 获取最后连接的 SSID（线程安全） */

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
void app_cfg_set_last_ssid(const char *ssid);   /* 设置最后连接的 SSID（线程安全，自动保存） */
void app_cfg_set_active_tile(int idx);              /* 设置当前活动的 Tile 索引 */

/* 新增 setter：对应 ui_settings.c 中的直接赋值场景 */
void app_cfg_set_tz_idx(int idx);                   /* 设置时区城市索引 */
void app_cfg_set_hour24(int enable);                 /* 设置 24 小时制 */
void app_cfg_set_date_fmt(int fmt);                  /* 设置日期格式 */
void app_cfg_set_show_seconds(int show);             /* 设置是否显示秒数（已存在） */
void app_cfg_set_show_fps(int show);                 /* 设置是否显示 FPS */
void app_cfg_set_audio_enable(int enable);           /* 设置音频使能 */
void app_cfg_set_audio_volume(int vol);              /* 设置音频音量 */
void app_cfg_set_theme(int theme);                   /* 设置主题索引 */
void app_cfg_set_wifi_autoconnect(int enable);       /* 设置 WiFi 自动连接 */

/* ---- 以下 getter/setter 由老 config 系统 (config.c) 合并而来 (v8) ---- */
const char *app_cfg_get_nas_type(void);
const char *app_cfg_get_nas_ip(void);
int         app_cfg_get_nas_port(void);
const char *app_cfg_get_nas_user(void);
const char *app_cfg_get_nas_pass(void);
int         app_cfg_get_nas_https(void);
const char *app_cfg_get_snmp_comm(void);
int         app_cfg_get_snmp_ver(void);
int         app_cfg_get_serial_baud(void);
int         app_cfg_get_poll_sec(void);
int         app_cfg_get_rotation_angle(void);
int         app_cfg_get_autodim(void);
int         app_cfg_get_timezone(void);
int         app_cfg_get_sata_disk_count(void);
int         app_cfg_get_m2_disk_count(void);
const char *app_cfg_get_weather_api_key(void);
const char *app_cfg_get_weather_city(void);
int         app_cfg_get_auto_cycle_enabled(void);
int         app_cfg_get_auto_cycle_interval_sec(void);
FanConfig   app_cfg_get_fan(void);

void app_cfg_set_nas_type(const char *v);
void app_cfg_set_nas_ip(const char *v);
void app_cfg_set_nas_port(int v);
void app_cfg_set_nas_user(const char *v);
void app_cfg_set_nas_pass(const char *v);
void app_cfg_set_nas_https(int v);
void app_cfg_set_snmp_comm(const char *v);
void app_cfg_set_snmp_ver(int v);
void app_cfg_set_serial_baud(int v);
void app_cfg_set_poll_sec(int v);
void app_cfg_set_rotation_angle(int v);
void app_cfg_set_autodim(int v);
void app_cfg_set_timezone(int v);
void app_cfg_set_sata_disk_count(int v);
void app_cfg_set_m2_disk_count(int v);
void app_cfg_set_weather_api_key(const char *v);
void app_cfg_set_weather_city(const char *v);
void app_cfg_set_auto_cycle_enabled(int v);
void app_cfg_set_auto_cycle_interval_sec(int v);
void app_cfg_set_fan(const FanConfig *v);

/* 槽位类型判断（替代老 config.h 的 config_is_sata_slot / config_is_m2_slot） */
static inline int app_cfg_is_sata_slot(int i)
{
    return i >= 0 && i < app_cfg_get_sata_disk_count();
}
static inline int app_cfg_is_m2_slot(int i)
{
    int sata = app_cfg_get_sata_disk_count();
    return i >= sata && i < sata + app_cfg_get_m2_disk_count();
}



#ifdef __cplusplus
}
#endif

#endif
