#include "ui_state.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "disp_driver.h"

extern lv_obj_t *g_tileview;
extern char g_status_text[256];
extern uint32_t g_last_activity_ms;
extern int g_dim_state;
extern uint32_t g_last_scroll_ms;
extern uint32_t g_menu_input_block_until_ms;

static const char *TAG = "ui_state";

lv_obj_t *ui_state_get_tileview(void)
{
    return g_tileview;
}

void ui_state_set_tileview(lv_obj_t *tv)
{
    g_tileview = tv;
}

void ui_state_get_status_text(char *buf, size_t buf_len)
{
    if (buf && buf_len > 0) {
        strncpy(buf, g_status_text, buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
}

void ui_state_set_status_text(const char *text)
{
    if (text) {
        strncpy(g_status_text, text, sizeof(g_status_text) - 1);
        g_status_text[sizeof(g_status_text) - 1] = '\0';
    }
}

uint32_t ui_state_get_last_activity_ms(void)
{
    return g_last_activity_ms;
}

void ui_state_set_last_activity_ms(uint32_t t)
{
    g_last_activity_ms = t;
}

void ui_state_notify_activity(void)
{
    g_last_activity_ms = lv_tick_get();
    event_bus_publish(EVENT_USER_ACTIVITY, NULL, 0);
}

int ui_state_get_dim_state(void)
{
    return g_dim_state;
}

void ui_state_set_dim_state(int state)
{
    if (g_dim_state != state) {
        g_dim_state = state;
        int s = state;
        event_bus_publish(EVENT_BACKLIGHT_CHANGED, &s, sizeof(s));
    }
}

uint32_t ui_state_get_last_scroll_ms(void)
{
    return g_last_scroll_ms;
}

void ui_state_set_last_scroll_ms(uint32_t t)
{
    g_last_scroll_ms = t;
}

uint32_t ui_state_get_menu_block_until_ms(void)
{
    return g_menu_input_block_until_ms;
}

void ui_state_set_menu_block_until_ms(uint32_t t)
{
    g_menu_input_block_until_ms = t;
}

bool ui_state_menu_input_blocked(void)
{
    return lv_tick_get() < g_menu_input_block_until_ms;
}
