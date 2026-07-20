#include "fan_control.h"
#include "config.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "fan_ctrl";

/* ============================================================
 * 内部状态（受 s_state_mux 保护）
 * ============================================================ */
static SemaphoreHandle_t s_state_mux = NULL;
static int16_t s_last_temp_cpu = 0;
static int16_t s_last_temp_sys = 0;
static int16_t s_ctrl_temp = 0;

static uint8_t  s_current_pwm = 0;       /* 当前实际输出 PWM (0-100) */
static uint16_t s_current_rpm = 0;       /* 当前 RPM */
static bool     s_stall_alarm = false;   /* 停转告警 */
static uint8_t  s_stall_zero_count = 0;  /* 连续 0 RPM 计数 */

static TaskHandle_t s_fan_task_hdl = NULL;
static bool s_inited = false;

/* ============================================================
 * 内部辅助：温度源选择 + 曲线插值
 * ============================================================ */
static int16_t select_ctrl_temp(TempSource src)
{
    int16_t cpu, sys;
    xSemaphoreTake(s_state_mux, portMAX_DELAY);
    cpu = s_last_temp_cpu;
    sys = s_last_temp_sys;
    xSemaphoreGive(s_state_mux);

    switch (src) {
        case TEMP_CPU_ONLY:    return cpu;
        case TEMP_SYS_ONLY:    return sys;
        case TEMP_AVG_CPU_SYS: return (int16_t)((cpu + sys) / 2);
        case TEMP_MAX_CPU_SYS:
        default:               return (cpu > sys) ? cpu : sys;
    }
}

static uint8_t eval_curve(int16_t temp, const FanCurvePoint *curve)
{
    if (temp <= curve[0].temp) return curve[0].pwm_pct;
    if (temp >= curve[FAN_CURVE_POINTS - 1].temp) return curve[FAN_CURVE_POINTS - 1].pwm_pct;

    for (int i = 0; i < FAN_CURVE_POINTS - 1; i++) {
        if (temp >= curve[i].temp && temp < curve[i + 1].temp) {
            int16_t dt = curve[i + 1].temp - curve[i].temp;
            int16_t dp = (int16_t)curve[i + 1].pwm_pct - (int16_t)curve[i].pwm_pct;
            int16_t result = (int16_t)curve[i].pwm_pct +
                             (int16_t)((int32_t)dp * (temp - curve[i].temp) / dt);
            if (result < 0) result = 0;
            if (result > 100) result = 100;
            return (uint8_t)result;
        }
    }
    return curve[FAN_CURVE_POINTS - 1].pwm_pct;
}

/* ============================================================
 * LEDC PWM 配置
 * ============================================================ */
static void fan_ledc_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode      = FAN_LEDC_MODE,
        .timer_num       = FAN_LEDC_TIMER,
        .duty_resolution = FAN_PWM_RES_BITS,
        .freq_hz         = FAN_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_APB_CLK,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch_conf = {
        .gpio_num   = FAN_PWM_GPIO,
        .speed_mode = FAN_LEDC_MODE,
        .channel    = FAN_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = FAN_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_channel_config(&ch_conf));

    ESP_LOGI(TAG, "LEDC PWM: gpio=%d freq=%dHz res=%dbit timer=%d ch=%d",
             FAN_PWM_GPIO, FAN_PWM_FREQ_HZ, FAN_PWM_RES_BITS,
             FAN_LEDC_TIMER, FAN_LEDC_CHANNEL);
}

static void fan_ledc_set_duty_pct(uint8_t pct)
{
    if (pct > 100) pct = 100;
    uint32_t max_duty = (1u << FAN_PWM_RES_BITS) - 1;
    uint32_t duty = (uint32_t)(max_duty * pct) / 100u;
    ledc_set_duty(FAN_LEDC_MODE, FAN_LEDC_CHANNEL, duty);
    ledc_update_duty(FAN_LEDC_MODE, FAN_LEDC_CHANNEL);
}

/* ============================================================
 * PCNT 测速配置
 * ============================================================ */
