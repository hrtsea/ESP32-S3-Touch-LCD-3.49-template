#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "tz_cities.h"
#include "i18n.h"
#include "user_config.h"

#include "app_cfg.h"
#include "event_bus.h"
#include "esp_bus.h"

/* 如果存在 wifi_secret.h，则包含它以获取默认 WiFi 凭证 */
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

/* g_cfg 互斥锁：保护跨任务读写（尤其是字符串字段） */
static SemaphoreHandle_t s_cfg_mutex = NULL;

#include "../utils/i18n.h"

/**
 * @brief 全局配置实例，使用默认值初始化
 * 
 * 当 NVS 中没有配置时，使用这些默认值
 */
app_cfg_t g_cfg = {
    .version           = CFG_VERSION,     /* 配置版本号 */
    .tz_idx            = TZ_DEFAULT_CITY_INDEX, /* 默认时区索引 */
    .brightness        = 255,             /* 默认最大亮度 */
    .dim_s             = 8 * 3600,        /* 默认8小时后变暗 */
    .off_s             = 8 * 3600,        /* 默认8小时后关闭 */
    .last_ssid         = {0},             /* 无默认 SSID */
    .hour24            = 1,               /* 默认24小时制 */
    .date_fmt          = 0,               /* 默认日期格式 YYYY-MM-DD */
    .show_seconds      = 1,               /* 默认显示秒数 */
    .show_ms           = 1,               /* 默认显示毫秒 */
    .audio_enable      = 1,               /* 默认开启音频 */
    .audio_volume      = 70,              /* 默认音量70% */
    .theme             = 0,               /* 默认主题 */
    .show_fps          = 1,               /* 默认显示 FPS */
    .wifi_autoconnect  = 1,               /* 默认自动连接 WiFi */
    .lang              = 0,               /* 默认语言 */
    .clock_x           = -52,             /* 时钟水平偏移 */
    .clock_y           = 0,               /* 时钟垂直偏移 */
    .clock_size        = 3,               /* 默认最大字号 */
    .clock_rgba        = 0xFFFFFFFF,      /* 默认白色 */
    .show_clock        = 1,               /* 默认显示时钟 */
    .clock_text        = {0},             /* 无自定义文本 */
    .bg_mode           = 0,               /* 默认深色背景 */
    .bg_refresh_s      = 0,               /* 默认不自动刷新背景 */
    .bg_url            = {0},             /* 无默认背景 URL */
    .bg_color          = 0x202020FFu,     /* 默认深灰色背景 */
    .quotes_sym_l      = "xauusd",        /* 默认左侧黄金行情 */
    .quotes_sym_r      = "xagusd",        /* 默认右侧白银行情 */
    .quotes_refresh_s  = 60,              /* 默认60秒刷新一次行情 */
    .quotes_up_rgba    = 0x33DD66FFu,     /* 默认绿色上涨 */
    .quotes_down_rgba  = 0xFF4040FFu,     /* 默认红色下跌 */

    /* v8: 由老 config 系统 (config.c) 合并而来 */
    .nas_type          = "mock",            /* 默认 NAS 类型(本地模拟,便于无 NAS 环境开发调试) */
    .nas_ip            = {0},              /* 无默认 IP */
    .nas_port          = 0,                /* 0 = 使用协议默认端口 */
    .nas_user          = {0},
    .nas_pass          = {0},
    .nas_https         = 0,
    .snmp_comm         = "public",
    .snmp_ver          = 1,                /* 默认 v2c */
    .serial_baud       = 115200,
    .poll_sec          = 10,
    .rotation_angle    = 0,
    .autodim           = 1,
    .timezone          = 0,
    .sata_disk_count   = 0,
    .m2_disk_count     = 0,
    .weather_api_key   = {0},
    .weather_city      = {0},
    .auto_cycle_enabled = 0,
    .auto_cycle_interval_sec = 10,
};

/* 延迟落盘标志：配置变更后先标记脏，由 esp_timer 周期统一写入 NVS（C1 去抖） */
static bool                s_dirty = false;
static esp_timer_handle_t  s_flush_timer = NULL;

static void cfg_write_all_to_nvs(void);
static void cfg_timer_cb(void *arg);
static void cfg_mark_dirty(void);

/**
 * @brief 延迟落盘定时器回调：若有脏配置则统一写盘
 */
static void cfg_timer_cb(void *arg)
{
    (void)arg;
    if (s_dirty) {
        s_dirty = false;
        cfg_write_all_to_nvs();
    }
}

/**
 * @brief 标记配置为脏，触发延迟落盘
 *
 * 高频 setter 仅标记，不每次提交 NVS，由周期定时器合并提交（防磨损）。
 * 首次标记时创建定时器（1s 周期，一次性触发后停止），避免空转。
 */
static void cfg_mark_dirty(void)
{
    s_dirty = true;
    if (s_flush_timer == NULL) {
        esp_timer_create_args_t args = {
            .callback = cfg_timer_cb,
            .name = "cfg_flush",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &s_flush_timer) != ESP_OK) return;
    }
    /* 每次变更都重置 1s 倒计时：最后变更后 1s 合并落盘 */
    esp_timer_start_once(s_flush_timer, 1000000);
}

/**
 * @brief 发布配置变更事件
 *
 * 统一的发布入口，避免每个 setter 重复样板代码。
 * 具体字段变更会顺带标记脏（延迟落盘），CFG_FIELD_ALL 除外。
 */
