/* sim/stubs/stubs.c
 * PC 模拟器所有 ESP-IDF/外部模块函数的集中 stub 实现。
 * 编译为静态库 sim_stubs，链接到 sim_overrides 和最终可执行文件。
 *
 * 实现原则：
 *  - WiFi 系列返回假网络数据（已连接状态 + 3 个假扫描结果）
 *  - SD 卡相关：sdcard_is_mounted() 返回 false
 *  - LCD 背光：setUpduty() 为 no-op
 *  - strlcpy/strlcat：MinGW 缺失，提供 BSD 兼容实现
 *  - ESP-IDF 时间的 esp_timer_get_time/esp_log_timestamp 已在头文件 inline 实现
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ESP-IDF 头（stub 版本，所有 static inline 已就绪） */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"

/* 项目依赖的外部组件头（来自 managed_components，需要 mock 实现） */
#include "esp_wifi_config.h"
#include "esp_http_server.h"

/* BSP 头 */
#include "bsp/lcd_bl_pwm_bsp.h"
#include "bsp/sdcard_bsp.h"

/* 项目数据客户端头（需要 mock create 函数） */
#include "data_source.h"
#include "client/api_client.h"
#include "client/synology_client.h"
#include "client/netdata_client.h"
#include "client/truenas_client.h"
#include "client/qnap_client.h"
#include "client/serial_client.h"
#include "client/snmp_client.h"

/* ============================================================
 * BSD 字符串兼容：strlcpy / strlcat
 * MinGW 标准库不提供，项目代码假定 <string.h> 含此二者。
 * ============================================================ */
size_t strlcpy(char *dst, const char *src, size_t siz)
{
    if (!dst || !src) return 0;
    size_t srclen = strlen(src);
    if (siz != 0) {
        size_t n = (srclen >= siz) ? (siz - 1) : srclen;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return srclen;
}

size_t strlcat(char *dst, const char *src, size_t siz)
{
    if (!dst || !src) return 0;
    size_t dstlen = strlen(dst);
    size_t srclen = strlen(src);
    if (dstlen >= siz) return siz + srclen;
    if (srclen == 0) return dstlen;
    size_t remaining = siz - dstlen - 1;
    size_t n = (srclen > remaining) ? remaining : srclen;
    memcpy(dst + dstlen, src, n);
    dst[dstlen + n] = '\0';
    return dstlen + srclen;
}

/* ============================================================
 * POSIX setenv / unsetenv
 * MinGW stdlib 不提供，基于 CRT _putenv_s 实现。
 * _putenv_s 会同时更新 CRT environ 副本和进程环境块，
 * 因此后续 getenv() 和 tzset() 均可见。
 * ============================================================ */
int setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !*name || strchr(name, '=')) return -1;
    if (!overwrite && getenv(name) != NULL) return 0;
    errno_t err = _putenv_s(name, value ? value : "");
    return err == 0 ? 0 : -1;
}

int unsetenv(const char *name)
{
    if (!name || !*name || strchr(name, '=')) return -1;
    /* _putenv_s 设空字符串会从 environ 中移除该变量 */
    errno_t err = _putenv_s(name, "");
    return err == 0 ? 0 : -1;
}

/* ============================================================
 * BSP: LCD 背光 PWM（ui_helpers.c 调用 setUpduty）
 * ============================================================ */
void lcd_bl_pwm_bsp_init(uint16_t duty) { (void)duty; }
void setUpduty(uint16_t duty) { (void)duty; }

/* ============================================================
 * BSP: SD 卡（仅声明，无人调用，但需提供符号）
 * ============================================================ */
sdcard_bsp_t user_sdcard_bsp = { 0.0f };
volatile bool sdcard_busy = false;

