#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "lvgl.h"
#include "esp_lcd_axs15231b.h"
#include "esp_io_expander_tca9554.h"
#include "driver/i2c_master.h"

#include "user_config.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "lcd_bl_pwm_bsp.h"
#include "adc_bsp.h"
#include "sdcard_bsp.h"
#include "button_bsp.h"

static const char *TAG = "skeleton";

#define LCD_BIT_PER_PIXEL (16)

static SemaphoreHandle_t lvgl_mux        = NULL;
static SemaphoreHandle_t lvgl_flush_semap = NULL;
static uint16_t         *lvgl_dma_buf    = NULL;

static volatile uint32_t fps_frame_count = 0;
static lv_obj_t         *fps_label       = NULL;

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 100},
    {0x29, (uint8_t []){0x00}, 0, 100},
};


static bool lvgl_lock(int timeout_ms)
{
    const TickType_t to = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, to) == pdTRUE;
}

static void lvgl_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(lvgl_flush_semap, &hp);
    return hp == pdTRUE;
}

static void lvgl_tick_inc_cb(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    const int flush_count = (EXAMPLE_LCD_V_RES + LVGL_FLUSH_STRIP_ROWS - 1) / LVGL_FLUSH_STRIP_ROWS;
    const int offgap      = LVGL_FLUSH_STRIP_ROWS;
    const int dmalen      = (LVGL_DMA_BUFF_LEN / 2);

    int x1 = 0, y1 = 0, x2 = EXAMPLE_LCD_H_RES, y2 = offgap;
    uint16_t *map = (uint16_t *)color_map;

    xSemaphoreGive(lvgl_flush_semap);
    for (int i = 0; i < flush_count; i++) {
        xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
        if (y2 > EXAMPLE_LCD_V_RES) y2 = EXAMPLE_LCD_V_RES;
        int rows  = y2 - y1;
        int bytes = rows * EXAMPLE_LCD_H_RES * 2;
        memcpy(lvgl_dma_buf, map, bytes);
        esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, lvgl_dma_buf);
        y1 += offgap;
        y2 += offgap;
        map += dmalen;
    }
    xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
    fps_frame_count++;
    if ((fps_frame_count % 60) == 1) {
        ESP_LOGI(TAG, "flush #%lu  area x=%d..%d y=%d..%d",
                 (unsigned long)fps_frame_count,
                 (int)area->x1, (int)area->x2, (int)area->y1, (int)area->y2);
    }
    lv_disp_flush_ready(drv);
}