static void cfg_publish(cfg_field_t field)
{
    if (field != CFG_FIELD_ALL) cfg_mark_dirty();
    cfg_change_info_t info = { .field = field };
    event_bus_publish(EVENT_CFG_CHANGED, &info, sizeof(info));
}

/**
 * @brief 验证配置参数的有效性
 * 
 * 确保配置值在合理范围内，防止无效值导致异常
 */
static void cfg_validate(void)
{
    if (g_cfg.tz_idx >= TZ_CITY_COUNT) g_cfg.tz_idx = TZ_DEFAULT_CITY_INDEX; /* 时区索引校验 */
    if (g_cfg.date_fmt > DATE_FMT_MAX) g_cfg.date_fmt = 0; /* 日期格式校验 */
    if (g_cfg.theme > 2) g_cfg.theme = 0; /* 主题索引校验 */
    if (g_cfg.audio_volume > 100) g_cfg.audio_volume = 100; /* 音量范围校验 */
    if (g_cfg.clock_size > 3) g_cfg.clock_size = 3; /* 时钟字号校验 */
    if (g_cfg.bg_mode > 3) g_cfg.bg_mode = 0; /* 背景模式校验 */
    if ((g_cfg.clock_rgba & 0xFF) == 0) g_cfg.clock_rgba = 0xFFFFFFFFu; /* 透明度校验 */
    
    /* 布尔值规范化 */
    g_cfg.show_clock = g_cfg.show_clock ? 1 : 0;
    g_cfg.hour24 = g_cfg.hour24 ? 1 : 0;
    g_cfg.show_seconds = g_cfg.show_seconds ? 1 : 0;
    g_cfg.show_ms = g_cfg.show_ms ? 1 : 0;
    g_cfg.audio_enable = g_cfg.audio_enable ? 1 : 0;
    g_cfg.show_fps = g_cfg.show_fps ? 1 : 0;
    g_cfg.wifi_autoconnect = g_cfg.wifi_autoconnect ? 1 : 0;

    /* v8 合并字段校验 */
    g_cfg.nas_https = g_cfg.nas_https ? 1 : 0;
    g_cfg.autodim = g_cfg.autodim ? 1 : 0;
    g_cfg.snmp_ver = g_cfg.snmp_ver ? 1 : 0;  /* 仅 0/1 */
    if (g_cfg.poll_sec == 0) g_cfg.poll_sec = 10;
    if (g_cfg.rotation_angle > 270) g_cfg.rotation_angle = 0; /* 仅支持 0/90/180/270 */
    if (g_cfg.sata_disk_count > 16) g_cfg.sata_disk_count = 16;
    if (g_cfg.m2_disk_count > 16) g_cfg.m2_disk_count = 16;
    g_cfg.auto_cycle_enabled = g_cfg.auto_cycle_enabled ? 1 : 0;
    if (g_cfg.auto_cycle_interval_sec == 0) g_cfg.auto_cycle_interval_sec = 10;
}

/**
 * @brief 配置版本迁移
 * 
 * 当加载的配置版本低于当前版本时，执行迁移逻辑
 * 
 * @param from_ver 加载的配置版本号
 */
static void cfg_migrate(uint8_t from_ver)
{
    if (from_ver < 7) {
        ESP_LOGI(TAG, "cfg: migrate v%u -> v%u", (unsigned)from_ver, (unsigned)CFG_VERSION);
        /* 版本 7 的迁移逻辑可在此添加 */
    }
    if (from_ver < 8) {
        /* v8: 合并老 config 系统 (config.c) 字段到 app_cfg。
         * 接受"升级即重置"策略——老 "nasmon" 命名空间数据不迁移，
         * 新字段使用 g_cfg 默认值，用户需在 UI / WebUI 重新配置。 */
        ESP_LOGI(TAG, "cfg: migrated to v8 (old 'nasmon' config not carried over)");
    }
    g_cfg.version = CFG_VERSION;
}

/**
 * @brief 从 NVS 读取配置
 * 
 * @param h NVS 句柄
 */
