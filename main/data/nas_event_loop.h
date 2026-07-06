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

#ifdef __cplusplus
}
#endif

#endif