static void fps_timer_cb(lv_timer_t *t)
{
    static uint32_t last_tick = 0;
    uint32_t now = lv_tick_get();
    uint32_t dt  = now - last_tick;
    if (dt == 0) return;
    uint32_t frames = fps_frame_count;
    fps_frame_count = 0;
    last_tick = now;
    uint32_t fps_x10 = (frames * 10000U) / dt;
    if (fps_label) {
        lv_label_set_text_fmt(fps_label, "FPS %lu.%lu",
                              (unsigned long)(fps_x10 / 10),
                              (unsigned long)(fps_x10 % 10));
    }
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 0x0e, 0, 0, 0};
    uint8_t buff[32] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        i2c_master_write_read_dev(disp_touch_dev_handle, cmd, 11, buff, 32));

    uint16_t px = (((uint16_t)buff[2] & 0x0f) << 8) | buff[3];
    uint16_t py = (((uint16_t)buff[4] & 0x0f) << 8) | buff[5];

    if (buff[1] > 0 && buff[1] < 5) {
        if (px > EXAMPLE_LCD_V_RES) px = EXAMPLE_LCD_V_RES;
        if (py > EXAMPLE_LCD_H_RES) py = EXAMPLE_LCD_H_RES;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = py;
        data->point.y = (EXAMPLE_LCD_V_RES - px);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void lvgl_port_task(void *arg)
{
    uint32_t delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    for (;;) {
        if (lvgl_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_unlock();
        }
        if (delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        else if (delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static esp_lcd_panel_handle_t lcd_init(void)
{
    ESP_LOGI(TAG, "Init LCD reset GPIO");
    gpio_config_t io_conf = {};
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ((uint64_t)1 << EXAMPLE_PIN_NUM_LCD_RST);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io_conf));

    ESP_LOGI(TAG, "Init QSPI bus");
    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num    = EXAMPLE_PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num    = EXAMPLE_PIN_NUM_LCD_DATA1;
    buscfg.data2_io_num    = EXAMPLE_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num    = EXAMPLE_PIN_NUM_LCD_DATA3;
    buscfg.sclk_io_num     = EXAMPLE_PIN_NUM_LCD_PCLK;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    esp_lcd_spi_bus_handle_t spi_bus = (esp_lcd_spi_bus_handle_t)(uintptr_t)LCD_HOST;

    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t    panel    = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num         = EXAMPLE_PIN_NUM_LCD_CS;
    io_config.dc_gpio_num         = -1;
    io_config.spi_mode            = 3;
    io_config.pclk_hz             = 40 * 1000 * 1000;
    io_config.trans_queue_depth   = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready;
    io_config.lcd_cmd_bits        = 32;
    io_config.lcd_param_bits      = 8;
    io_config.flags.quad_mode     = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(spi_bus, &io_config, &panel_io));

    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds                = lcd_init_cmds;
    vendor_config.init_cmds_size           = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_config.vendor_config  = &vendor_config;

    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    return panel;
}

static void lvgl_init(esp_lcd_panel_handle_t panel)
{
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t      disp_drv;
    static lv_indev_drv_t     indev_drv;

    lvgl_flush_semap = xSemaphoreCreateBinary();
    lv_init();

    lvgl_dma_buf = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(lvgl_dma_buf);
    lv_color_t *fb = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    assert(fb);
    lv_disp_draw_buf_init(&disp_buf, fb, NULL, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res      = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &disp_buf;
    disp_drv.full_refresh = 1;
    disp_drv.user_data    = panel;
    lv_disp_drv_register(&disp_drv);

    esp_timer_create_args_t tick_args = {};
    tick_args.callback = &lvgl_tick_inc_cb;
    tick_args.name     = "lvgl_tick";
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", 4000, NULL, 4, NULL, 0);
}

static void show_hello_world(const char *status_text)
{
    if (!lvgl_lock(-1)) return;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *hello = lv_label_create(scr);
    lv_label_set_long_mode(hello, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(hello, EXAMPLE_LCD_H_RES);
    lv_label_set_text(hello, "Hello World  *  Hello World  *  Hello World  *  ");
    lv_obj_set_style_text_color(hello, lv_color_white(), 0);
    lv_obj_set_style_text_font(hello, &lv_font_montserrat_16, 0);
    lv_obj_set_style_anim_speed(hello, 40, 0);
    lv_obj_set_style_text_align(hello, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(hello, LV_ALIGN_CENTER, 0, -40);

    fps_label = lv_label_create(scr);
    lv_label_set_text(fps_label, "FPS --");
    lv_obj_set_style_text_color(fps_label, lv_color_make(0x00, 0xff, 0x80), 0);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_12, 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_RIGHT, -4, 4);

    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status, EXAMPLE_LCD_H_RES - 20);
    lv_label_set_text(status, status_text);
    lv_obj_set_style_text_color(status, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 40);

    lv_timer_create(fps_timer_cb, 500, NULL);

    lvgl_unlock();
}

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);
    ESP_LOGI(TAG, "===== 12_HelloWorld_Skeleton boot =====");
    ESP_LOGI(TAG, "H_RES=%d V_RES=%d  DMA=%d SPIRAM=%d",
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
             LVGL_DMA_BUFF_LEN, LVGL_SPIRAM_BUFF_LEN);

    char status[256];
    int  pos = 0;
    pos += snprintf(status + pos, sizeof(status) - pos, "Drivers:\n");

    ESP_LOGI(TAG, "[1/9] I2C buses");
    i2c_master_Init();
    ESP_LOGI(TAG, "      esp_i2c_bus_handle=%p", esp_i2c_bus_handle);
    pos += snprintf(status + pos, sizeof(status) - pos, "I2C OK\n");

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
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    pos += snprintf(status + pos, sizeof(status) - pos, "TCA9554 OK\n");

    ESP_LOGI(TAG, "[3/9] LCD backlight PWM (full)");
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    pos += snprintf(status + pos, sizeof(status) - pos, "BL OK\n");

    ESP_LOGI(TAG, "[4/9] LCD panel init");
    esp_lcd_panel_handle_t panel = lcd_init();
    ESP_LOGI(TAG, "      panel handle=%p", panel);

    ESP_LOGI(TAG, "[5/9] LVGL init");
    lvgl_init(panel);
    pos += snprintf(status + pos, sizeof(status) - pos, "LCD/Touch OK\n");

    ESP_LOGI(TAG, "[6/9] RTC + IMU");
    i2c_rtc_setup();
    i2c_imu_setup();
    pos += snprintf(status + pos, sizeof(status) - pos, "RTC/IMU OK\n");

    ESP_LOGI(TAG, "[7/9] ADC battery");
    adc_bsp_init();
    pos += snprintf(status + pos, sizeof(status) - pos, "ADC OK\n");

    ESP_LOGI(TAG, "[8/9] Audio codec SKIPPED (IDF 5.2 legacy I2C conflict)");
    pos += snprintf(status + pos, sizeof(status) - pos, "Audio SKIP\n");

    ESP_LOGI(TAG, "[9/9] SD card + Buttons");
    _sdcard_init();
    button_Init();
    pos += snprintf(status + pos, sizeof(status) - pos, "SD/Btn OK");

    show_hello_world(status);
    ESP_LOGI(TAG, "===== All drivers initialized =====");

    uint32_t heartbeat = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        size_t free8  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t freedma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        size_t freespi = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "alive #%lu  frames=%lu  heap8=%u dma=%u spiram=%u",
                 (unsigned long)heartbeat++,
                 (unsigned long)fps_frame_count,
                 (unsigned)free8, (unsigned)freedma, (unsigned)freespi);
    }
}
