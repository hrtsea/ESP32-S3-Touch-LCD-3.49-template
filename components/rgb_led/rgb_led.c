#include "rgb_led.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "RGB_LED";

// LEDC 配置（避免与风扇和 LCD 背光冲突）
#define RGB_LEDC_TIMER      LEDC_TIMER_1
#define RGB_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define RGB_LEDC_R_CHANNEL  LEDC_CHANNEL_3
#define RGB_LEDC_G_CHANNEL  LEDC_CHANNEL_4
#define RGB_LEDC_B_CHANNEL  LEDC_CHANNEL_5
#define RGB_LEDC_DUTY_RES   LEDC_TIMER_8_BIT
#define RGB_LEDC_FREQ_HZ    1000

// GPIO 引脚定义（根据实际硬件调整）
#ifndef RGB_LED_R_GPIO
#define RGB_LED_R_GPIO   4
#endif
#ifndef RGB_LED_G_GPIO
#define RGB_LED_G_GPIO   5
#endif
#ifndef RGB_LED_B_GPIO
#define RGB_LED_B_GPIO   6
#endif

// 默认配置
#define DEFAULT_BRIGHTNESS     0.25f  // 25% 默认亮度
#define DEFAULT_TEMP_MIN       35.0f  // 默认最低温度
#define DEFAULT_TEMP_MAX       85.0f  // 默认最高温度

// 内部状态
static TaskHandle_t s_anim_task_hdl = NULL;
static SemaphoreHandle_t s_state_mux = NULL;
static RgbLedConfig s_config = {0};
static RgbLedStatus s_status = {0};
static bool s_initialized = false;

// 温度监控
static float s_current_temp = 0.0f;
static esp_timer_handle_t s_temp_timer = NULL;

// 设置单个通道的 PWM 占空比
static void set_channel_pwm(uint8_t channel, uint8_t value)
{
    ledc_set_duty(RGB_LEDC_MODE, channel, value);
    ledc_update_duty(RGB_LEDC_MODE, channel);
}

// 应用颜色（考虑亮度）
static void apply_color(RgbColor color)
{
    // 应用亮度缩放
    uint8_t r = (uint8_t)(color.r * s_config.brightness);
    uint8_t g = (uint8_t)(color.g * s_config.brightness);
    uint8_t b = (uint8_t)(color.b * s_config.brightness);

    set_channel_pwm(RGB_LEDC_R_CHANNEL, r);
    set_channel_pwm(RGB_LEDC_G_CHANNEL, g);
    set_channel_pwm(RGB_LEDC_B_CHANNEL, b);

    s_status.current_color = color;
}

// 初始化 LEDC PWM
esp_err_t rgb_led_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // 创建互斥锁
    s_state_mux = xSemaphoreCreateMutex();
    if (!s_state_mux) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 配置定时器
    ledc_timer_config_t timer_conf = {
        .speed_mode      = RGB_LEDC_MODE,
        .timer_num       = RGB_LEDC_TIMER,
        .duty_resolution = RGB_LEDC_DUTY_RES,
        .freq_hz         = RGB_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_APB_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置 R 通道
    ledc_channel_config_t r_conf = {
        .gpio_num   = RGB_LED_R_GPIO,
        .speed_mode = RGB_LEDC_MODE,
        .channel    = RGB_LEDC_R_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = RGB_LEDC_TIMER,
        .duty       = 0,
    };
    ledc_channel_config(&r_conf);

    // 配置 G 通道
    ledc_channel_config_t g_conf = {
        .gpio_num   = RGB_LED_G_GPIO,
        .speed_mode = RGB_LEDC_MODE,
        .channel    = RGB_LEDC_G_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = RGB_LEDC_TIMER,
        .duty       = 0,
    };
    ledc_channel_config(&g_conf);

    // 配置 B 通道
    ledc_channel_config_t b_conf = {
        .gpio_num   = RGB_LED_B_GPIO,
        .speed_mode = RGB_LEDC_MODE,
        .channel    = RGB_LEDC_B_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = RGB_LEDC_TIMER,
        .duty       = 0,
    };
    ledc_channel_config(&b_conf);

    // 初始化默认配置
    s_config.mode = RGB_MODE_TEMP_MAP;
    s_config.start_color = (RgbColor){0, 0, 0};
    s_config.end_color = (RgbColor){0, 0, 0};
    s_config.speed = 5;
    s_config.brightness = DEFAULT_BRIGHTNESS;
    s_config.enabled = true;
    s_config.temp_min = DEFAULT_TEMP_MIN;
    s_config.temp_max = DEFAULT_TEMP_MAX;
    s_config.use_cpu_temp = true;

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized (R=%d, G=%d, B=%d, mode=%d)",
             RGB_LED_R_GPIO, RGB_LED_G_GPIO, RGB_LED_B_GPIO, s_config.mode);

    return ESP_OK;
}

// 设置固定颜色
esp_err_t rgb_led_set_color(RgbColor color)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state_mux, portMAX_DELAY);
    apply_color(color);
    s_status.current_mode = RGB_MODE_LIGHT;
    s_status.animation_active = false;
    xSemaphoreGive(s_state_mux);

    ESP_LOGD(TAG, "Set color: R=%d G=%d B=%d", color.r, color.g, color.b);
    return ESP_OK;
}