static void fan_pcnt_init(void)
{
    pcnt_config_t pcnt_cfg = {
        .pulse_gpio_num = FAN_TACH_GPIO,
        .ctrl_gpio_num  = PCNT_PIN_NOT_USED,
        .channel        = FAN_PCNT_CHANNEL,
        .unit           = FAN_PCNT_UNIT,
        .pos_mode       = PCNT_COUNT_INC,    /* 上升沿计数 */
        .neg_mode       = PCNT_COUNT_DIS,
        .lctrl_mode     = PCNT_MODE_KEEP,
        .hctrl_mode     = PCNT_MODE_KEEP,
        .counter_h_lim  = 0x7FFF,
        .counter_l_lim  = 0,
    };
    esp_err_t ret = pcnt_unit_config(&pcnt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "pcnt_unit_config failed: %s (TACH disabled)", esp_err_to_name(ret));
        return;
    }
    pcnt_set_filter_value(FAN_PCNT_UNIT, 100);
    pcnt_filter_enable(FAN_PCNT_UNIT);
    pcnt_counter_clear(FAN_PCNT_UNIT);
    /* 计数模式已在 pcnt_unit_config 中通过 pos_mode/neg_mode 设置 */

    ESP_LOGI(TAG, "PCNT TACH: gpio=%d unit=%d ch=%d", FAN_TACH_GPIO, FAN_PCNT_UNIT, FAN_PCNT_CHANNEL);
}

static uint16_t fan_pcnt_read_rpm(void)
{
    int16_t count = 0;
    pcnt_counter_clear(FAN_PCNT_UNIT);
    /* 等待 1 个采样窗口 */
    vTaskDelay(pdMS_TO_TICKS(FAN_RPM_SAMPLE_MS));
    pcnt_get_counter_value(FAN_PCNT_UNIT, &count);

    /* RPM = (count * 60) / (pulses_per_rev * sample_seconds) */
    int32_t rpm = (int32_t)count * 60 / FAN_PULSES_PER_REV;
    if (rpm < 0) rpm = 0;
    if (rpm > 9999) rpm = 9999;
    return (uint16_t)rpm;
}

/* ============================================================
 * 风扇控制任务
 * ============================================================ */
static void fan_control_task(void *arg)
{
    (void)arg;
    uint32_t status_publish_counter = 0;
    const uint32_t status_publish_period = FAN_STATUS_PUBLISH_MS / FAN_CONTROL_PERIOD_MS;
    FanConfig cfg_snapshot;

    ESP_LOGI(TAG, "fan_control_task started (period=%dms)", FAN_CONTROL_PERIOD_MS);

    while (1) {
        /* 读取最新 RPM */
        s_current_rpm = fan_pcnt_read_rpm();

        /* 读取最新配置副本（线程安全） */
        memcpy(&cfg_snapshot, &g_config.fan, sizeof(FanConfig));

        if (!cfg_snapshot.enabled) {
            fan_ledc_set_duty_pct(0);
            s_current_pwm = 0;
            s_stall_alarm = false;
            s_stall_zero_count = 0;
        } else if (cfg_snapshot.mode == FAN_MODE_MANUAL) {
            uint8_t target = cfg_snapshot.manual_pwm_pct;
            fan_ledc_set_duty_pct(target);
            s_current_pwm = target;
            s_ctrl_temp = select_ctrl_temp(cfg_snapshot.temp_source);
            s_stall_alarm = false;
            s_stall_zero_count = 0;
        } else {
            /* AUTO 模式 */
            int16_t temp = select_ctrl_temp(cfg_snapshot.temp_source);
            s_ctrl_temp = temp;

            /* 紧急温度保护 */
            uint8_t target;
            if (cfg_snapshot.emergency_temp > 0 && temp >= cfg_snapshot.emergency_temp) {
                target = 100;
            } else {
                target = eval_curve(temp, cfg_snapshot.curve);
            }

            /* 最低 PWM 限制 */
            if (target < cfg_snapshot.min_pwm_pct) {
                target = cfg_snapshot.min_pwm_pct;
            }

            /* 滞后防抖：变化幅度小于 hysteresis 时保持当前值 */
            int16_t diff = (int16_t)target - (int16_t)s_current_pwm;
            if (abs(diff) < cfg_snapshot.hysteresis && target != 0 && s_current_pwm != 0) {
                target = s_current_pwm;
            }
            /* 最小变化阈值 */
            if (abs(diff) < cfg_snapshot.min_change_pct && target != 0 && s_current_pwm != 0) {
                target = s_current_pwm;
            }

            fan_ledc_set_duty_pct(target);
            s_current_pwm = target;

            /* 停转检测：PWM > min_pwm 但 RPM = 0 持续 stall_detect_sec 秒 */
            if (target > cfg_snapshot.min_pwm_pct && s_current_rpm == 0) {
                s_stall_zero_count++;
                if (s_stall_zero_count >= cfg_snapshot.stall_detect_sec) {
                    if (!s_stall_alarm) {
                        ESP_LOGW(TAG, "STALL ALARM: pwm=%u rpm=%u for %d sec",
                                 s_current_pwm, s_current_rpm, s_stall_zero_count);
                    }
                    s_stall_alarm = true;
                }
            } else {
                s_stall_zero_count = 0;
                s_stall_alarm = false;
            }
        }

        /* 周期性发布状态 */
        status_publish_counter++;
        if (status_publish_counter >= status_publish_period) {
            status_publish_counter = 0;
            event_bus_publish(EVENT_FAN_STATUS_UPDATE, NULL, 0);
        }

        /* 周期日志 */
        ESP_LOGD(TAG, "pwm=%u rpm=%u temp=%d alarm=%d",
                 s_current_pwm, s_current_rpm, s_ctrl_temp, s_stall_alarm);
    }
}

