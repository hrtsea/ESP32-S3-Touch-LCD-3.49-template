#pragma once

#include <stdint.h>
#include <stdbool.h>

#define FAN_CURVE_POINTS       5
#define FAN_DEFAULT_HYSTERESIS 3
#define FAN_DEFAULT_MIN_PWM    20
#define FAN_DEFAULT_EMERGENCY  55
#define FAN_DEFAULT_RAMP_MS    2000
#define FAN_DEFAULT_STALL_SEC  5
#define FAN_ENABLED            true

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
