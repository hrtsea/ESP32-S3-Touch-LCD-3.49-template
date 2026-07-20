#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/ledc.h"
#include "driver/pcnt.h"
#include "nas_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAN_CURVE_POINTS       5
#define FAN_DEFAULT_HYSTERESIS 3
#define FAN_DEFAULT_MIN_PWM    20
#define FAN_DEFAULT_EMERGENCY  55
#define FAN_DEFAULT_RAMP_MS    2000
#define FAN_DEFAULT_STALL_SEC  5
#define FAN_ENABLED            true

/* 风扇 LEDC/PCNT 硬件资源（避免与 LCD 背光冲突：LCD 用 TIMER_3/CHANNEL_1） */
#ifndef FAN_LEDC_TIMER
#define FAN_LEDC_TIMER     LEDC_TIMER_2
#endif
#ifndef FAN_LEDC_CHANNEL
#define FAN_LEDC_CHANNEL   LEDC_CHANNEL_2
#endif
#define FAN_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define FAN_PCNT_UNIT      PCNT_UNIT_0
#define FAN_PCNT_CHANNEL   PCNT_CHANNEL_0

/* 风扇控制任务参数 */
#define FAN_TASK_STACK        4096
#define FAN_TASK_PRIORITY     2
#define FAN_DATA_TASK_STACK   3072
#define FAN_DATA_TASK_PRIORITY 2
#define FAN_CONTROL_PERIOD_MS 1000   /* 控制循环周期 */
#define FAN_STATUS_PUBLISH_MS 5000   /* 状态发布间隔 */

typedef enum FanMode {
    FAN_MODE_AUTO = 0,
    FAN_MODE_MANUAL = 1
} FanMode;

typedef enum TempSource {
    TEMP_MAX_CPU_SYS = 0,
    TEMP_AVG_CPU_SYS = 1,
    TEMP_CPU_ONLY    = 2,
    TEMP_SYS_ONLY    = 3
} TempSource;

typedef struct FanCurvePoint {
    int16_t temp;
    uint8_t pwm_pct;
} FanCurvePoint;

typedef struct FanConfig {
    FanCurvePoint curve[FAN_CURVE_POINTS];
    TempSource temp_source;
    uint8_t hysteresis;
    uint8_t min_change_pct;
    uint8_t min_pwm_pct;
    int16_t emergency_temp;
    uint8_t stall_detect_sec;
    uint16_t ramp_time_ms;
    FanMode mode;
    uint8_t manual_pwm_pct;
    bool enabled;
} FanConfig;

static const FanCurvePoint DEFAULT_FAN_CURVE[FAN_CURVE_POINTS] = {
    { 25,  25 },
    { 35,  30 },
    { 45,  50 },
    { 55,  80 },
    { 65, 100 },
};

/* ============================================================
 * 风扇控制对外 API
 * ============================================================ */
void fan_control_init(void);                         /* 初始化 LEDC + PCNT + 控制任务 */
void fan_control_set_pwm(uint8_t pct);               /* 直接设置 PWM 占空比 (0-100) */
uint16_t fan_control_get_rpm(void);                  /* 读取当前 RPM */
uint8_t fan_control_get_current_pwm(void);           /* 读取当前实际 PWM */
int16_t fan_control_get_ctrl_temp(void);             /* 读取当前控制温度 */
bool fan_control_is_stall_alarm(void);               /* 停转告警状态 */
void fan_control_apply_config(const FanConfig *cfg); /* 配置变更后重载 */
void fan_control_get_status(FanStatus *out);         /* 填充 FanStatus 给 NasData */
void fan_control_on_nas_data(const NasData *data);   /* ui_events 收到 NAS 数据时回调 */

#ifdef __cplusplus
}
#endif
