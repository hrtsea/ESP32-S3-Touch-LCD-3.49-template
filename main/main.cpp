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
#include "audio_min.h"

static const char *TAG = "skeleton";

#define LCD_BIT_PER_PIXEL (16)

static SemaphoreHandle_t lvgl_mux        = NULL;
static SemaphoreHandle_t lvgl_flush_semap = NULL;
static uint16_t         *lvgl_dma_buf    = NULL;

static volatile uint32_t fps_frame_count = 0;
static lv_obj_t         *fps_label       = NULL;
static lv_obj_t         *play_btn_label  = NULL;
static esp_lcd_panel_io_handle_t g_panel_io = NULL;
static esp_lcd_panel_handle_t    g_panel    = NULL;
/* rot_state: 0=0deg, 1=90deg CW, 2=180deg, 3=270deg CW.
   At 0/180 the canvas is portrait (172w x 640h);
   at 90/270 the canvas is landscape (640w x 172h). */
static int               rot_state       = 1;
static int               canvas_w        = UI_CANVAS_W;  /* 640 */
static int               canvas_h        = UI_CANVAS_H;  /* 172 */
static lv_disp_drv_t    *g_disp_drv      = NULL;
static char              g_status_text[256];
static void show_hello_world(const char *status_text);

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
    const int PW          = EXAMPLE_LCD_H_RES;     /* panel width  = 172 */
    const int PH          = EXAMPLE_LCD_V_RES;     /* panel height = 640 */
    const int CW          = canvas_w;
    const int CH          = canvas_h;
    const int rs          = rot_state;

    uint16_t *src = (uint16_t *)color_map;
    int x1 = 0, y1 = 0, x2 = PW, y2 = offgap;

    xSemaphoreGive(lvgl_flush_semap);
    for (int i = 0; i < flush_count; i++) {
        xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
        if (y2 > PH) y2 = PH;
        int rows = y2 - y1;
        for (int r = 0; r < rows; r++) {
            int py = y1 + r;
            uint16_t *drow = lvgl_dma_buf + (size_t)r * PW;
            for (int px = 0; px < PW; px++) {
                int cx, cy;
                switch (rs) {
                    case 0:  /* 0deg:  panel(px,py) = canvas(px, py)              */
                        cx = px;       cy = py;            break;
                    case 1:  /* 90 CW: panel(px,py) = canvas(py, CH-1-px)         */
                        cx = py;       cy = CH - 1 - px;   break;
                    case 2:  /* 180:   panel(px,py) = canvas(CW-1-px, CH-1-py)    */
                        cx = CW-1-px;  cy = CH - 1 - py;   break;
                    default: /* 270 CW:panel(px,py) = canvas(CW-1-py, px)         */
                        cx = CW-1-py;  cy = px;            break;
                }
                if ((unsigned)cx < (unsigned)CW && (unsigned)cy < (unsigned)CH) {
                    drow[px] = src[(size_t)cy * CW + cx];
                } else {
                    drow[px] = 0;
                }
            }
        }
        esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, lvgl_dma_buf);
        y1 += offgap;
        y2 += offgap;
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

    uint16_t rx = (((uint16_t)buff[2] & 0x0f) << 8) | buff[3];
    uint16_t ry = (((uint16_t)buff[4] & 0x0f) << 8) | buff[5];

    if (buff[1] > 0 && buff[1] < 5) {
        if (rx > EXAMPLE_LCD_V_RES) rx = EXAMPLE_LCD_V_RES;
        if (ry > EXAMPLE_LCD_H_RES) ry = EXAMPLE_LCD_H_RES;
        /* Raw touch -> portrait panel coords (panel_px in [0,PW), panel_py in [0,PH)). */
        int panel_px = ry;
        int panel_py = EXAMPLE_LCD_V_RES - rx;
        const int PW = EXAMPLE_LCD_H_RES;
        const int PH = EXAMPLE_LCD_V_RES;
        const int CW = canvas_w;
        const int CH = canvas_h;
        int cx, cy;
        /* Inverse of the flush_cb mapping. */
        switch (rot_state) {
            case 0:  cx = panel_px;            cy = panel_py;              break;
            case 1:  cx = panel_py;            cy = CH - 1 - panel_px;     break;
            case 2:  cx = CW - 1 - panel_px;   cy = CH - 1 - panel_py;     break;
            default: cx = CW - 1 - panel_py;   cy = panel_px;              break;
        }
        (void)PW; (void)PH;
        if (cx < 0) cx = 0;
        if (cx >= CW) cx = CW - 1;
        if (cy < 0) cy = 0;
        if (cy >= CH) cy = CH - 1;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = cx;
        data->point.y = cy;
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
    g_panel_io = panel_io;

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
    g_panel = panel;
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
    lv_disp_draw_buf_init(&disp_buf, fb, NULL, UI_CANVAS_W * UI_CANVAS_H);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = canvas_w;
    disp_drv.ver_res      = canvas_h;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &disp_buf;
    disp_drv.full_refresh = 1;
    disp_drv.user_data    = panel;
    lv_disp_drv_register(&disp_drv);
    g_disp_drv = &disp_drv;

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

static void play_btn_event_cb(lv_event_t *e)
{
    (void)e;
    bool now = !audio_min_is_playing();
    audio_min_play_midi(now);
    if (play_btn_label) {
        lv_label_set_text(play_btn_label, now ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }
    ESP_LOGI(TAG, "play toggled -> %s", now ? "PLAY" : "STOP");
}

static void rotate_btn_event_cb(lv_event_t *e)
{
    (void)e;
    /* Cycle 0 -> 90 -> 180 -> 270 -> 0. The canvas dimensions swap
       between portrait (172x640) and landscape (640x172) so the UI
       fills the panel at every rotation. The framebuffer is reused
       (172*640 == 640*172 pixels) so no realloc is needed. */
    rot_state = (rot_state + 1) & 3;
    if (rot_state == 0 || rot_state == 2) {
        canvas_w = EXAMPLE_LCD_H_RES;   /* 172 */
        canvas_h = EXAMPLE_LCD_V_RES;   /* 640 */
    } else {
        canvas_w = UI_CANVAS_W;          /* 640 */
        canvas_h = UI_CANVAS_H;          /* 172 */
    }
    if (g_disp_drv) {
        g_disp_drv->hor_res = canvas_w;
        g_disp_drv->ver_res = canvas_h;
        lv_disp_drv_update(lv_disp_get_default(), g_disp_drv);
    }
    /* Wipe the active screen and rebuild widgets sized for the new canvas. */
    lv_obj_clean(lv_scr_act());
    fps_label = NULL;
    play_btn_label = NULL;
    show_hello_world(g_status_text);
    ESP_LOGI(TAG, "rotate -> %d deg  canvas=%dx%d", rot_state * 90, canvas_w, canvas_h);
}

static void build_hello_world_ui(const char *status_text)
{
    static bool fps_timer_created = false;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *hello = lv_label_create(scr);
    lv_label_set_long_mode(hello, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(hello, canvas_w);
    lv_label_set_text(hello, "Hello World  *  Hello World  *  Hello World  *  ");
    lv_obj_set_style_text_color(hello, lv_color_white(), 0);
    lv_obj_set_style_text_font(hello, &lv_font_montserrat_16, 0);
    lv_obj_set_style_anim_speed(hello, 40, 0);
    lv_obj_set_style_text_align(hello, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(hello, LV_ALIGN_TOP_MID, 0, 8);

    fps_label = lv_label_create(scr);
    lv_label_set_text(fps_label, "FPS --");
    lv_obj_set_style_text_color(fps_label, lv_color_make(0x00, 0xff, 0x80), 0);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_12, 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_RIGHT, -4, 4);

    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status, canvas_w - 20);
    lv_label_set_text(status, status_text);
    lv_obj_set_style_text_color(status, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 0);

    /* Play / Stop button */
    lv_obj_t *play_btn = lv_btn_create(scr);
    lv_obj_set_size(play_btn, 50, 50);
    lv_obj_align(play_btn, LV_ALIGN_BOTTOM_LEFT, 16, -8);
    lv_obj_set_style_radius(play_btn, 25, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_make(0x20, 0x80, 0x40), 0);
    lv_obj_add_event_cb(play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
    play_btn_label = lv_label_create(play_btn);
    lv_label_set_text(play_btn_label, audio_min_is_playing() ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_btn_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(play_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_center(play_btn_label);

    /* Rotate button */
    lv_obj_t *rot_btn = lv_btn_create(scr);
    lv_obj_set_size(rot_btn, 50, 50);
    lv_obj_align(rot_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -8);
    lv_obj_set_style_radius(rot_btn, 30, 0);
    lv_obj_set_style_bg_color(rot_btn, lv_color_make(0x40, 0x40, 0x80), 0);
    lv_obj_add_event_cb(rot_btn, rotate_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rot_label = lv_label_create(rot_btn);
    lv_label_set_text(rot_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rot_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(rot_label, &lv_font_montserrat_16, 0);
    lv_obj_center(rot_label);

    if (!fps_timer_created) {
        lv_timer_create(fps_timer_cb, 500, NULL);
        fps_timer_created = true;
    }
}

static void show_hello_world(const char *status_text)
{
    /* Cache for redraw on rotation. */
    if (status_text != g_status_text) {
        strncpy(g_status_text, status_text, sizeof(g_status_text) - 1);
        g_status_text[sizeof(g_status_text) - 1] = '\0';
    }
    /* If we're already inside the LVGL task (e.g. rebuilding from a button
       event), the mutex is already held; re-locking would deadlock. */
    bool need_lock = (xSemaphoreGetMutexHolder(lvgl_mux) != xTaskGetCurrentTaskHandle());
    if (need_lock && !lvgl_lock(-1)) return;
    build_hello_world_ui(g_status_text);
    if (need_lock) lvgl_unlock();
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

    ESP_LOGI(TAG, "[8/9] Audio (audio_min: ES8311 + I2S)");
    if (audio_min_init() == ESP_OK) {
        pos += snprintf(status + pos, sizeof(status) - pos, "Audio OK\n");
    } else {
        pos += snprintf(status + pos, sizeof(status) - pos, "Audio FAIL\n");
    }

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