static void cfg_read_nvs(nvs_handle_t h)
{
    /* 基础配置 */
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
    
    /* 时钟配置 */
    nvs_get_i16(h, "clk_x",     &g_cfg.clock_x);
    nvs_get_i16(h, "clk_y",     &g_cfg.clock_y);
    nvs_get_u8 (h, "clk_sz",    &g_cfg.clock_size);
    nvs_get_u32(h, "clk_rgba",  &g_cfg.clock_rgba);
    nvs_get_u8 (h, "clk_show",  &g_cfg.show_clock);
    
    /* 背景配置 */
    nvs_get_u8 (h, "bg_mode",   &g_cfg.bg_mode);
    nvs_get_u16(h, "bg_refr",   &g_cfg.bg_refresh_s);
    nvs_get_u32(h, "bg_color",  &g_cfg.bg_color);
    
    /* 字符串配置 */
    size_t sl = sizeof(g_cfg.last_ssid);
    nvs_get_str(h, "last_ssid", g_cfg.last_ssid, &sl);

    size_t ctl = sizeof(g_cfg.clock_text);
    nvs_get_str(h, "clk_text",  g_cfg.clock_text, &ctl);

    size_t bgul = sizeof(g_cfg.bg_url);
    nvs_get_str(h, "bg_url",    g_cfg.bg_url, &bgul);

    /* 行情配置 */
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

    /* v8: 老 config 系统合并字段 */
    size_t ntl = sizeof(g_cfg.nas_type);  nvs_get_str(h, "nas_type", g_cfg.nas_type, &ntl);
    size_t nil = sizeof(g_cfg.nas_ip);    nvs_get_str(h, "nas_ip",   g_cfg.nas_ip,   &nil);
    nvs_get_u16(h, "nas_port",  &g_cfg.nas_port);
    size_t nul = sizeof(g_cfg.nas_user);  nvs_get_str(h, "nas_user", g_cfg.nas_user, &nul);
    size_t npl = sizeof(g_cfg.nas_pass);  nvs_get_str(h, "nas_pass", g_cfg.nas_pass, &npl);
    nvs_get_u8 (h, "nas_https", &g_cfg.nas_https);
    size_t scl = sizeof(g_cfg.snmp_comm); nvs_get_str(h, "snmp_comm", g_cfg.snmp_comm, &scl);
    nvs_get_u8 (h, "snmp_ver",  &g_cfg.snmp_ver);
    nvs_get_u32(h, "serial_bd", &g_cfg.serial_baud);
    nvs_get_u8 (h, "poll_sec",  &g_cfg.poll_sec);
    nvs_get_u8 (h, "rot_ang",   &g_cfg.rotation_angle);
    nvs_get_u8 (h, "autodim",   &g_cfg.autodim);
    int8_t tz;
    if (nvs_get_i8(h, "tz_off", &tz) == ESP_OK) g_cfg.timezone = tz;
    nvs_get_u8 (h, "sata_cnt",  &g_cfg.sata_disk_count);
    nvs_get_u8 (h, "m2_cnt",    &g_cfg.m2_disk_count);
    size_t wkl = sizeof(g_cfg.weather_api_key); nvs_get_str(h, "wx_key", g_cfg.weather_api_key, &wkl);
    size_t wcl = sizeof(g_cfg.weather_city);    nvs_get_str(h, "wx_city", g_cfg.weather_city, &wcl);
    nvs_get_u8 (h, "auto_cyc",  &g_cfg.auto_cycle_enabled);
    nvs_get_u8 (h, "auto_cyc_i",&g_cfg.auto_cycle_interval_sec);

    size_t fbl = sizeof(g_cfg.fan);
    nvs_get_blob(h, "fan", &g_cfg.fan, &fbl);
}

/**
 * @brief 获取配置互斥锁（阻塞式）
 */
static void cfg_lock(void)
{
    if (s_cfg_mutex) {
        xSemaphoreTake(s_cfg_mutex, portMAX_DELAY);
    }
}

/**
 * @brief 释放配置互斥锁
 */
static void cfg_unlock(void)
{
    if (s_cfg_mutex) {
        xSemaphoreGive(s_cfg_mutex);
    }
}

/**
 * @brief 初始化配置模块
 * 
 * 初始化 NVS Flash，从 NVS 加载配置，执行版本迁移和参数验证
 */
void app_cfg_init(void)
{
    /* 初始化互斥锁 */
    s_cfg_mutex = xSemaphoreCreateMutex();

    /* 初始化 NVS Flash */
    esp_err_t er = nvs_flash_init();
    if (er == ESP_ERR_NVS_NO_FREE_PAGES || er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS 页面已满或版本不匹配，擦除后重新初始化 */
        ESP_ERROR_CHECK(nvs_flash_erase());
        er = nvs_flash_init();
    }
    ESP_ERROR_CHECK(er);

    /* 打开配置命名空间并读取配置 */
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READONLY, &h) != ESP_OK) return;
    cfg_read_nvs(h);
    nvs_close(h);

    /* 保存加载的版本号，用于后续迁移判断 */
    uint8_t loaded_ver = g_cfg.version;
    
    /* 验证配置参数有效性 */
    cfg_validate();

    /* 如果配置版本低于当前版本，执行迁移 */
    if (loaded_ver < CFG_VERSION) {
        cfg_migrate(loaded_ver);
        
        /* 默认 WiFi 凭证改由 esp_wifi_config 的 default_networks 在 NVS 空时回退注入，
         * 见 main.cpp network_init()，此处不再重复存储到 app_cfg。 */
        if (DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0]) {
            strncpy(g_cfg.last_ssid, DEFAULT_WIFI_SSID, sizeof(g_cfg.last_ssid) - 1);
        }
        
        /* 保存迁移后的配置（迁移必须立即落盘，不延迟） */
        app_cfg_flush();
    }

    /* 输出配置信息日志 */
    const char *city_name = tz_current_city_name();
    const char *tz_posix = k_tz_cities[g_cfg.tz_idx].posix_tz;
    ESP_LOGI(TAG, "cfg: tz=%s (%s) bri=%u dim=%us off=%us last_ssid=%s",
             city_name ? city_name : "(unknown)",
             tz_posix ? tz_posix : "(unknown)",
             g_cfg.brightness, g_cfg.dim_s, g_cfg.off_s,
             g_cfg.last_ssid[0] ? g_cfg.last_ssid : "(none)");


}

/**
 * @brief 加载配置（调用 app_cfg_init）
 */
void app_cfg_load(void)
{
    app_cfg_init();
}

/**
 * @brief 将全部配置字段写入 NVS 并提交
 *
 * 内部公共写盘逻辑，供延迟提交（tick）与立即落盘（flush）复用。
 */
