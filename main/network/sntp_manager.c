#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "i2c_equipment.h"

static const char *TAG = "sntp_manager";

/**
 * @brief SNTP 同步间隔（毫秒）
 * 
 * 设置为 4 小时，避免过于频繁的网络请求，同时保证时间精度。
 * 间隔过长可能导致 RTC 漂移累积，过短则浪费网络带宽。
 */
#define SNTP_SYNC_INTERVAL_MS (4UL * 3600UL * 1000UL)

/**
 * @brief SNTP 是否已启动标志
 * 
 * 防止重复初始化 SNTP 服务。
 */
static bool g_sntp_started = false;

/**
 * @brief 上次 SNTP 同步成功的时间戳
 * 
 * 记录最后一次成功从 NTP 服务器获取时间的时刻，用于调试和监控。
 */
static time_t g_last_sntp_sync = 0;

/**
 * @brief SNTP 时间同步回调函数
 * 
 * 当 SNTP 成功获取到网络时间后，ESP-IDF 会调用此函数。
 * 主要职责：
 * 1. 记录同步时间戳
 * 2. 打印同步日志
 * 3. 将网络时间回写到 RTC 硬件，确保断电后时间不丢失
 * 
 * @param tv 同步后的时间值，包含秒和微秒
 */
static void sntp_sync_notification_cb(struct timeval *tv)
{
    if (!tv) return;

    g_last_sntp_sync = tv->tv_sec;

    struct tm tm;
    gmtime_r(&g_last_sntp_sync, &tm);

    ESP_LOGI(TAG, "sntp: 已同步到 %04d-%02d-%02d %02d:%02d:%02d UTC",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    i2c_rtc_setTime((uint16_t)(tm.tm_year + 1900),
                    (uint8_t)(tm.tm_mon + 1),
                    (uint8_t)tm.tm_mday,
                    (uint8_t)tm.tm_hour,
                    (uint8_t)tm.tm_min,
                    (uint8_t)tm.tm_sec);
}

/**
 * @brief 启动 SNTP 时间同步服务
 * 
 * 初始化并启动 ESP-IDF 的 SNTP 客户端，配置如下：
 * - 工作模式：轮询模式（SNTP_OPMODE_POLL）
 * - NTP 服务器：pool.ntp.org 和 time.google.com（双服务器提高可靠性）
 * - 同步间隔：4 小时
 * - 同步回调：sntp_sync_notification_cb（用于回写 RTC）
 * 
 * 注意：此函数应在网络连接建立后调用，否则无法获取时间。
 * 多次调用不会重复初始化（通过 g_sntp_started 标志保护）。
 */
void sntp_manager_start(void)
{
    if (g_sntp_started) return;
    g_sntp_started = true;

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");

    sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);

    sntp_set_sync_interval(SNTP_SYNC_INTERVAL_MS);

    esp_sntp_init();

    ESP_LOGI(TAG, "sntp: 已启动, 服务器=pool.ntp.org, 同步间隔=%lus",
             (unsigned long)(SNTP_SYNC_INTERVAL_MS / 1000));
}