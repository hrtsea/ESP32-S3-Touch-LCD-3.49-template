/* sim/overrides/nas_event_loop.c - NAS 事件循环 no-op
 *
 * 原版创建 FreeRTOS 任务消费 event_bus 队列。PC 单线程模拟器
 * 中由 main.c 主动调用 data_source_poll + event_bus_publish_nas_data，
 * 故此处仅保留 no-op 实现。
 */
#include "nas_event_loop.h"

void nas_event_loop_start(void) {}
void nas_event_loop_stop(void) {}
bool nas_event_loop_is_running(void) { return false; }
bool nas_event_loop_switch_source(const char *nas_type_id) {
    (void)nas_type_id;
    return false;
}

/* 拉取定时器 no-op：PC 模拟器由 main.c 主循环按 poll_sec 直接驱动 mock 轮询 */
void nas_event_loop_timer_init(void) {}
void nas_event_loop_timer_start(void) {}
void nas_event_loop_timer_stop(void) {}
void nas_event_loop_timer_set_interval_ms(uint32_t ms) { (void)ms; }
uint32_t nas_event_loop_timer_get_interval_ms(void) { return 0; }
bool nas_event_loop_timer_is_running(void) { return false; }
