#include "ui_helpers.h"

#include <string.h>
#include "lcd_bl_pwm_bsp.h"
#include "event_bus.h"
#include "app_cfg.h"

static lv_obj_t *s_tileview = NULL;
static char s_status_text[256] = {0};
static uint32_t s_last_activity_ms = 0;
static int s_dim_state = 0;
static uint32_t s_last_scroll_ms = 0;
static uint32_t s_menu_input_block_until_ms = 0;

lv_obj_t *ui_helpers_get_tileview(void)
{
    return s_tileview;
}

void ui_helpers_set_tileview(lv_obj_t *tv)
{
    s_tileview = tv;
}

void ui_helpers_get_status_text(char *buf, size_t buf_len)
{
    if (buf && buf_len > 0) {
        strncpy(buf, s_status_text, buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
}

void ui_helpers_set_status_text(const char *text)
{
    if (text) {
        strncpy(s_status_text, text, sizeof(s_status_text) - 1);
        s_status_text[sizeof(s_status_text) - 1] = '\0';
    }
}

uint32_t ui_helpers_get_last_activity_ms(void)
{
    return s_last_activity_ms;
}

void ui_helpers_set_last_activity_ms(uint32_t t)
{
    s_last_activity_ms = t;
}

void ui_helpers_notify_activity(void)
{
    s_last_activity_ms = lv_tick_get();
    event_bus_publish(EVENT_USER_ACTIVITY, NULL, 0);
    if (s_dim_state != 0) {
        s_dim_state = 0;
        ui_helpers_backlight_apply(g_cfg.brightness);
    }
}

int ui_helpers_get_dim_state(void)
{
    return s_dim_state;
}

void ui_helpers_set_dim_state(int state)
{
    if (s_dim_state != state) {
        s_dim_state = state;
        int s = state;
        event_bus_publish(EVENT_BACKLIGHT_CHANGED, &s, sizeof(s));
    }
}

uint32_t ui_helpers_get_last_scroll_ms(void)
{
    return s_last_scroll_ms;
}

void ui_helpers_set_last_scroll_ms(uint32_t t)
{
    s_last_scroll_ms = t;
}

uint32_t ui_helpers_get_menu_block_until_ms(void)
{
    return s_menu_input_block_until_ms;
}

void ui_helpers_set_menu_block_until_ms(uint32_t t)
{
    s_menu_input_block_until_ms = t;
}

bool ui_helpers_menu_input_blocked(void)
{
    return lv_tick_get() < s_menu_input_block_until_ms;
}

void ui_helpers_backlight_apply(uint8_t bri)
{
    setUpduty((uint16_t)(0xFF - bri));
}