static void cfg_write_all_to_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READWRITE, &h) != ESP_OK) return;

    /* 基础配置 */
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

    /* 时钟配置 */
    nvs_set_i16(h, "clk_x",     g_cfg.clock_x);
    nvs_set_i16(h, "clk_y",     g_cfg.clock_y);
    nvs_set_u8 (h, "clk_sz",    g_cfg.clock_size);
    nvs_set_u32(h, "clk_rgba",  g_cfg.clock_rgba);
    nvs_set_u8 (h, "clk_show",  g_cfg.show_clock);
    nvs_set_str(h, "clk_text",  g_cfg.clock_text);

    /* 背景配置 */
    nvs_set_u8 (h, "bg_mode",   g_cfg.bg_mode);
    nvs_set_u16(h, "bg_refr",   g_cfg.bg_refresh_s);
    nvs_set_str(h, "bg_url",    g_cfg.bg_url);
    nvs_set_u32(h, "bg_color",  g_cfg.bg_color);

    /* 行情配置 */
    nvs_set_str(h, "q_sl",      g_cfg.quotes_sym_l);
    nvs_set_str(h, "q_sr",      g_cfg.quotes_sym_r);
    nvs_set_u16(h, "q_refr",    g_cfg.quotes_refresh_s);
    nvs_set_u32(h, "q_up",      g_cfg.quotes_up_rgba);
    nvs_set_u32(h, "q_dn",      g_cfg.quotes_down_rgba);

    /* v8: 老 config 系统合并字段 */
    nvs_set_str(h, "nas_type",  g_cfg.nas_type);
    nvs_set_str(h, "nas_ip",    g_cfg.nas_ip);
    nvs_set_u16(h, "nas_port",  g_cfg.nas_port);
    nvs_set_str(h, "nas_user",  g_cfg.nas_user);
    nvs_set_str(h, "nas_pass",  g_cfg.nas_pass);
    nvs_set_u8 (h, "nas_https", g_cfg.nas_https);
    nvs_set_str(h, "snmp_comm", g_cfg.snmp_comm);
    nvs_set_u8 (h, "snmp_ver",  g_cfg.snmp_ver);
    nvs_set_u32(h, "serial_bd", g_cfg.serial_baud);
    nvs_set_u8 (h, "poll_sec",  g_cfg.poll_sec);
    nvs_set_u8 (h, "rot_ang",   g_cfg.rotation_angle);
    nvs_set_u8 (h, "autodim",   g_cfg.autodim);
    nvs_set_i8 (h, "tz_off",    (int8_t)g_cfg.timezone);
    nvs_set_u8 (h, "sata_cnt",  g_cfg.sata_disk_count);
    nvs_set_u8 (h, "m2_cnt",    g_cfg.m2_disk_count);
    nvs_set_str(h, "wx_key",    g_cfg.weather_api_key);
    nvs_set_str(h, "wx_city",   g_cfg.weather_city);
    nvs_set_u8 (h, "auto_cyc",  g_cfg.auto_cycle_enabled);
    nvs_set_u8 (h, "auto_cyc_i",g_cfg.auto_cycle_interval_sec);
    nvs_set_blob(h, "fan", &g_cfg.fan, sizeof(g_cfg.fan));

    /* WiFi 配置 */
    nvs_set_str(h, "last_ssid", g_cfg.last_ssid);

    /* 提交更改并关闭 NVS */
    nvs_commit(h);
    nvs_close(h);
}

/**
 * @brief 保存配置到 NVS（延迟）
 *
 * C1 去抖：仅标记脏，由 1Hz tick 统一合并写入 NVS，避免高频 setter
 * （亮度/音量等）每次操作都触发全量擦写，减少 Flash 磨损。
 */
void app_cfg_save(void)
{
    cfg_mark_dirty();
}

/**
 * @brief 立即刷新脏配置到 NVS（同步阻塞）
 *
 * 用于关机 / 配网 / 手动保存等需要确保落盘的场景。
 */
void app_cfg_flush(void)
{
    if (s_dirty) {
        s_dirty = false;
        cfg_write_all_to_nvs();
    }
}

/* ==================== 获取器 API ==================== */

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

/**
 * @brief 设置是否显示秒数
 * 
 * @param show 1=显示, 0=不显示
 */
void app_cfg_set_show_seconds(int show)
{
    g_cfg.show_seconds = show ? 1 : 0;
    cfg_publish(CFG_FIELD_SHOW_SECONDS);
    event_bus_publish(EVENT_CLOCK_TIME_FORMAT_CHANGED, NULL, 0);
    app_cfg_save();
}

int app_cfg_get_show_clock(void) { return g_cfg.show_clock; }

/**
 * @brief 设置是否显示时钟
 *
 * @param show 1=显示, 0=不显示
 */
void app_cfg_set_show_clock(int show)
{
    g_cfg.show_clock = show ? 1 : 0;
    cfg_publish(CFG_FIELD_SHOW_CLOCK);
    event_bus_publish(EVENT_CLOCK_LAYOUT_CHANGED, NULL, 0);
    app_cfg_save();
}

const char *app_cfg_get_clock_text(void) { return g_cfg.clock_text; }

int app_cfg_get_bg_mode(void) { return g_cfg.bg_mode; }
int app_cfg_get_bg_refresh_s(void) { return g_cfg.bg_refresh_s; }
const char *app_cfg_get_bg_url(void) { return g_cfg.bg_url; }