/* ============================================================
 * 对外 API 实现
 * ============================================================ */
void fan_control_init(void)
{
    if (s_inited) return;

    s_state_mux = xSemaphoreCreateMutex();
    if (!s_state_mux) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return;
    }

    fan_ledc_init();
    fan_pcnt_init();

    /* 应用初始配置 */
    fan_ledc_set_duty_pct(g_config.fan.enabled ? g_config.fan.manual_pwm_pct : 0);

    xTaskCreate(fan_control_task, "fan_ctrl", FAN_TASK_STACK, NULL,
                FAN_TASK_PRIORITY, &s_fan_task_hdl);

    s_inited = true;
    ESP_LOGI(TAG, "fan_control initialized (mode=%d enabled=%d)",
             g_config.fan.mode, g_config.fan.enabled);
}

void fan_control_set_pwm(uint8_t pct)
{
    if (!s_inited) return;
    fan_ledc_set_duty_pct(pct);
    s_current_pwm = pct;
}

uint16_t fan_control_get_rpm(void)
{
    return s_current_rpm;
}

uint8_t fan_control_get_current_pwm(void)
{
    return s_current_pwm;
}

int16_t fan_control_get_ctrl_temp(void)
{
    return s_ctrl_temp;
}

bool fan_control_is_stall_alarm(void)
{
    return s_stall_alarm;
}

void fan_control_apply_config(const FanConfig *cfg)
{
    if (!cfg || !s_inited) return;
    /* 配置变更后下一次控制循环会自动读取 g_config.fan，无需特殊处理
     * 此处仅记录日志 */
    ESP_LOGI(TAG, "config applied: mode=%d enabled=%d manual=%u",
             cfg->mode, cfg->enabled, cfg->manual_pwm_pct);
}

void fan_control_get_status(FanStatus *out)
{
    if (!out) return;
    out->rpm         = s_current_rpm;
    out->pwm_pct     = s_current_pwm;
    out->ctrl_temp   = s_ctrl_temp;
    out->stall_alarm = s_stall_alarm;
    out->enabled     = g_config.fan.enabled;
}

void fan_control_on_nas_data(const NasData *data)
{
    if (!data) return;
    xSemaphoreTake(s_state_mux, portMAX_DELAY);
    s_last_temp_cpu = data->system.temp_cpu;
    s_last_temp_sys = data->system.temp_sys;
    xSemaphoreGive(s_state_mux);
}