// 呼吸动画
static void animate_breathe(RgbColor color, uint8_t speed)
{
    float brightness = 0.0f;
    float step = 0.01f * (speed / 10.0f);
    bool increasing = true;

    while (s_config.mode == RGB_MODE_BREATHE && s_config.enabled) {
        if (increasing) {
            brightness += step;
            if (brightness >= 1.0f) {
                brightness = 1.0f;
                increasing = false;
            }
        } else {
            brightness -= step;
            if (brightness <= 0.0f) {
                brightness = 0.0f;
                increasing = true;
            }
        }

        RgbColor current = {
            .r = (uint8_t)(color.r * brightness),
            .g = (uint8_t)(color.g * brightness),
            .b = (uint8_t)(color.b * brightness)
        };

        xSemaphoreTake(s_state_mux, portMAX_DELAY);
        apply_color(current);
        xSemaphoreGive(s_state_mux);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// 渐变动画
static void animate_gradient(RgbColor start, RgbColor end, uint8_t speed)
{
    float t = 0.0f;
    float step = 0.005f * (speed / 10.0f);

    while (s_config.mode == RGB_MODE_GRADIENT && s_config.enabled) {
        t += step;
        if (t >= 1.0f) t = 0.0f;

        RgbColor current = {
            .r = (uint8_t)(start.r + (end.r - start.r) * t),
            .g = (uint8_t)(start.g + (end.g - start.g) * t),
            .b = (uint8_t)(start.b + (end.b - start.b) * t)
        };

        xSemaphoreTake(s_state_mux, portMAX_DELAY);
        apply_color(current);
        xSemaphoreGive(s_state_mux);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// 温度映射动画
static void animate_temp_map(void)
{
    while (s_config.mode == RGB_MODE_TEMP_MAP && s_config.enabled) {
        float temp = s_current_temp;

        RgbColor color = rgb_led_temp_to_color(temp, s_config.temp_min, s_config.temp_max);

        xSemaphoreTake(s_state_mux, portMAX_DELAY);
        apply_color(color);
        xSemaphoreGive(s_state_mux);

        ESP_LOGD(TAG, "Temp %.1f°C -> RGB(%d,%d,%d)", temp, color.r, color.g, color.b);

        vTaskDelay(pdMS_TO_TICKS(2000));  // 2 秒更新一次
    }
}

// 动画任务
static void animation_task(void *arg)
{
    while (1) {
        if (!s_config.enabled) {
            apply_color((RgbColor){0, 0, 0});
            vTaskSuspend(NULL);
        }

        switch (s_config.mode) {
            case RGB_MODE_OFF:
                apply_color((RgbColor){0, 0, 0});
                vTaskSuspend(NULL);
                break;

            case RGB_MODE_LIGHT:
                apply_color(s_config.start_color);
                vTaskSuspend(NULL);
                break;

            case RGB_MODE_BREATHE:
                s_status.animation_active = true;
                animate_breathe(s_config.start_color, s_config.speed);
                break;

            case RGB_MODE_GRADIENT:
                s_status.animation_active = true;
                animate_gradient(s_config.start_color, s_config.end_color, s_config.speed);
                break;

            case RGB_MODE_TEMP_MAP:
                s_status.animation_active = true;
                animate_temp_map();
                break;

            default:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

// 设置动画模式
esp_err_t rgb_led_set_mode(RgbMode mode, RgbColor start, RgbColor end, uint8_t speed)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state_mux, portMAX_DELAY);
    s_config.mode = mode;
    s_config.start_color = start;
    s_config.end_color = end;
    s_config.speed = speed;
    s_status.current_mode = mode;

    // 创建或唤醒动画任务
    if (!s_anim_task_hdl) {
        xTaskCreate(animation_task, "rgb_anim", 2048, NULL, 5, &s_anim_task_hdl);
    } else {
        vTaskResume(s_anim_task_hdl);
    }

    xSemaphoreGive(s_state_mux);

    ESP_LOGI(TAG, "Set mode=%d speed=%d", mode, speed);
    return ESP_OK;
}

// 应用配置
esp_err_t rgb_led_apply_config(const RgbLedConfig *config)
{
    if (!config || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state_mux, portMAX_DELAY);
    memcpy(&s_config, config, sizeof(RgbLedConfig));

    if (s_anim_task_hdl) {
        vTaskResume(s_anim_task_hdl);
    }

    xSemaphoreGive(s_state_mux);

    ESP_LOGI(TAG, "Config applied: mode=%d enabled=%d", config->mode, config->enabled);
    return ESP_OK;
}

// 温度映射颜色
RgbColor rgb_led_temp_to_color(float temp, float min_temp, float max_temp)
{
    if (max_temp <= min_temp) {
        return (RgbColor){0, 0, 64};  // 默认蓝色
    }

    // 归一化温度 (0.0 - 1.0)
    float t = (temp - min_temp) / (max_temp - min_temp);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // 颜色渐变：蓝色(0) → 红色(1)
    // 亮度固定为配置值
    RgbColor color = {
        .r = (uint8_t)(t * 255 * s_config.brightness),
        .g = 0,
        .b = (uint8_t)((1.0f - t) * 255 * s_config.brightness)
    };

    return color;
}

// 获取当前状态
void rgb_led_get_status(RgbLedStatus *status)
{
    if (!status) return;

    xSemaphoreTake(s_state_mux, portMAX_DELAY);
    memcpy(status, &s_status, sizeof(RgbLedStatus));
    xSemaphoreGive(s_state_mux);
}

// 更新温度（供外部调用）
void rgb_led_update_temp(float temp)
{
    s_current_temp = temp;
}

// 停止所有动画
void rgb_led_stop(void)
{
    if (s_anim_task_hdl) {
        vTaskSuspend(s_anim_task_hdl);
    }
    apply_color((RgbColor){0, 0, 0});
    s_status.animation_active = false;

    ESP_LOGI(TAG, "RGB LED stopped");
}