/**
 * @brief 设置背景模式
 * 
 * @param m 背景模式 (0=深色, 1=浅色, 2=图片, 3=纯色)
 */
void app_cfg_set_bg_mode(int m)
{
    if (m < 0) m = 0;
    if (m > 3) m = 3;
    g_cfg.bg_mode = (uint8_t)m;
    cfg_publish(CFG_FIELD_BG_MODE);
    event_bus_publish(EVENT_CLOCK_BG_CHANGED, NULL, 0);
    app_cfg_save();
}

/**
 * @brief 设置背景图片 URL
 *
 * @param url 背景图片 URL
 */
void app_cfg_set_bg_url(const char *url)
{
    if (!url) url = "";
    cfg_lock();
    strncpy(g_cfg.bg_url, url, sizeof(g_cfg.bg_url) - 1);
    g_cfg.bg_url[sizeof(g_cfg.bg_url) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_BG_URL);
    app_cfg_save();
}

uint32_t app_cfg_get_bg_color(void) { return g_cfg.bg_color; }

/**
 * @brief 设置背景纯色
 *
 * @param rgba 背景颜色 (RGBA)
 */
void app_cfg_set_bg_color(uint32_t rgba)
{
    g_cfg.bg_color = rgba ? rgba : 0x202020FFu; /* 默认深灰色 */
    cfg_publish(CFG_FIELD_BG_COLOR);
    if (g_cfg.bg_mode == 3) {
        event_bus_publish(EVENT_CLOCK_BG_CHANGED, NULL, 0);
    }
    app_cfg_save();
}

/**
 * @brief 设置背景刷新间隔
 *
 * @param s 刷新间隔（秒），最大24小时
 */
void app_cfg_set_bg_refresh_s(int s)
{
    if (s < 0) s = 0;
    if (s > 24 * 3600) s = 24 * 3600; /* 最大24小时 */
    g_cfg.bg_refresh_s = (uint16_t)s;
    cfg_publish(CFG_FIELD_BG_REFRESH_S);
    app_cfg_save();
}

/**
 * @brief 重新加载背景
 * 
 * 触发背景变更回调，让 UI 模块重新绘制背景
 */
void app_cfg_clock_bg_reload(void)
{
    event_bus_publish(EVENT_CLOCK_BG_CHANGED, NULL, 0);
}

/**
 * @brief 立即获取背景图片
 * 
 * 如果当前是图片背景模式且有 URL，则触发获取回调
 */
void app_cfg_bg_fetch_now(void)
{
    if (g_cfg.bg_mode != 2 || !g_cfg.bg_url[0]) return;
}

/* ==================== 行情配置 API ==================== */

const char *app_cfg_get_quotes_sym_l(void) { return g_cfg.quotes_sym_l; }
const char *app_cfg_get_quotes_sym_r(void) { return g_cfg.quotes_sym_r; }
int app_cfg_get_quotes_refresh_s(void) { return g_cfg.quotes_refresh_s; }
uint32_t app_cfg_get_quotes_up_rgba(void) { return g_cfg.quotes_up_rgba; }
uint32_t app_cfg_get_quotes_down_rgba(void) { return g_cfg.quotes_down_rgba; }

/**
 * @brief 设置左侧行情符号
 * 
 * @param s 行情符号（如 xauusd）
 */
void app_cfg_set_quotes_sym_l(const char *s)
{
    if (!s) s = "";
    cfg_lock();
    strncpy(g_cfg.quotes_sym_l, s, sizeof(g_cfg.quotes_sym_l) - 1);
    g_cfg.quotes_sym_l[sizeof(g_cfg.quotes_sym_l) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_QUOTES_SYM_L);
    app_cfg_save();
    event_bus_publish(EVENT_QUOTES_CHANGED, NULL, 0);
}

/**
 * @brief 设置右侧行情符号
 * 
 * @param s 行情符号（如 xagusd）
 */
void app_cfg_set_quotes_sym_r(const char *s)
{
    if (!s) s = "";
    cfg_lock();
    strncpy(g_cfg.quotes_sym_r, s, sizeof(g_cfg.quotes_sym_r) - 1);
    g_cfg.quotes_sym_r[sizeof(g_cfg.quotes_sym_r) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_QUOTES_SYM_R);
    app_cfg_save();
    event_bus_publish(EVENT_QUOTES_CHANGED, NULL, 0);
}

/**
 * @brief 设置行情刷新间隔
 * 
 * @param s 刷新间隔（秒），范围5-3600秒
 */
void app_cfg_set_quotes_refresh_s(int s)
{
    if (s < 5) s = 5;      /* 最小5秒 */
    if (s > 3600) s = 3600; /* 最大1小时 */
    g_cfg.quotes_refresh_s = (uint16_t)s;
    cfg_publish(CFG_FIELD_QUOTES_REFRESH_S);
    app_cfg_save();
}

void app_cfg_set_quotes_up_rgba(uint32_t v)
{
    g_cfg.quotes_up_rgba = v;
    cfg_publish(CFG_FIELD_QUOTES_UP_RGBA);
    app_cfg_save();
}

void app_cfg_set_quotes_down_rgba(uint32_t v)
{
    g_cfg.quotes_down_rgba = v;
    cfg_publish(CFG_FIELD_QUOTES_DOWN_RGBA);
    app_cfg_save();
}

