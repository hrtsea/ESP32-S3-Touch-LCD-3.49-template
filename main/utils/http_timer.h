#ifndef HTTP_TIMER_H
#define HTTP_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

void http_timer_init(void);
void http_timer_start(void);
void http_timer_stop(void);
void http_timer_set_interval_ms(uint32_t ms);
uint32_t http_timer_get_interval_ms(void);
bool http_timer_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
