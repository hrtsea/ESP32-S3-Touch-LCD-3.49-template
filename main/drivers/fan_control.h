#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/ledc.h"
#include "driver/pcnt.h"
#include "nas_data.h"
#include "fan_config.h"    /* FanConfig 类型（config 层提供） */

#ifdef __cplusplus
extern "C" {
#endif

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

/* 风扇硬件管脚与参数（原位于已废弃的 config.h，迁移至此） */
#ifndef FAN_PWM_GPIO
#define FAN_PWM_GPIO      4
#endif
#ifndef FAN_TACH_GPIO
#define FAN_TACH_GPIO     5
#endif
#define FAN_PWM_FREQ_HZ   25000   /* 25KHz 风扇标准 PWM 频率 */
#define FAN_PWM_RES_BITS  10      /* 10bit = 1024 级 */
#define FAN_PULSES_PER_REV 2      /* 风扇每转脉冲数（典型 2） */
#define FAN_RPM_SAMPLE_MS 1000    /* 1 秒采样窗口 */

#ifdef __cplusplus
}
#endif