/* ==================== 时钟配置 API ==================== */

/**
 * @brief 设置自定义时钟文本
 * 
 * @param s 自定义文本
 */
void app_cfg_set_clock_text(const char *s)
{
    if (!s) s = "";
    cfg_lock();
    strncpy(g_cfg.clock_text, s, sizeof(g_cfg.clock_text) - 1);
    g_cfg.clock_text[sizeof(g_cfg.clock_text) - 1] = 0;
    if (g_cfg.clock_text[0]) g_cfg.show_clock = 1;
    cfg_unlock();
    cfg_publish(CFG_FIELD_CLOCK_TEXT);
    event_bus_publish(EVENT_CLOCK_LAYOUT_CHANGED, NULL, 0);
    app_cfg_save();
}

/**
 * @brief 设置时钟位置偏移
 *
 * @param x 水平偏移
 * @param y 垂直偏移
 */
void app_cfg_set_clock_pos(int x, int y)
{
    if (x < -512) x = -512;
    if (x > 512) x = 512;
    if (y < -256) y = -256;
    if (y > 256) y = 256;
    g_cfg.clock_x = (int16_t)x;
    g_cfg.clock_y = (int16_t)y;
    cfg_publish(CFG_FIELD_CLOCK_POS);
    event_bus_publish(EVENT_CLOCK_LAYOUT_CHANGED, NULL, 0);
    app_cfg_save();
}

/**
 * @brief 设置时钟字号大小
 *
 * @param sz 字号大小 (0-3)
 */
void app_cfg_set_clock_size(int sz)
{
    if (sz < 0) sz = 0;
    if (sz > 3) sz = 3;
    g_cfg.clock_size = (uint8_t)sz;
    cfg_publish(CFG_FIELD_CLOCK_SIZE);
    event_bus_publish(EVENT_CLOCK_LAYOUT_CHANGED, NULL, 0);
    app_cfg_save();
}

/**
 * @brief 设置时钟文字颜色
 *
 * @param rgba 文字颜色 (RGBA)
 */
void app_cfg_set_clock_rgba(uint32_t rgba)
{
    g_cfg.clock_rgba = rgba ? rgba : 0xFFFFFFFFu; /* 默认白色 */
    cfg_publish(CFG_FIELD_CLOCK_RGBA);
    event_bus_publish(EVENT_CLOCK_LAYOUT_CHANGED, NULL, 0);
    app_cfg_save();
}

/**
 * @brief 设置是否显示毫秒
 *
 * @param show 1=显示, 0=不显示
 */
void app_cfg_set_show_ms(int show)
{
    g_cfg.show_ms = show ? 1 : 0;
    cfg_publish(CFG_FIELD_SHOW_MS);
    event_bus_publish(EVENT_CLOCK_LAYOUT_CHANGED, NULL, 0);
    app_cfg_save();
}

/**
 * @brief 设置语言索引
 * 
 * @param lang 语言索引
 */
void app_cfg_set_lang(int lang)
{
    if (lang < 0) lang = 0;
    if (lang >= I18N_LANG_COUNT) lang = 0; /* 超出范围时使用默认语言 */
    g_cfg.lang = (uint8_t)lang;
    cfg_publish(CFG_FIELD_LANG);
    app_cfg_save();
}

/**
 * @brief 设置背光亮度
 *
 * @param v 亮度值 (0-255)
 */
void app_cfg_set_brightness(int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_cfg.brightness = (uint8_t)v;
    cfg_publish(CFG_FIELD_BRIGHTNESS);
    event_bus_publish(EVENT_BACKLIGHT_CHANGED, &g_cfg.brightness, sizeof(g_cfg.brightness));
    app_cfg_save();
}

/**
 * @brief 设置自动变暗和关闭时间
 *
 * @param dim_s 自动变暗延迟时间（秒）
 * @param off_s 自动关闭延迟时间（秒）
 */
void app_cfg_set_dim_off(int dim_s, int off_s)
{
    if (dim_s < 0) dim_s = 0;
    if (off_s < 0) off_s = 0;
    g_cfg.dim_s = (uint16_t)dim_s;
    g_cfg.off_s = (uint16_t)off_s;
    cfg_publish(CFG_FIELD_DIM_S);
    cfg_publish(CFG_FIELD_OFF_S);
    app_cfg_save();
}

/**
 * @brief 设置当前活动的 Tile 索引
 *
 * 通过发布 EVENT_TILE_CHANGED 事件通知 UI 层切换，
 * 解耦 config 层与 LVGL/UI 层。
 *
 * @param idx Tile 索引 (0-5)
 */
void app_cfg_set_active_tile(int idx)
{
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    event_bus_publish(EVENT_TILE_CHANGED, &idx, sizeof(idx));
}

/* ==================== 新增 setter ==================== */

void app_cfg_set_tz_idx(int idx)
{
    if (idx < 0) idx = 0;
    g_cfg.tz_idx = (uint16_t)idx;
    cfg_publish(CFG_FIELD_TZ_IDX);
    event_bus_publish(EVENT_CLOCK_TIME_FORMAT_CHANGED, NULL, 0);
    app_cfg_save();
}

void app_cfg_set_hour24(int enable)
{
    g_cfg.hour24 = enable ? 1 : 0;
    cfg_publish(CFG_FIELD_HOUR24);
    event_bus_publish(EVENT_CLOCK_TIME_FORMAT_CHANGED, NULL, 0);
    app_cfg_save();
}

