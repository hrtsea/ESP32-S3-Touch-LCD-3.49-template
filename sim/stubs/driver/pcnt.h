/* sim/stubs/driver/pcnt.h - PCNT 计数器 stub（fan_control.h 引用枚举） */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PCNT_UNIT_0 = 0,
    PCNT_UNIT_1,
    PCNT_UNIT_2,
    PCNT_UNIT_3,
    PCNT_UNIT_MAX,
} pcnt_unit_t;

typedef enum {
    PCNT_CHANNEL_0 = 0,
    PCNT_CHANNEL_1,
    PCNT_CHANNEL_MAX,
} pcnt_channel_t;

typedef enum {
    PCNT_COUNT_INC = 0,
    PCNT_COUNT_DEC,
    PCNT_COUNT_DIS,
} pcnt_count_mode_t;

typedef enum {
    PCNT_MODE_KEEP = 0,
    PCNT_MODE_REVERSE,
    PCNT_MODE_DISABLE,
} pcnt_ctrl_mode_t;

typedef enum {
    PCNT_COUNT_PULSE_DISABLE = 0,
    PCNT_COUNT_PULSE_ENABLE,
} pcnt_pulse_count_cmd_t;

typedef struct {
    int pulse_gpio_num;
    int ctrl_gpio_num;
    pcnt_ctrl_mode_t lctrl_mode;
    pcnt_ctrl_mode_t hctrl_mode;
    pcnt_count_mode_t pos_mode;
    pcnt_count_mode_t neg_mode;
    int16_t counter_h_lim;
    int16_t counter_l_lim;
    pcnt_unit_t unit;
    pcnt_channel_t channel;
} pcnt_config_t;

static inline int pcnt_unit_config(const pcnt_config_t *cfg) {
    (void)cfg; return 0;
}
static inline int pcnt_count_mode_set(pcnt_unit_t u, pcnt_channel_t ch,
                                       pcnt_count_mode_t pos, pcnt_count_mode_t neg) {
    (void)u; (void)ch; (void)pos; (void)neg; return 0;
}
static inline int pcnt_counter_clear(pcnt_unit_t u) { (void)u; return 0; }
static inline int pcnt_counter_resume(pcnt_unit_t u) { (void)u; return 0; }
static inline int pcnt_counter_pause(pcnt_unit_t u) { (void)u; return 0; }
static inline int pcnt_get_counter_value(pcnt_unit_t u, int16_t *count) {
    (void)u; if (count) *count = 0; return 0;
}
static inline int pcnt_intr_enable(pcnt_unit_t u) { (void)u; return 0; }
static inline int pcnt_intr_disable(pcnt_unit_t u) { (void)u; return 0; }
static inline int pcnt_event_enable(pcnt_unit_t u, int evt) { (void)u; (void)evt; return 0; }
static inline int pcnt_set_event_value(pcnt_unit_t u, int evt, int16_t val) {
    (void)u; (void)evt; (void)val; return 0;
}
static inline int pcnt_filter_enable(pcnt_unit_t u) { (void)u; return 0; }
static inline int pcnt_filter_disable(pcnt_unit_t u) { (void)u; return 0; }
static inline int pcnt_set_filter_value(pcnt_unit_t u, uint16_t val) {
    (void)u; (void)val; return 0;
}

#ifdef __cplusplus
}
#endif
