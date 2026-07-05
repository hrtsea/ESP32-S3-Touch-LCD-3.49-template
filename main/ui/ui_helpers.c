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

void _ui_bar_set_property(lv_obj_t * target, int id, int val)
{
    if(id == _UI_BAR_PROPERTY_VALUE_WITH_ANIM) lv_bar_set_value(target, val, LV_ANIM_ON);
    if(id == _UI_BAR_PROPERTY_VALUE) lv_bar_set_value(target, val, LV_ANIM_OFF);
}

void _ui_basic_set_property(lv_obj_t * target, int id, int val)
{
    if(id == _UI_BASIC_PROPERTY_POSITION_X) lv_obj_set_x(target, val);
    if(id == _UI_BASIC_PROPERTY_POSITION_Y) lv_obj_set_y(target, val);
    if(id == _UI_BASIC_PROPERTY_WIDTH) lv_obj_set_width(target, val);
    if(id == _UI_BASIC_PROPERTY_HEIGHT) lv_obj_set_height(target, val);
}

void _ui_dropdown_set_property(lv_obj_t * target, int id, int val)
{
    if(id == _UI_DROPDOWN_PROPERTY_SELECTED) lv_dropdown_set_selected(target, val);
}

void _ui_image_set_property(lv_obj_t * target, int id, uint8_t * val)
{
    if(id == _UI_IMAGE_PROPERTY_IMAGE) lv_img_set_src(target, val);
}

void _ui_label_set_property(lv_obj_t * target, int id, const char * val)
{
    if(id == _UI_LABEL_PROPERTY_TEXT) lv_label_set_text(target, val);
}

void _ui_roller_set_property(lv_obj_t * target, int id, int val)
{
    if(id == _UI_ROLLER_PROPERTY_SELECTED_WITH_ANIM) lv_roller_set_selected(target, val, LV_ANIM_ON);
    if(id == _UI_ROLLER_PROPERTY_SELECTED) lv_roller_set_selected(target, val, LV_ANIM_OFF);
}

void _ui_slider_set_property(lv_obj_t * target, int id, int val)
{
    if(id == _UI_SLIDER_PROPERTY_VALUE_WITH_ANIM) lv_slider_set_value(target, val, LV_ANIM_ON);
    if(id == _UI_SLIDER_PROPERTY_VALUE) lv_slider_set_value(target, val, LV_ANIM_OFF);
}

void _ui_screen_change(lv_obj_t ** target, lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void))
{
    if(*target == NULL)
        target_init();
    lv_scr_load_anim(*target, fademode, spd, delay, false);
}

void _ui_screen_delete(void (*target)(void))
{
    if(target != NULL) {
        target();
    }
}

void _ui_arc_increment(lv_obj_t * target, int val)
{
    int old = lv_arc_get_value(target);
    lv_arc_set_value(target, old + val);
    lv_event_send(target, LV_EVENT_VALUE_CHANGED, 0);
}

void _ui_bar_increment(lv_obj_t * target, int val, int anm)
{
    int old = lv_bar_get_value(target);
    lv_bar_set_value(target, old + val, anm);
}

void _ui_slider_increment(lv_obj_t * target, int val, int anm)
{
    int old = lv_slider_get_value(target);
    lv_slider_set_value(target, old + val, anm);
    lv_event_send(target, LV_EVENT_VALUE_CHANGED, 0);
}

void _ui_keyboard_set_target(lv_obj_t * keyboard, lv_obj_t * textarea)
{
    lv_keyboard_set_textarea(keyboard, textarea);
}

void _ui_flag_modify(lv_obj_t * target, int32_t flag, int value)
{
    if(value == _UI_MODIFY_FLAG_TOGGLE) {
        if(lv_obj_has_flag(target, flag)) lv_obj_clear_flag(target, flag);
        else lv_obj_add_flag(target, flag);
    }
    else if(value == _UI_MODIFY_FLAG_ADD) lv_obj_add_flag(target, flag);
    else lv_obj_clear_flag(target, flag);
}

void _ui_state_modify(lv_obj_t * target, int32_t state, int value)
{
    if(value == _UI_MODIFY_STATE_TOGGLE) {
        if(lv_obj_has_state(target, state)) lv_obj_clear_state(target, state);
        else lv_obj_add_state(target, state);
    }
    else if(value == _UI_MODIFY_STATE_ADD) lv_obj_add_state(target, state);
    else lv_obj_clear_state(target, state);
}