void app_cfg_set_date_fmt(int fmt)
{
    if (fmt < 0) fmt = 0;
    if (fmt > DATE_FMT_MAX) fmt = DATE_FMT_MAX;
    g_cfg.date_fmt = (uint8_t)fmt;
    cfg_publish(CFG_FIELD_DATE_FMT);
    event_bus_publish(EVENT_CLOCK_TIME_FORMAT_CHANGED, NULL, 0);
    app_cfg_save();
}

void app_cfg_set_show_fps(int show)
{
    g_cfg.show_fps = show ? 1 : 0;
    cfg_publish(CFG_FIELD_SHOW_FPS);
    uint8_t v = g_cfg.show_fps;
    event_bus_publish(EVENT_SHOW_FPS_CHANGED, &v, sizeof(v));
    app_cfg_save();
}

void app_cfg_set_audio_enable(int enable)
{
    g_cfg.audio_enable = enable ? 1 : 0;
    cfg_publish(CFG_FIELD_AUDIO_ENABLE);
    app_cfg_save();
}

void app_cfg_set_audio_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_cfg.audio_volume = (uint8_t)vol;
    cfg_publish(CFG_FIELD_AUDIO_VOLUME);
    event_bus_publish(EVENT_AUDIO_VOLUME_CHANGED, NULL, 0);
    app_cfg_save();
}

void app_cfg_set_theme(int theme)
{
    if (theme < 0) theme = 0;
    g_cfg.theme = (uint8_t)theme;
    cfg_publish(CFG_FIELD_THEME);
    app_cfg_save();
}

void app_cfg_set_wifi_autoconnect(int enable)
{
    g_cfg.wifi_autoconnect = enable ? 1 : 0;
    cfg_publish(CFG_FIELD_WIFI_AUTOCONNECT);
    app_cfg_save();
}


void app_cfg_set_last_ssid(const char *ssid)
{
    if (!ssid) return;
    cfg_lock();
    if (strcmp(g_cfg.last_ssid, ssid) == 0) {
        cfg_unlock();
        return;
    }
    size_t len = strlen(ssid);
    if (len >= sizeof(g_cfg.last_ssid)) len = sizeof(g_cfg.last_ssid) - 1;
    memcpy(g_cfg.last_ssid, ssid, len);
    g_cfg.last_ssid[len] = '\0';
    cfg_unlock();
    cfg_publish(CFG_FIELD_LAST_SSID);
    app_cfg_flush();   /* 配网凭证：立即落盘，防断电丢失 */
}

size_t app_cfg_get_last_ssid(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return 0;
    cfg_lock();
    size_t len = strlen(g_cfg.last_ssid);
    if (len >= buf_len) len = buf_len - 1;
    memcpy(buf, g_cfg.last_ssid, len);
    buf[len] = '\0';
    cfg_unlock();
    return len;
}

/* ==================== v8: 老 config 系统合并字段 API ==================== */

const char *app_cfg_get_nas_type(void) { return g_cfg.nas_type; }
const char *app_cfg_get_nas_ip(void)    { return g_cfg.nas_ip; }
int         app_cfg_get_nas_port(void)  { return g_cfg.nas_port; }
const char *app_cfg_get_nas_user(void)  { return g_cfg.nas_user; }
const char *app_cfg_get_nas_pass(void)  { return g_cfg.nas_pass; }
int         app_cfg_get_nas_https(void) { return g_cfg.nas_https; }
const char *app_cfg_get_snmp_comm(void) { return g_cfg.snmp_comm; }
int         app_cfg_get_snmp_ver(void)  { return g_cfg.snmp_ver; }
int         app_cfg_get_serial_baud(void){ return (int)g_cfg.serial_baud; }
int         app_cfg_get_poll_sec(void)  { return g_cfg.poll_sec; }
int         app_cfg_get_rotation_angle(void) { return g_cfg.rotation_angle; }
int         app_cfg_get_autodim(void)   { return g_cfg.autodim; }
int         app_cfg_get_timezone(void)  { return g_cfg.timezone; }
int         app_cfg_get_sata_disk_count(void) { return g_cfg.sata_disk_count; }
int         app_cfg_get_m2_disk_count(void)   { return g_cfg.m2_disk_count; }
const char *app_cfg_get_weather_api_key(void) { return g_cfg.weather_api_key; }
const char *app_cfg_get_weather_city(void)    { return g_cfg.weather_city; }
int         app_cfg_get_auto_cycle_enabled(void) { return g_cfg.auto_cycle_enabled; }
int         app_cfg_get_auto_cycle_interval_sec(void) { return g_cfg.auto_cycle_interval_sec; }
FanConfig   app_cfg_get_fan(void)       { return g_cfg.fan; }

