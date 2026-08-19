/**
 * @file board.c
 * @brief 3.49" 触摸屏开发板板描述实现
 *
 * 数据来源：main/user_config.h（迁移中，阶段 2/4 将彻底下沉）。
 * 行为等价原配置：物理面板 172×640（竖屏）、LVGL 画布 640×172（横屏）、
 * QSPI 接口、CST 触摸 0x3b。
 */
#include "board.h"
#include "res_class.h"
#include "driver/gpio.h"
#include "user_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_io_expander_tca9554.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "lcd_bl_pwm_bsp.h"
#include "adc_bsp.h"
#include "sdcard_bsp.h"
#include "button_bsp.h"
#include "audio_min.h"
#include "disp_driver.h"
#include "fan_control.h"
#include "app_cfg.h"

static const char *TAG = "board";

#define TCA9554_POWER_DELAY_MS 50u
#define BL_MAX_BRIGHTNESS 255u

static const board_desc_t s_board = {
    .name = "esp32s3_touch_lcd_3_49",

    .lcd_if = LCD_IF_QSPI,
    .panel_h_res = EXAMPLE_LCD_H_RES,        /* 172 */
    .panel_v_res = EXAMPLE_LCD_V_RES,        /* 640 */
    .canvas_w = UI_CANVAS_W,                 /* 640 */
    .canvas_h = UI_CANVAS_H,                 /* 172 */
    .default_rotation = 1,                   /* 90° CW */
    .rotation_step_mode = ROT_STEP_QUARTER,  /* CPU 像素变换，四档可旋 */
    .display_flush_bands = LVGL_FLUSH_STRIP_ROWS, /* 128 */
    .res_class = RES_640X172,

    .backlight = {
        .gpio = EXAMPLE_PIN_NUM_BK_LIGHT,    /* GPIO8 */
        .pwm_channel = -1,                   /* 由 lcd_bl_pwm_bsp 决定 */
        .max_brightness = 255,
        .min_input = 1,
    },

    .panel_pins = {
        .cs = EXAMPLE_PIN_NUM_LCD_CS,        /* GPIO9 */
        .pclk = EXAMPLE_PIN_NUM_LCD_PCLK,    /* GPIO10 */
        .data0 = EXAMPLE_PIN_NUM_LCD_DATA0,  /* GPIO11 */
        .data1 = EXAMPLE_PIN_NUM_LCD_DATA1,  /* GPIO12 */
        .data2 = EXAMPLE_PIN_NUM_LCD_DATA2,  /* GPIO13 */
        .data3 = EXAMPLE_PIN_NUM_LCD_DATA3,  /* GPIO14 */
        .rst = EXAMPLE_PIN_NUM_LCD_RST,      /* GPIO21 */
        .dc = -1,                            /* QSPI 无 DC */
    },

    .touch = {
        .i2c_port = 0,                       /* 复用 i2c_bsp 主端口（阶段 2 细化） */
        .addr = EXAMPLE_PIN_NUM_TOUCH_ADDR,  /* 0x3b */
        .rst_gpio = EXAMPLE_PIN_NUM_TOUCH_RST,  /* -1 */
        .int_gpio = EXAMPLE_PIN_NUM_TOUCH_INT,  /* -1 */
        .h_res = EXAMPLE_LCD_H_RES,          /* 172 */
        .v_res = EXAMPLE_LCD_V_RES,          /* 640 */
    },

    .lvgl_timing = {
        .tick_period_ms = EXAMPLE_LVGL_TICK_PERIOD_MS,        /* 2 */
        .task_max_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS,  /* 16 */
        .task_min_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS,  /* 2 */
    },

    .sd = {
        .sdmmc = true,
        .clk = 41, .cmd = 39, .d0 = 40,      /* 迁移自 sdcard_bsp.c */
        .width = 1,
        .max_freq_khz = 20000,               /* SDMMC_FREQ_DEFAULT */
        .mount_max_files = 5,
        .internal_pullup = true,             /* S3 GPIO matrix 需内上拉 */
        .auto_format = false,
    },

    .periph = {
        .tca9554 = true,
        .rtc = true,
        .imu = true,
        .adc = true,
        .audio = true,
        .fan = true,
        .sd = true,
        .button = true,
        .rgb = true,
    },
};

const board_desc_t *board_get(void)
{
    return &s_board;
}

res_class_t res_class_get(void)
{
    return s_board.res_class;
}

/* ============================================================
 * 板级硬件初始化（随板内聚，替代原 hw_init() 的板级部分）
 * 按 periph 能力位图裁剪；进度通过 s_status_cb 上报（未注册仅日志）。
 * 原 hw_init.c 的 10 步中：1-9 移入此处，第 10 步（系统时间）保留在 main 层。
 * ============================================================ */

static board_init_status_cb_t s_status_cb = NULL;

void board_set_status_cb(board_init_status_cb_t cb)
{
    s_status_cb = cb;
}

static void status_line(const char *line)
{
    if (s_status_cb) {
        s_status_cb(line);
    }
}

void board_init(void)
{
    const board_desc_t *bd = board_get();

    status_line("Drivers:\n");

    ESP_LOGI(TAG, "[1] I2C buses");
    i2c_master_Init();
    ESP_LOGI(TAG, "      esp_i2c_bus_handle=%p", esp_i2c_bus_handle);
    status_line("I2C OK\n");

    if (bd->periph.tca9554) {
        ESP_LOGI(TAG, "[2] TCA9554 power rails P6+P7=HIGH");
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
        status_line("TCA9554 OK\n");
    }

    ESP_LOGI(TAG, "[3] LCD backlight PWM (from cfg)");
    lcd_bl_pwm_bsp_init((uint16_t)(BL_MAX_BRIGHTNESS - g_cfg.brightness));
    status_line("BL OK\n");

    ESP_LOGI(TAG, "[4] LCD panel + LVGL");
    disp_driver_init();
    status_line("LCD/Touch OK\n");

    if (bd->periph.rtc) {
        ESP_LOGI(TAG, "[5] RTC");
        i2c_rtc_setup();
        status_line("RTC OK\n");
    }
    if (bd->periph.imu) {
        ESP_LOGI(TAG, "[5] IMU");
        i2c_imu_setup();
        status_line("IMU OK\n");
    }

    if (bd->periph.adc) {
        ESP_LOGI(TAG, "[6] ADC battery");
        adc_bsp_init();
        status_line("ADC OK\n");
    }

    if (bd->periph.audio) {
        ESP_LOGI(TAG, "[7] Audio MIDI (ES8311 + ES7210 + I2S TDM)");
        if (audio_min_init() == ESP_OK) {
            audio_min_set_volume(g_cfg.audio_volume);
            status_line("MIDI OK\n");
        } else {
            status_line("MIDI FAIL\n");
        }
    }

    if (bd->periph.fan) {
        ESP_LOGI(TAG, "[8] Fan control (PWM + TACH)");
        fan_control_init();
        status_line("FAN OK\n");
    }

    if (bd->periph.sd) {
        ESP_LOGI(TAG, "[9] SD card");
        _sdcard_init();
        status_line("SD OK\n");
    }
    if (bd->periph.button) {
        ESP_LOGI(TAG, "[9] Buttons");
        button_Init();
        status_line("Btn OK\n");
    }
}
