#ifndef NAS_EVENT_LOOP_H
#define NAS_EVENT_LOOP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

void nas_event_loop_start(const char* type_id);
void nas_event_loop_stop(void);
bool nas_event_loop_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
