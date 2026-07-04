#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "i2c_equipment.h"

static const char *TAG = "sntp_manager";

#define SNTP_SYNC_INTERVAL_MS (4UL * 3600UL * 1000UL)

static bool g_sntp_started = false;
static time_t g_last_sntp_sync = 0;

static void sntp_sync_notification_cb(struct timeval *tv)
{
    if (!tv) return;
    g_last_sntp_sync = tv->tv_sec;
    struct tm tm;
    gmtime_r(&g_last_sntp_sync, &tm);
    ESP_LOGI(TAG, "sntp: synced to %04d-%02d-%02d %02d:%02d:%02d UTC",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    i2c_rtc_setTime((uint16_t)(tm.tm_year + 1900),
                    (uint8_t)(tm.tm_mon + 1),
                    (uint8_t)tm.tm_mday,
                    (uint8_t)tm.tm_hour,
                    (uint8_t)tm.tm_min,
                    (uint8_t)tm.tm_sec);
}

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
    ESP_LOGI(TAG, "sntp: started, server=pool.ntp.org, interval=%lus",
             (unsigned long)(SNTP_SYNC_INTERVAL_MS / 1000));
}