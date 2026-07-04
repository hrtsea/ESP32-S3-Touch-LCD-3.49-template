#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
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

static const char *TAG = "disp_driver";

#define LCD_BIT_PER_PIXEL (16)

static SemaphoreHandle_t s_lvgl_mux = NULL;
static SemaphoreHandle_t s_lvgl_flush_semap = NULL;
static uint16_t *s_lvgl_dma_bufs[2] = { NULL, NULL };

volatile uint32_t g_fps_frame_count = 0;
lv_obj_t *g_fps_label = NULL;
static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
int g_rot_state = 1;
int g_canvas_w = UI_CANVAS_W;
int g_canvas_h = UI_CANVAS_H;
static lv_disp_drv_t *s_disp_drv = NULL;

static const axs15231b_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 100},
    {0x29, (uint8_t []){0x00}, 0, 100},
};

bool lvgl_lock(int timeout_ms)
{
    const TickType_t to = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, to) == pdTRUE;
}

void lvgl_unlock(void)
{
    xSemaphoreGive(s_lvgl_mux);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_lvgl_flush_semap, &hp);
    return hp == pdTRUE;
}

static void lvgl_tick_inc_cb(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void flush_rot0(uint16_t *dst, const uint16_t *src,
                       int y1, int rows, int PW, int CW, int CH)
{
    int copy_w = (PW < CW ? PW : CW) * 2;
    for (int r = 0; r < rows; r++) {
        int py = y1 + r;
        uint16_t *drow = dst + (size_t)r * PW;
        if (py < CH) {
            memcpy(drow, src + (size_t)py * CW, copy_w);
            if (PW > CW) memset(drow + CW, 0, (PW - CW) * 2);
        } else {
            memset(drow, 0, PW * 2);
        }
    }
}

static void flush_rot90(uint16_t *dst, const uint16_t *src,
                        int y1, int rows, int PW, int CW, int CH)
{
    uint16_t span[LVGL_FLUSH_STRIP_ROWS];
    for (int px = 0; px < PW; px++) {
        int cy = CH - 1 - px;
        if ((unsigned)cy >= (unsigned)CH) {
            for (int r = 0; r < rows; r++)
                dst[(size_t)r * PW + px] = 0;
            continue;
        }
        const uint16_t *crow = src + (size_t)cy * CW + y1;
        int span_n = (y1 + rows <= CW) ? rows : (CW - y1);
        if (span_n < 0) span_n = 0;
        if (span_n > 0) memcpy(span, crow, (size_t)span_n * 2);
        for (int r = 0; r < span_n; r++)
            dst[(size_t)r * PW + px] = span[r];
        for (int r = span_n; r < rows; r++)
            dst[(size_t)r * PW + px] = 0;
    }
}

static void flush_rot180(uint16_t *dst, const uint16_t *src,
                         int y1, int rows, int PW, int CW, int CH)
{
    for (int r = 0; r < rows; r++) {
        int py = y1 + r;
        int cy = CH - 1 - py;
        uint16_t *drow = dst + (size_t)r * PW;
        if ((unsigned)cy >= (unsigned)CH) {
            memset(drow, 0, PW * 2);
            continue;
        }
        const uint16_t *crow = src + (size_t)cy * CW;
        for (int px = 0; px < PW; px++) {
            int cx = CW - 1 - px;
            drow[px] = ((unsigned)cx < (unsigned)CW) ? crow[cx] : 0;
        }
    }
}

static void flush_rot270(uint16_t *dst, const uint16_t *src,
                         int y1, int rows, int PW, int CW, int CH)
{
    uint16_t span[LVGL_FLUSH_STRIP_ROWS];
    for (int px = 0; px < PW; px++) {
        int cy = px;
        if ((unsigned)cy >= (unsigned)CH) {
            for (int r = 0; r < rows; r++)
                dst[(size_t)r * PW + px] = 0;
            continue;
        }
        int cx_end_excl = CW - y1;
        int cx_start = cx_end_excl - rows;
        int span_n = rows;
        int dst_skip = 0;
        if (cx_start < 0) {
            dst_skip = -cx_start;
            span_n -= dst_skip;
            cx_start = 0;
        }
        if (span_n > 0) {
            const uint16_t *crow = src + (size_t)cy * CW + cx_start;
            memcpy(span, crow, (size_t)span_n * 2);
        }
        for (int r = 0; r < dst_skip; r++)
            dst[(size_t)r * PW + px] = 0;
        for (int r = 0; r < span_n; r++)
            dst[(size_t)(dst_skip + r) * PW + px] = span[span_n - 1 - r];
    }
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    const int PW = EXAMPLE_LCD_H_RES;
    const int PH = EXAMPLE_LCD_V_RES;
    const int CW = g_canvas_w;
    const int CH = g_canvas_h;
    const int rs = g_rot_state;

    (void)area;
    uint16_t *src = (uint16_t *)color_map;

    const int flush_count = (PH + LVGL_FLUSH_STRIP_ROWS - 1) / LVGL_FLUSH_STRIP_ROWS;
    const int offgap = LVGL_FLUSH_STRIP_ROWS;
    int x1 = 0, y1 = 0, x2 = PW, y2 = offgap;

    int64_t t_frame_start = esp_timer_get_time();
    int64_t t_xform_total = 0;
    int64_t t_wait_total = 0;
    int64_t t_draw_total = 0;

    int buf_idx = 0;
    for (int i = 0; i < flush_count; i++) {
        int64_t t_w0 = esp_timer_get_time();
        xSemaphoreTake(s_lvgl_flush_semap, portMAX_DELAY);
        t_wait_total += esp_timer_get_time() - t_w0;
        uint16_t *lvgl_dma_buf = s_lvgl_dma_bufs[buf_idx];
        buf_idx ^= 1;
        if (y2 > PH) y2 = PH;
        int rows = y2 - y1;
        int64_t t_x0 = esp_timer_get_time();

        switch (rs) {
            case 0:  flush_rot0(lvgl_dma_buf, src, y1, rows, PW, CW, CH); break;
            case 1:  flush_rot90(lvgl_dma_buf, src, y1, rows, PW, CW, CH); break;
            case 2:  flush_rot180(lvgl_dma_buf, src, y1, rows, PW, CW, CH); break;
            default: flush_rot270(lvgl_dma_buf, src, y1, rows, PW, CW, CH); break;
        }

        t_xform_total += esp_timer_get_time() - t_x0;
        int64_t t_d0 = esp_timer_get_time();
        esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, lvgl_dma_buf);
        t_draw_total += esp_timer_get_time() - t_d0;
        y1 += offgap;
        y2 += offgap;
    }

    int64_t t_w1 = esp_timer_get_time();
    xSemaphoreTake(s_lvgl_flush_semap, portMAX_DELAY);
    xSemaphoreTake(s_lvgl_flush_semap, portMAX_DELAY);
    xSemaphoreGive(s_lvgl_flush_semap);
    xSemaphoreGive(s_lvgl_flush_semap);
    t_wait_total += esp_timer_get_time() - t_w1;
    int64_t t_total = esp_timer_get_time() - t_frame_start;
    g_fps_frame_count++;
    static uint32_t flush_log_div = 0;
    if ((flush_log_div++ % 120) == 0) {
        ESP_LOGI(TAG,
                 "flush rs=%d C=%dx%d total=%lldus xform=%lldus draw=%lldus wait=%lldus",
                 rs, CW, CH,
                 (long long)t_total, (long long)t_xform_total,
                 (long long)t_draw_total, (long long)t_wait_total);
    }
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static bool s_was_pressed = false;
    static int s_hold_x = 0, s_hold_y = 0;

    uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 0x0e, 0, 0, 0};
    uint8_t buff[32] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        i2c_master_write_read_dev(disp_touch_dev_handle, cmd, 11, buff, 32));

    uint16_t rx = (((uint16_t)buff[2] & 0x0f) << 8) | buff[3];
    uint16_t ry = (((uint16_t)buff[4] & 0x0f) << 8) | buff[5];

    if (buff[1] > 0 && buff[1] < 5) {
        if (rx > EXAMPLE_LCD_V_RES) rx = EXAMPLE_LCD_V_RES;
        if (ry > EXAMPLE_LCD_H_RES) ry = EXAMPLE_LCD_H_RES;
        int panel_px = ry;
        int panel_py = EXAMPLE_LCD_V_RES - rx;
        const int PW = EXAMPLE_LCD_H_RES;
        const int PH = EXAMPLE_LCD_V_RES;
        const int CW = g_canvas_w;
        const int CH = g_canvas_h;
        int cx, cy;

        switch (g_rot_state) {
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

        const int DEAD = 2;
        if (s_was_pressed && abs(cx - s_hold_x) <= DEAD && abs(cy - s_hold_y) <= DEAD) {
            cx = s_hold_x;
            cy = s_hold_y;
        } else {
            s_hold_x = cx;
            s_hold_y = cy;
        }
        s_was_pressed = true;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = cx;
        data->point.y = cy;
    } else {
        s_was_pressed = false;
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
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ((uint64_t)1 << EXAMPLE_PIN_NUM_LCD_RST);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io_conf));

    ESP_LOGI(TAG, "Init QSPI bus");
    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;
    buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
    buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    esp_lcd_spi_bus_handle_t spi_bus = (esp_lcd_spi_bus_handle_t)(uintptr_t)LCD_HOST;

    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 3;
    io_config.pclk_hz = 60 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(spi_bus, &io_config, &panel_io));
    s_panel_io = panel_io;

    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds = s_lcd_init_cmds;
    vendor_config.init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_config.vendor_config = &vendor_config;

    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    s_panel = panel;
    return panel;
}

