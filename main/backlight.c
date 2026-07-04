#include <stdint.h>
#include "lcd_bl_pwm_bsp.h"

uint32_t g_last_activity_ms = 0;
int      g_dim_state = 0;

void backlight_apply(uint8_t bri)
{
    setUpduty((uint16_t)(0xFF - bri));
}