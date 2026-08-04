/* sim/overrides/http_timer.c - HTTP 轮询计时器简化版
 * 原版基于 esp_timer 定时触发 EVENT_TRIGGER_HTTP_FETCH。
 * PC 模拟器由 main.c 主循环直接驱动 mock 数据轮询，无需定时器。
 */
#include "http_timer.h"
#include <stdint.h>
#include <stdbool.h>

static uint32_t s_interval_ms = 5000;
static bool s_running = false;

void http_timer_init(void) {
    s_interval_ms = 5000;
    s_running = false;
}

void http_timer_start(void) { s_running = true; }
void http_timer_stop(void) { s_running = false; }
void http_timer_set_interval_ms(uint32_t ms) { s_interval_ms = ms; }
uint32_t http_timer_get_interval_ms(void) { return s_interval_ms; }
bool http_timer_is_running(void) { return s_running; }
