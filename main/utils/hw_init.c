#include "hw_init.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "esp_log.h"

#include "ui_common.h"
#include "esp_io_expander_tca9554.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "user_config.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "lcd_bl_pwm_bsp.h"
#include "adc_bsp.h"
#include "sdcard_bsp.h"
#include "button_bsp.h"
#include "audio_min.h"

#include "app_cfg.h"
#include "disp_driver.h"

static const char *TAG = "hw_init";

#define BL_MAX_BRIGHTNESS 255u
#define TCA9554_POWER_DELAY_MS 50u

static int status_text_pos = 0;

static void status_text_append(const char *fmt, ...)
{
    if (status_text_pos >= (int)sizeof(g_status_text) - 1) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(g_status_text + status_text_pos, 
                      sizeof(g_status_text) - status_text_pos, fmt, args);
    va_end(args);
    if (n > 0) {
        status_text_pos += n;
        if (status_text_pos >= (int)sizeof(g_status_text) - 1) {
            status_text_pos = (int)sizeof(g_status_text) - 1;
            g_status_text[status_text_pos] = '\0';
        }
    }
}

void hw_init(void)
{
    status_text_append("Drivers:\n");
    ESP_LOGI(TAG, "[1/9] I2C buses");
    i2c_master_Init();
    ESP_LOGI(TAG, "      esp_i2c_bus_handle=%p", esp_i2c_bus_handle);
    status_text_append("I2C OK\n");

    ESP_LOGI(TAG, "[2/9] TCA9554 power rails P6+P7=HIGH");
    {
        esp_io_expander_handle_t io_expander = NULL;
        esp_err_t er = esp_io_expander_new_i2c_tca9554(
            esp_i2c_bus_handle, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);
        ESP_LOGI(TAG, "      tca9554 new=%s handle=%p", esp_err_to_name(er), io_expander);
        ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander,
            IO_EXPANDER_PIN_NUM_7 | IO_EXPANDER_PIN_NUM_6, IO_EXPANDER_OUTPUT));
        ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander,
            IO_EXPANDER_PIN_NUM_7 | IO_EXPANDER_PIN_NUM_6, 1));
        esp_io_expander_print_state(io_expander);
        vTaskDelay(pdMS_TO_TICKS(TCA9554_POWER_DELAY_MS));
    }
    status_text_append("TCA9554 OK\n");

    ESP_LOGI(TAG, "[3/9] LCD backlight PWM (from cfg)");
    lcd_bl_pwm_bsp_init((uint16_t)(BL_MAX_BRIGHTNESS - g_cfg.brightness));
    status_text_append("BL OK\n");

    ESP_LOGI(TAG, "[4/9] LCD panel + LVGL");
    disp_driver_init();
    status_text_append("LCD/Touch OK\n");

    ESP_LOGI(TAG, "[5/9] RTC + IMU");
    i2c_rtc_setup();
    i2c_imu_setup();
    status_text_append("RTC/IMU OK\n");

    ESP_LOGI(TAG, "[6/9] ADC battery");
    adc_bsp_init();
    status_text_append("ADC OK\n");

    ESP_LOGI(TAG, "[7/9] Audio (audio_min: ES8311 + I2S)");
    if (audio_min_init() == ESP_OK) {
        audio_min_set_volume(g_cfg.audio_volume);
        status_text_append("Audio OK\n");
    } else {
        status_text_append("Audio FAIL\n");
    }

    ESP_LOGI(TAG, "[8/9] SD card + Buttons");
    _sdcard_init();
    button_Init();
    status_text_append("SD/Btn OK");
}
