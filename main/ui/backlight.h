#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern uint32_t g_last_activity_ms;
extern int      g_dim_state;

void backlight_apply(uint8_t bri);

#ifdef __cplusplus
}
#endif

#endif