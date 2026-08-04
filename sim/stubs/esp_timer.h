/* sim/stubs/esp_timer.h - ESP-IDF 定时器 stub */
#pragma once
#include <stdint.h>
#include <time.h>

typedef void *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);

typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    const char *name;
} esp_timer_create_args_t;

static inline int64_t esp_timer_get_time(void) {
    return (int64_t)clock() * 1000000 / CLOCKS_PER_SEC;
}
static inline uint32_t esp_timer_get_time_ms(void) {
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}

static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *h) {
    (void)args; if (h) *h = (esp_timer_handle_t)1; return ESP_OK;
}
static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t us) { (void)h; (void)us; return ESP_OK; }
static inline esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us) { (void)h; (void)us; return ESP_OK; }
static inline esp_err_t esp_timer_stop(esp_timer_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t esp_timer_delete(esp_timer_handle_t h) { (void)h; return ESP_OK; }
static inline uint64_t esp_timer_get_next_alarm(void) { return 0; }

#include "esp_err.h"
