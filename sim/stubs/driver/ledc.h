/* sim/stubs/driver/ledc.h - LEDC PWM stub（fan_control.h 引用枚举/宏） */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LEDC_TIMER_0 = 0,
    LEDC_TIMER_1,
    LEDC_TIMER_2,
    LEDC_TIMER_3,
    LEDC_TIMER_MAX,
} ledc_timer_t;

typedef enum {
    LEDC_CHANNEL_0 = 0,
    LEDC_CHANNEL_1,
    LEDC_CHANNEL_2,
    LEDC_CHANNEL_3,
    LEDC_CHANNEL_4,
    LEDC_CHANNEL_5,
    LEDC_CHANNEL_6,
    LEDC_CHANNEL_7,
    LEDC_CHANNEL_MAX,
} ledc_channel_t;

typedef enum {
    LEDC_LOW_SPEED_MODE = 0,
    LEDC_HIGH_SPEED_MODE,
} ledc_mode_t;

typedef enum {
    LEDC_APB_CLK = 0,
    LEDC_REF_TICK,
    LEDC_SLOW_CLK_RC_FAST,
} ledc_clk_cfg_t;

typedef enum {
    LEDC_TIMER_RES_BIT_1 = 1,
    LEDC_TIMER_RES_BIT_8 = 8,
    LEDC_TIMER_RES_BIT_10 = 10,
    LEDC_TIMER_RES_BIT_MAX,
} ledc_timer_bit_t;

typedef enum {
    LEDC_INTR_DISABLE = 0,
    LEDC_INTR_ENABLE,
} ledc_intr_type_t;

typedef struct {
    ledc_clk_cfg_t clk_cfg;
    ledc_mode_t speed_mode;
    ledc_timer_t timer_num;
    uint32_t duty_resolution;
    uint32_t freq_hz;
    ledc_intr_type_t intr_type;
    void *deconfigure;
} ledc_timer_config_t;

typedef struct {
    int gpio_num;
    ledc_mode_t speed_mode;
    ledc_channel_t channel;
    ledc_intr_type_t intr_type;
    ledc_timer_t timer_sel;
    uint32_t duty;
    int hpoint;
    void *flags;
} ledc_channel_config_t;

static inline int ledc_timer_config(const ledc_timer_config_t *cfg) {
    (void)cfg; return 0;
}
static inline int ledc_channel_config(const ledc_channel_config_t *cfg) {
    (void)cfg; return 0;
}
static inline int ledc_set_duty(ledc_mode_t mode, ledc_channel_t ch, uint32_t duty) {
    (void)mode; (void)ch; (void)duty; return 0;
}
static inline int ledc_update_duty(ledc_mode_t mode, ledc_channel_t ch) {
    (void)mode; (void)ch; return 0;
}
static inline int ledc_set_freq(ledc_mode_t mode, ledc_timer_t t, uint32_t freq) {
    (void)mode; (void)t; (void)freq; return 0;
}
static inline uint32_t ledc_get_duty(ledc_mode_t mode, ledc_channel_t ch) {
    (void)mode; (void)ch; return 0;
}
static inline int ledc_stop(ledc_mode_t mode, ledc_channel_t ch, uint32_t idle_level) {
    (void)mode; (void)ch; (void)idle_level; return 0;
}

#ifdef __cplusplus
}
#endif