void _ui_textarea_move_cursor(lv_obj_t * target, int val)
{
    if(val == UI_MOVE_CURSOR_UP) lv_textarea_cursor_up(target);
    if(val == UI_MOVE_CURSOR_RIGHT) lv_textarea_cursor_right(target);
    if(val == UI_MOVE_CURSOR_DOWN) lv_textarea_cursor_down(target);
    if(val == UI_MOVE_CURSOR_LEFT) lv_textarea_cursor_left(target);
    lv_obj_add_state(target, LV_STATE_FOCUSED);
}

typedef void (*screen_destroy_cb_t)(void);

void scr_unloaded_delete_cb(lv_event_t * e)
{
    screen_destroy_cb_t destroy_cb = lv_event_get_user_data(e);
    if(destroy_cb) {
        destroy_cb();
    }
}

void _ui_opacity_set(lv_obj_t * target, int val)
{
    lv_obj_set_style_opa(target, val, 0);
}

void _ui_anim_callback_free_user_data(lv_anim_t * a)
{
    lv_mem_free(a->user_data);
    a->user_data = NULL;
}

void _ui_anim_callback_set_x(lv_anim_t * a, int32_t v)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_x(usr->target, v);
}

void _ui_anim_callback_set_y(lv_anim_t * a, int32_t v)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_y(usr->target, v);
}

void _ui_anim_callback_set_width(lv_anim_t * a, int32_t v)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_width(usr->target, v);
}

void _ui_anim_callback_set_height(lv_anim_t * a, int32_t v)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_height(usr->target, v);
}

void _ui_anim_callback_set_opacity(lv_anim_t * a, int32_t v)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    lv_obj_set_style_opa(usr->target, v, 0);
}

void _ui_anim_callback_set_image_zoom(lv_anim_t * a, int32_t v)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    lv_img_set_zoom(usr->target, v);
}

void _ui_anim_callback_set_image_angle(lv_anim_t * a, int32_t v)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    lv_img_set_angle(usr->target, v);
}

void _ui_anim_callback_set_image_frame(lv_anim_t * a, int32_t v)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    usr->val = v;
    if(v < 0) v = 0;
    if(v >= usr->imgset_size) v = usr->imgset_size - 1;
    lv_img_set_src(usr->target, usr->imgset[v]);
}

int32_t _ui_anim_callback_get_x(lv_anim_t * a)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_x_aligned(usr->target);
}

int32_t _ui_anim_callback_get_y(lv_anim_t * a)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_y_aligned(usr->target);
}

int32_t _ui_anim_callback_get_width(lv_anim_t * a)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_width(usr->target);
}

int32_t _ui_anim_callback_get_height(lv_anim_t * a)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_height(usr->target);
}

int32_t _ui_anim_callback_get_opacity(lv_anim_t * a)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    return lv_obj_get_style_opa(usr->target, 0);
}

int32_t _ui_anim_callback_get_image_zoom(lv_anim_t * a)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    return lv_img_get_zoom(usr->target);
}

int32_t _ui_anim_callback_get_image_angle(lv_anim_t * a)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    return lv_img_get_angle(usr->target);
}

int32_t _ui_anim_callback_get_image_frame(lv_anim_t * a)
{
    ui_anim_user_data_t * usr = (ui_anim_user_data_t *)a->user_data;
    return usr->val;
}

void _ui_arc_set_text_value(lv_obj_t * trg, lv_obj_t * src, const char * prefix, const char * postfix)
{
    char buf[_UI_TEMPORARY_STRING_BUFFER_SIZE];
    lv_snprintf(buf, sizeof(buf), "%s%d%s", prefix, (int)lv_arc_get_value(src), postfix);
    lv_label_set_text(trg, buf);
}

void _ui_slider_set_text_value(lv_obj_t * trg, lv_obj_t * src, const char * prefix, const char * postfix)
{
    char buf[_UI_TEMPORARY_STRING_BUFFER_SIZE];
    lv_snprintf(buf, sizeof(buf), "%s%d%s", prefix, (int)lv_slider_get_value(src), postfix);
    lv_label_set_text(trg, buf);
}

void _ui_checked_set_text_value(lv_obj_t * trg, lv_obj_t * src, const char * txt_on, const char * txt_off)
{
    if(lv_obj_has_state(src, LV_STATE_CHECKED)) lv_label_set_text(trg, txt_on);
    else lv_label_set_text(trg, txt_off);
}

void _ui_spinbox_step(lv_obj_t * target, int val)
{
    if(val > 0) lv_spinbox_increment(target);
    else lv_spinbox_decrement(target);
    lv_event_send(target, LV_EVENT_VALUE_CHANGED, 0);
}

void _ui_switch_theme(int val)
{
#ifdef UI_THEME_ACTIVE
    ui_theme_set(val);
#endif
}