void app_cfg_set_nas_type(const char *v)
{
    if (!v) v = "";
    cfg_lock();
    strncpy(g_cfg.nas_type, v, sizeof(g_cfg.nas_type) - 1);
    g_cfg.nas_type[sizeof(g_cfg.nas_type) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_NAS_TYPE);
    app_cfg_save();
}
void app_cfg_set_nas_ip(const char *v)
{
    if (!v) v = "";
    cfg_lock();
    strncpy(g_cfg.nas_ip, v, sizeof(g_cfg.nas_ip) - 1);
    g_cfg.nas_ip[sizeof(g_cfg.nas_ip) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_NAS_IP);
    app_cfg_save();
}
void app_cfg_set_nas_port(int v)
{
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    g_cfg.nas_port = (uint16_t)v;
    cfg_publish(CFG_FIELD_NAS_PORT);
    app_cfg_save();
}
void app_cfg_set_nas_user(const char *v)
{
    if (!v) v = "";
    cfg_lock();
    strncpy(g_cfg.nas_user, v, sizeof(g_cfg.nas_user) - 1);
    g_cfg.nas_user[sizeof(g_cfg.nas_user) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_NAS_USER);
    app_cfg_save();
}
void app_cfg_set_nas_pass(const char *v)
{
    if (!v) v = "";
    cfg_lock();
    strncpy(g_cfg.nas_pass, v, sizeof(g_cfg.nas_pass) - 1);
    g_cfg.nas_pass[sizeof(g_cfg.nas_pass) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_NAS_PASS);
    app_cfg_save();
}
void app_cfg_set_nas_https(int v)
{
    g_cfg.nas_https = v ? 1 : 0;
    cfg_publish(CFG_FIELD_NAS_HTTPS);
    app_cfg_save();
}
void app_cfg_set_snmp_comm(const char *v)
{
    if (!v) v = "";
    cfg_lock();
    strncpy(g_cfg.snmp_comm, v, sizeof(g_cfg.snmp_comm) - 1);
    g_cfg.snmp_comm[sizeof(g_cfg.snmp_comm) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_SNMP_COMM);
    app_cfg_save();
}
void app_cfg_set_snmp_ver(int v)
{
    g_cfg.snmp_ver = v ? 1 : 0;
    cfg_publish(CFG_FIELD_SNMP_VER);
    app_cfg_save();
}
void app_cfg_set_serial_baud(int v)
{
    if (v < 0) v = 0;
    g_cfg.serial_baud = (uint32_t)v;
    cfg_publish(CFG_FIELD_SERIAL_BAUD);
    app_cfg_save();
}
void app_cfg_set_poll_sec(int v)
{
    if (v < 1) v = 1;
    if (v > 255) v = 255;
    g_cfg.poll_sec = (uint8_t)v;
    cfg_publish(CFG_FIELD_POLL_SEC);
    app_cfg_save();
}
void app_cfg_set_rotation_angle(int v)
{
    if (v < 0) v = 0;
    if (v > 270) v = 0;
    g_cfg.rotation_angle = (uint8_t)(v / 90) * 90;
    cfg_publish(CFG_FIELD_ROTATION_ANGLE);
    app_cfg_save();
}
void app_cfg_set_autodim(int v)
{
    g_cfg.autodim = v ? 1 : 0;
    cfg_publish(CFG_FIELD_AUTODIM);
    app_cfg_save();
}
void app_cfg_set_timezone(int v)
{
    if (v < -14) v = -14;
    if (v > 14) v = 14;
    g_cfg.timezone = (int8_t)v;
    cfg_publish(CFG_FIELD_TIMEZONE);
    app_cfg_save();
}
void app_cfg_set_sata_disk_count(int v)
{
    if (v < 0) v = 0;
    if (v > 16) v = 16;
    g_cfg.sata_disk_count = (uint8_t)v;
    cfg_publish(CFG_FIELD_SATA_DISK_COUNT);
    app_cfg_save();
}
void app_cfg_set_m2_disk_count(int v)
{
    if (v < 0) v = 0;
    if (v > 16) v = 16;
    g_cfg.m2_disk_count = (uint8_t)v;
    cfg_publish(CFG_FIELD_M2_DISK_COUNT);
    app_cfg_save();
}
void app_cfg_set_weather_api_key(const char *v)
{
    if (!v) v = "";
    cfg_lock();
    strncpy(g_cfg.weather_api_key, v, sizeof(g_cfg.weather_api_key) - 1);
    g_cfg.weather_api_key[sizeof(g_cfg.weather_api_key) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_WEATHER_API_KEY);
    app_cfg_save();
}
void app_cfg_set_weather_city(const char *v)
{
    if (!v) v = "";
    cfg_lock();
    strncpy(g_cfg.weather_city, v, sizeof(g_cfg.weather_city) - 1);
    g_cfg.weather_city[sizeof(g_cfg.weather_city) - 1] = 0;
    cfg_unlock();
    cfg_publish(CFG_FIELD_WEATHER_CITY);
    app_cfg_save();
}
void app_cfg_set_auto_cycle_enabled(int v)
{
    g_cfg.auto_cycle_enabled = v ? 1 : 0;
    cfg_publish(CFG_FIELD_AUTO_CYCLE_EN);
    app_cfg_save();
}
void app_cfg_set_auto_cycle_interval_sec(int v)
{
    if (v < 1) v = 1;
    if (v > 255) v = 255;
    g_cfg.auto_cycle_interval_sec = (uint8_t)v;
    cfg_publish(CFG_FIELD_AUTO_CYCLE_INT);
    app_cfg_save();
}
void app_cfg_set_fan(const FanConfig *v)
{
    if (!v) return;
    cfg_lock();
    memcpy(&g_cfg.fan, v, sizeof(FanConfig));
    cfg_unlock();
    cfg_publish(CFG_FIELD_FAN);
    event_bus_publish(EVENT_FAN_CONFIG_CHANGED, &g_cfg.fan, sizeof(g_cfg.fan));
    app_cfg_save();
}

