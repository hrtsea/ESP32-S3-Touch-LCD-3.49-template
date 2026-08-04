/* sim/stubs/esp_log.h - ESP-IDF 日志 stub
 *
 * 注意：绝不能 #include <windows.h>，因为其 interface 宏会
 * 与 nas_data.h 中的 char interface[16] 字段冲突。
 * 改用前向声明调用 Win32 API。
 */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

#ifndef ESP_LOG_TAG
#define ESP_LOG_TAG "sim"
#endif

static inline void _sim_log(const char *level, const char *tag, const char *fmt, ...) {
    (void)level; (void)tag;
    va_list args; va_start(args, fmt);
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

#define ESP_LOGE(tag, fmt, ...) do { fprintf(stderr, "E [%s] " fmt "\n", tag ? tag : "?", ##__VA_ARGS__); fflush(stderr); } while(0)
#define ESP_LOGW(tag, fmt, ...) do { fprintf(stderr, "W [%s] " fmt "\n", tag ? tag : "?", ##__VA_ARGS__); fflush(stderr); } while(0)
#define ESP_LOGI(tag, fmt, ...) do { fprintf(stderr, "I [%s] " fmt "\n", tag ? tag : "?", ##__VA_ARGS__); fflush(stderr); } while(0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)

#define ESP_LOG_LEVEL_LOCAL(level, tag, fmt, ...) ((void)0)

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

static inline void esp_log_level_set(const char *tag, esp_log_level_t level) { (void)tag; (void)level; }

#ifdef _WIN32
/* 前向声明 GetTickCount，避免 #include <windows.h> 的 interface 宏污染 */
__declspec(dllimport) unsigned long __stdcall GetTickCount(void);
#endif

/* 返回系统启动以来的毫秒数（模拟器环境用实际系统时间） */
static inline uint32_t esp_log_timestamp(void) {
#ifdef _WIN32
    return (uint32_t)GetTickCount();
#else
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
#endif
}