extern void webui_set_framebuffer(void *fb, int w, int h);

static void lvgl_init(esp_lcd_panel_handle_t panel)
{
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;
    static lv_indev_drv_t indev_drv;

    s_lvgl_flush_semap = xSemaphoreCreateCounting(2, 2);
    lv_init();

    s_lvgl_dma_bufs[0] = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    s_lvgl_dma_bufs[1] = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(s_lvgl_dma_bufs[0] && s_lvgl_dma_bufs[1]);

    lv_color_t *fb1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN,
                                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!fb1) {
        ESP_LOGW(TAG, "fb in internal RAM failed; falling back to SPIRAM");
        fb1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    }
    assert(fb1);

    webui_set_framebuffer(fb1, UI_CANVAS_W, UI_CANVAS_H);
    lv_disp_draw_buf_init(&disp_buf, fb1, NULL, UI_CANVAS_W * UI_CANVAS_H);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = g_canvas_w;
    disp_drv.ver_res = g_canvas_h;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.full_refresh = 0;
    disp_drv.direct_mode = 1;
    disp_drv.user_data = panel;
    lv_disp_drv_register(&disp_drv);
    s_disp_drv = &disp_drv;

    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->refr_timer) {
        lv_timer_set_period(disp->refr_timer, 5);
    }

    esp_timer_create_args_t tick_args = {};
    tick_args.callback = &lvgl_tick_inc_cb;
    tick_args.name = "lvgl_tick";
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    indev_drv.scroll_limit = 18;
    indev_drv.scroll_throw = 25;
    indev_drv.gesture_limit = 80;
    indev_drv.long_press_time = 500;
    indev_drv.long_press_repeat_time = 100;
    lv_indev_drv_register(&indev_drv);

    s_lvgl_mux = xSemaphoreCreateMutex();
    assert(s_lvgl_mux);
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", 8192, NULL, 4, NULL, 0);
}

void disp_driver_update_resolution(void)
{
    if (s_disp_drv) {
        s_disp_drv->hor_res = g_canvas_w;
        s_disp_drv->ver_res = g_canvas_h;
        lv_disp_drv_update(lv_disp_get_default(), s_disp_drv);
    }
}

void disp_driver_init(void)
{
    esp_lcd_panel_handle_t panel = lcd_init();
    lvgl_init(panel);
}

int webui_snapshot_fb(void *out, size_t cap)
{
    if (!out) return -1;
    size_t need = (size_t)g_canvas_w * g_canvas_h * 2;
    if (cap < need) return -1;

    if (!lvgl_lock(50)) return -1;

    lv_disp_t *disp = lv_disp_get_default();
    if (!disp || !disp->driver || !disp->driver->draw_buf) {
        lvgl_unlock();
        return -1;
    }
    const void *src = disp->driver->draw_buf->buf1;
    if (!src) {
        lvgl_unlock();
        return -1;
    }

    memcpy(out, src, need);
    lvgl_unlock();
    return (int)need;
}
