/* sim/overrides/fan_control.c - 风扇控制内存版
 * 不依赖 LEDC/PCNT 硬件，直接返回固定的风扇状态用于 UI 显示。
 * PWM/RPM 通过 set_pwm/get_rpm 在内存中维护。
 */
#include "fan_control.h"
#include "config.h"
#include "nas_data.h"
#include <string.h>

static FanStatus s_fan_status = {
    .rpm = 1240,
    .pwm_pct = 35,
    .ctrl_temp = 42,
    .stall_alarm = false,
    .enabled = true,
};

void fan_control_init(void) {
    /* 同步配置初始值到内存状态 */
    s_fan_status.enabled = g_config.fan.enabled;
}

void fan_control_set_pwm(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_fan_status.pwm_pct = pct;
    /* 简单线性模型：rpm = 200 + pct*40 */
    s_fan_status.rpm = (uint16_t)(200 + (uint32_t)pct * 40);
}

uint16_t fan_control_get_rpm(void) { return s_fan_status.rpm; }
uint8_t  fan_control_get_current_pwm(void) { return s_fan_status.pwm_pct; }
int16_t fan_control_get_ctrl_temp(void) { return s_fan_status.ctrl_temp; }
bool    fan_control_is_stall_alarm(void) { return s_fan_status.stall_alarm; }

void fan_control_apply_config(const FanConfig *cfg) {
    (void)cfg;
    /* 配置变更后无需重启硬件 */
}

void fan_control_get_status(FanStatus *out) {
    if (!out) return;
    *out = s_fan_status;
}

void fan_control_on_nas_data(const NasData *data) {
    if (!data) return;
    /* 模拟控制温度跟随 NAS CPU 温度变化 */
    int16_t t = data->system.temp_cpu;
    if (t > 0) {
        s_fan_status.ctrl_temp = t;
        /* 简单温度→PWM 映射，模拟自动模式 */
        if (g_config.fan.mode == FAN_MODE_AUTO) {
            uint8_t pwm = 25;
            if (t >= 65)      pwm = 100;
            else if (t >= 55) pwm = 80;
            else if (t >= 45) pwm = 50;
            else if (t >= 35) pwm = 30;
            else              pwm = 25;
            fan_control_set_pwm(pwm);
        }
    }
}