void _sdcard_init(void) {}
esp_err_t sdcard_file_write(const char *path, const char *data) {
    (void)path; (void)data; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t sdcard_file_read(const char *path, char *buffer, size_t *out_len) {
    (void)path; (void)buffer; if (out_len) *out_len = 0; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t sdcard_format(void) { return ESP_ERR_NOT_SUPPORTED; }
bool sdcard_is_mounted(void) { return false; }

/* ============================================================
 * WiFi 配置（esp_wifi_config.h 全部 API 的 mock）
 * ============================================================ */
static wifi_status_t s_wifi_status = {
    .state = WIFI_STATE_CONNECTED,
    .ssid = "ZotLab-Mock-AP",
    .rssi = -52,
    .quality = 78,
    .channel = 6,
    .ip = "192.168.1.100",
    .netmask = "255.255.255.0",
    .gateway = "192.168.1.1",
    .dns = "8.8.8.8",
    .mac = "AA:BB:CC:DD:EE:FF",
    .hostname = "nas-monitor-sim",
    .uptime_ms = 0,
    .ap_active = false,
};

static wifi_ap_status_t s_wifi_ap_status = {0};

esp_err_t wifi_cfg_init(const wifi_cfg_config_t *config) {
    (void)config;
    ESP_LOGI("wifi_cfg", "sim init (mock connected to %s)", s_wifi_status.ssid);
    return ESP_OK;
}

esp_err_t wifi_cfg_deinit(bool deinit_wifi) {
    (void)deinit_wifi;
    return ESP_OK;
}

bool wifi_cfg_is_connected(void) {
    return s_wifi_status.state == WIFI_STATE_CONNECTED;
}

wifi_state_t wifi_cfg_get_state(void) {
    return s_wifi_status.state;
}

esp_err_t wifi_cfg_wait_connected(uint32_t timeout_ms) {
    (void)timeout_ms;
    return wifi_cfg_is_connected() ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_cfg_get_status(wifi_status_t *status) {
    if (!status) return ESP_ERR_INVALID_ARG;
    /* 累加 uptime 让 UI 显示连接时间在变化 */
    s_wifi_status.uptime_ms += 5000;
    *status = s_wifi_status;
    return ESP_OK;
}

httpd_handle_t wifi_cfg_get_httpd(void) { return NULL; }
esp_err_t wifi_cfg_stop_http(void) { return ESP_OK; }

esp_err_t wifi_cfg_add_network(const wifi_network_t *network) {
    (void)network; return ESP_OK;
}
esp_err_t wifi_cfg_update_network(const wifi_network_t *network) {
    (void)network; return ESP_OK;
}
esp_err_t wifi_cfg_remove_network(const char *ssid) {
    (void)ssid; return ESP_OK;
}
esp_err_t wifi_cfg_get_network(const char *ssid, wifi_network_t *network) {
    (void)ssid; (void)network; return ESP_ERR_NOT_FOUND;
}
esp_err_t wifi_cfg_list_networks(wifi_network_t *networks, size_t max_count, size_t *count) {
    if (!networks || !count) return ESP_ERR_INVALID_ARG;
    /* 提供两个假网络让 UI 显示已保存列表 */
    size_t n = max_count < 2 ? max_count : 2;
    if (n >= 1) {
        memset(&networks[0], 0, sizeof(wifi_network_t));
        strncpy(networks[0].ssid, "ZotLab-Mock-AP", sizeof(networks[0].ssid) - 1);
        strncpy(networks[0].password, "12345678", sizeof(networks[0].password) - 1);
        networks[0].priority = 10;
    }
    if (n >= 2) {
        memset(&networks[1], 0, sizeof(wifi_network_t));
        strncpy(networks[1].ssid, "Home-WiFi", sizeof(networks[1].ssid) - 1);
        strncpy(networks[1].password, "password", sizeof(networks[1].password) - 1);
        networks[1].priority = 5;
    }
    *count = n;
    return ESP_OK;
}

esp_err_t wifi_cfg_set_var(const char *key, const char *value) {
    (void)key; (void)value; return ESP_OK;
}
esp_err_t wifi_cfg_get_var(const char *key, char *value, size_t max_len) {
    (void)key; if (value && max_len) value[0] = '\0'; return ESP_ERR_NOT_FOUND;
}
esp_err_t wifi_cfg_del_var(const char *key) { (void)key; return ESP_OK; }

esp_err_t wifi_cfg_start_ap(const wifi_cfg_ap_config_t *config) {
    (void)config;
    s_wifi_ap_status.active = true;
    strncpy(s_wifi_ap_status.ssid, "NAS-Monitor-AP", sizeof(s_wifi_ap_status.ssid) - 1);
    strncpy(s_wifi_ap_status.ip, "192.168.4.1", sizeof(s_wifi_ap_status.ip) - 1);
    return ESP_OK;
}
esp_err_t wifi_cfg_stop_ap(void) {
    s_wifi_ap_status.active = false;
    return ESP_OK;
}
esp_err_t wifi_cfg_get_ap_status(wifi_ap_status_t *status) {
    if (!status) return ESP_ERR_INVALID_ARG;
    *status = s_wifi_ap_status;
    return ESP_OK;
}
esp_err_t wifi_cfg_set_ap_config(const wifi_cfg_ap_config_t *config) {
    (void)config; return ESP_OK;
}
esp_err_t wifi_cfg_get_ap_config(wifi_cfg_ap_config_t *config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    return ESP_OK;
}

esp_err_t wifi_cfg_connect(const char *ssid) {
    ESP_LOGI("wifi_cfg", "sim connect to %s", ssid ? ssid : "(null)");
    if (ssid) {
        strncpy(s_wifi_status.ssid, ssid, sizeof(s_wifi_status.ssid) - 1);
        s_wifi_status.ssid[sizeof(s_wifi_status.ssid) - 1] = '\0';
    }
    s_wifi_status.state = WIFI_STATE_CONNECTED;
    return ESP_OK;
}
esp_err_t wifi_cfg_disconnect(void) {
    s_wifi_status.state = WIFI_STATE_DISCONNECTED;
    return ESP_OK;
}

esp_err_t wifi_cfg_scan(wifi_scan_result_t *results, size_t max_count, size_t *count) {
    if (!results || !count || max_count == 0) {
        if (count) *count = 0;
        return ESP_OK;
    }
    /* 提供 5 个假扫描结果 */
    static const struct {
        const char *ssid;
        int8_t rssi;
        wifi_auth_mode_t auth;
    } fake[] = {
        { "ZotLab-Mock-AP",   -52, WIFI_AUTH_WPA2_PSK },
        { "Home-WiFi",        -65, WIFI_AUTH_WPA2_PSK },
        { "Neighbor-5G",      -78, WIFI_AUTH_WPA3_PSK },
        { "TP-LINK_Guest",    -82, WIFI_AUTH_OPEN     },
        { "ChinaNet-1234",    -88, WIFI_AUTH_WPA_WPA2_PSK },
    };
    size_t n = max_count < 5 ? max_count : 5;
    for (size_t i = 0; i < n; i++) {
        memset(&results[i], 0, sizeof(wifi_scan_result_t));
        strncpy(results[i].ssid, fake[i].ssid, sizeof(results[i].ssid) - 1);
        results[i].rssi = fake[i].rssi;
        results[i].auth = fake[i].auth;
    }
    *count = n;
    return ESP_OK;
}

esp_err_t wifi_cfg_factory_reset(void) {
    ESP_LOGW("wifi_cfg", "sim factory_reset (no-op)");
    return ESP_OK;
}

/* ============================================================
 * 非 mock 客户端的 create 函数：返回 NULL，让 data_source 走 mock 路径
 * ============================================================ */
DataSource *api_client_create(NasType type) { (void)type; return NULL; }
DataSource *synology_client_create(void) { return NULL; }
DataSource *netdata_client_create(void) { return NULL; }
DataSource *truenas_client_create(void) { return NULL; }
DataSource *qnap_client_create(void) { return NULL; }
DataSource *serial_client_create(void) { return NULL; }
DataSource *snmp_client_create(void) { return NULL; }
DataSource *unraid_client_create(void) { return NULL; }
