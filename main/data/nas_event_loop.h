#ifndef NAS_EVENT_LOOP_H
#define NAS_EVENT_LOOP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

void nas_event_loop_start(void);
void nas_event_loop_stop(void);
bool nas_event_loop_is_running(void);
bool nas_event_loop_switch_source(const char *nas_type_id);

/* NAS 数据拉取周期定时器（原 http_timer，已并入本模块）。
 * 以固定间隔发布 EVENT_TRIGGER_HTTP_FETCH，由 task_nas_data_loop 消费。 */
void nas_event_loop_timer_init(void);
void nas_event_loop_timer_start(void);
void nas_event_loop_timer_stop(void);
void nas_event_loop_timer_set_interval_ms(uint32_t ms);
uint32_t nas_event_loop_timer_get_interval_ms(void);
bool nas_event_loop_timer_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
