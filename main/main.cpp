#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

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

/* Custom JetBrains Mono Bold fonts, generated via lv_font_conv. */
extern "C" const lv_font_t font_jbmono_64;
extern "C" const lv_font_t font_jbmono_96;

#define LCD_BIT_PER_PIXEL (16)

static SemaphoreHandle_t lvgl_mux        = NULL;
static SemaphoreHandle_t lvgl_flush_semap = NULL;
/* Two DMA-capable strip buffers so the next strip can be transformed
   while the prior strip's SPI DMA is still draining. */
static uint16_t         *lvgl_dma_bufs[2] = { NULL, NULL };

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

/* Tileview-based UI: hello | clock (start) | sunmap. Swipe horizontally. */
static lv_obj_t  *g_tileview         = NULL;
static lv_obj_t  *g_clock_time_label = NULL;
static lv_obj_t  *g_clock_date_label = NULL;
static lv_obj_t  *g_sunmap_canvas    = NULL;
static lv_color_t *g_sunmap_buf      = NULL;
static int        g_sunmap_w         = 0;
static int        g_sunmap_h         = 0;
static lv_timer_t *g_clock_timer     = NULL;
static lv_timer_t *g_sunmap_timer    = NULL;

static void show_main_ui(const char *status_text);

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
    const int PW          = EXAMPLE_LCD_H_RES;     /* panel width  = 172 */
    const int PH          = EXAMPLE_LCD_V_RES;     /* panel height = 640 */
    const int CW          = canvas_w;
    const int CH          = canvas_h;
    const int rs          = rot_state;

    /* direct_mode: color_map points at the full canvas framebuffer
       (LVGL has only redrawn the dirty area, but the rest is already
       valid from the last frame). The AXS15231B in QSPI mode streams
       each frame as a contiguous block starting at y=0, so we must
       always send the whole panel even though only a sliver changed.
       The win is on the LVGL rasterize side: with direct_mode, LVGL
       skips re-rendering the unchanged pixels each frame. */
    (void)area;
    uint16_t *src = (uint16_t *)color_map;

    const int flush_count = (PH + LVGL_FLUSH_STRIP_ROWS - 1) / LVGL_FLUSH_STRIP_ROWS;
    const int offgap      = LVGL_FLUSH_STRIP_ROWS;
    int x1 = 0, y1 = 0, x2 = PW, y2 = offgap;

    int64_t t_frame_start = esp_timer_get_time();
    int64_t t_xform_total = 0;   /* per-pixel transform */
    int64_t t_wait_total  = 0;   /* waiting for a free DMA buffer */
    int64_t t_draw_total  = 0;   /* esp_lcd_panel_draw_bitmap submit time */

    int buf_idx = 0;
    for (int i = 0; i < flush_count; i++) {
        /* Take one slot from the counting semaphore -- we have 2 DMA
           buffers and the SPI-done callback gives a slot back. So this
           only blocks when both buffers are still in flight. */
        int64_t t_w0 = esp_timer_get_time();
        xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
        t_wait_total += esp_timer_get_time() - t_w0;
        uint16_t *lvgl_dma_buf = lvgl_dma_bufs[buf_idx];
        buf_idx ^= 1;
        if (y2 > PH) y2 = PH;
        int rows = y2 - y1;
        int64_t t_x0 = esp_timer_get_time();
        /* Specialized inner loops per rotation. Landscape modes (90/270)
           must iterate `r` (panel row -> canvas column) on the INNER loop
           so each canvas row is read sequentially -- otherwise PSRAM
           cache misses on every pixel drop landscape FPS to ~2. */
        switch (rs) {
            case 0: {  /* 0 deg: portrait, identity copy. */
                int copy_w = (PW < CW ? PW : CW) * 2;
                for (int r = 0; r < rows; r++) {
                    int py = y1 + r;
                    uint16_t *drow = lvgl_dma_buf + (size_t)r * PW;
                    if (py < CH) {
                        memcpy(drow, src + (size_t)py * CW, copy_w);
                        if (PW > CW) memset(drow + CW, 0, (PW - CW) * 2);
                    } else {
                        memset(drow, 0, PW * 2);
                    }
                }
            } break;
            case 1: {  /* 90 CW: drow[px] = src[(CH-1-px)*CW + py].
                          Stage each canvas row span into internal RAM via
                          a single memcpy (sequential PSRAM read), then
                          scatter transposed into the DMA buffer. */
                uint16_t span[LVGL_FLUSH_STRIP_ROWS];
                for (int px = 0; px < PW; px++) {
                    int cy = CH - 1 - px;
                    if ((unsigned)cy >= (unsigned)CH) {
                        for (int r = 0; r < rows; r++)
                            lvgl_dma_buf[(size_t)r * PW + px] = 0;
                        continue;
                    }
                    const uint16_t *crow = src + (size_t)cy * CW + y1;
                    int span_n = (y1 + rows <= CW) ? rows : (CW - y1);
                    if (span_n < 0) span_n = 0;
                    if (span_n > 0) memcpy(span, crow, (size_t)span_n * 2);
                    for (int r = 0; r < span_n; r++)
                        lvgl_dma_buf[(size_t)r * PW + px] = span[r];
                    for (int r = span_n; r < rows; r++)
                        lvgl_dma_buf[(size_t)r * PW + px] = 0;
                }
            } break;
            case 2: {  /* 180: drow[px] = src[(CH-1-py)*CW + (CW-1-px)]. */
                for (int r = 0; r < rows; r++) {
                    int py = y1 + r;
                    int cy = CH - 1 - py;
                    uint16_t *drow = lvgl_dma_buf + (size_t)r * PW;
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
            } break;
            default: { /* 270 CW: drow[px] = src[px*CW + (CW-1-(y1+r))].
                          Stage canvas row span into internal RAM, then
                          scatter transposed in reverse order. */
                uint16_t span[LVGL_FLUSH_STRIP_ROWS];
                for (int px = 0; px < PW; px++) {
                    int cy = px;
                    if ((unsigned)cy >= (unsigned)CH) {
                        for (int r = 0; r < rows; r++)
                            lvgl_dma_buf[(size_t)r * PW + px] = 0;
                        continue;
                    }
                    int cx_end_excl = CW - y1;            /* exclusive */
                    int cx_start    = cx_end_excl - rows; /* may be < 0 */
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
                    /* span[0..span_n) holds canvas pixels in increasing cx;
                       we need decreasing cx, so write in reverse. */
                    for (int r = 0; r < dst_skip; r++)
                        lvgl_dma_buf[(size_t)r * PW + px] = 0;
                    for (int r = 0; r < span_n; r++)
                        lvgl_dma_buf[(size_t)(dst_skip + r) * PW + px] = span[span_n - 1 - r];
                }
            } break;
        }
        t_xform_total += esp_timer_get_time() - t_x0;
        int64_t t_d0 = esp_timer_get_time();
        esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, lvgl_dma_buf);
        t_draw_total += esp_timer_get_time() - t_d0;
        y1 += offgap;
        y2 += offgap;
    }
    /* Drain both DMA buffers before reporting flush_ready to LVGL,
       otherwise LVGL may swap framebuffers while SPI is still sending. */
    int64_t t_w1 = esp_timer_get_time();
    xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
    xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
    /* Restore both slots for the next flush. */
    xSemaphoreGive(lvgl_flush_semap);
    xSemaphoreGive(lvgl_flush_semap);
    t_wait_total += esp_timer_get_time() - t_w1;
    int64_t t_total = esp_timer_get_time() - t_frame_start;
    fps_frame_count++;
    static uint32_t flush_log_div = 0;
    if ((flush_log_div++ % 120) == 0) {  /* ~every 5s at 24 FPS */
        ESP_LOGI(TAG,
                 "flush rs=%d C=%dx%d total=%lldus xform=%lldus draw=%lldus wait=%lldus",
                 rs, CW, CH,
                 (long long)t_total, (long long)t_xform_total,
                 (long long)t_draw_total, (long long)t_wait_total);
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
    static uint32_t print_div = 0;
    if ((print_div++ & 3) == 0) {  /* ~every 2s */
        ESP_LOGI(TAG, "fps=%lu.%lu  (frames=%lu in %lu ms)",
                 (unsigned long)(fps_x10 / 10), (unsigned long)(fps_x10 % 10),
                 (unsigned long)frames, (unsigned long)dt);
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

    /* Counting semaphore: 2 because we have 2 ping-pong DMA buffers.
       Each completed SPI transfer "gives" one slot back. */
    lvgl_flush_semap = xSemaphoreCreateCounting(2, 2);
    lv_init();

    lvgl_dma_bufs[0] = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    lvgl_dma_bufs[1] = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(lvgl_dma_bufs[0] && lvgl_dma_bufs[1]);
    /* Put the LVGL framebuffer in internal RAM. Rasterizing 110 KB of
       16-bit pixels into internal RAM is ~10x faster than into SPIRAM,
       which was the dominant cost in lv_timer_handler. We only have
       headroom for one buffer here -- the other side is the SPI DMA
       which is paced by panel bandwidth, not memory. */
    lv_color_t *fb1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN,
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!fb1) {
        ESP_LOGW(TAG, "fb in internal RAM failed; falling back to SPIRAM");
        fb1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    }
    assert(fb1);
    lv_disp_draw_buf_init(&disp_buf, fb1, NULL, UI_CANVAS_W * UI_CANVAS_H);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = canvas_w;
    disp_drv.ver_res      = canvas_h;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &disp_buf;
    /* direct_mode lets LVGL render only dirty regions into the existing
       full-canvas framebuffer instead of re-rasterizing every pixel each
       frame. flush_cb still gets called with the dirty area, but the
       framebuffer pointer it receives points at the WHOLE canvas, so we
       can read any pixel from it. We then send full panel-width strips
       to the AXS15231B (whose QSPI mode does not honour partial-width
       column windows reliably) but only for the rows the dirty rect
       intersects. */
    disp_drv.full_refresh = 0;
    disp_drv.direct_mode  = 1;
    disp_drv.user_data    = panel;
    lv_disp_drv_register(&disp_drv);
    g_disp_drv = &disp_drv;
    /* Force the LVGL refresh timer to fire every 5 ms so the flush rate is
       paced by SPI/PSRAM instead of LVGL's default ~30 ms cadence. */
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->refr_timer) {
        lv_timer_set_period(disp->refr_timer, 5);
    }

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
    g_tileview = NULL;
    g_clock_time_label = NULL;
    g_clock_date_label = NULL;
    g_sunmap_canvas = NULL;
    if (g_clock_timer)  { lv_timer_del(g_clock_timer);  g_clock_timer  = NULL; }
    if (g_sunmap_timer) { lv_timer_del(g_sunmap_timer); g_sunmap_timer = NULL; }
    show_main_ui(g_status_text);
    ESP_LOGI(TAG, "rotate -> %d deg  canvas=%dx%d", rot_state * 90, canvas_w, canvas_h);
}

/* ---------------------- Clock tile ---------------------- */

static void get_display_time(struct tm *out)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t t = tv.tv_sec + (time_t)(TZ_OFFSET_HOURS * 3600);
    gmtime_r(&t, out);
}

static void clock_update_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_clock_time_label || !g_clock_date_label) return;
    struct tm tm;
    get_display_time(&tm);
    char buf[64];
    int yyyy = tm.tm_year + 1900;
    int mm   = tm.tm_mon + 1;
    int dd   = tm.tm_mday;
    if (yyyy < 0) yyyy = 0;
    if (yyyy > 9999) yyyy = 9999;
    if (mm < 1) mm = 1;
    if (mm > 12) mm = 12;
    if (dd < 1) dd = 1;
    if (dd > 31) dd = 31;
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", yyyy, mm, dd);
    lv_label_set_text(g_clock_date_label, buf);
    int hh = tm.tm_hour, mi = tm.tm_min, ss = tm.tm_sec;
    if (hh < 0) hh = 0;
    if (hh > 23) hh = 23;
    if (mi < 0) mi = 0;
    if (mi > 59) mi = 59;
    if (ss < 0) ss = 0;
    if (ss > 60) ss = 60;
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mi, ss);
    lv_label_set_text(g_clock_time_label, buf);
}

/* Forward decl: sunmap functions used by clock tile. */
static void sunmap_redraw(void);
static void sunmap_update_cb(lv_timer_t *t);

static void build_clock_tile(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* Daylight map: stretch to fill the full screen as the clock
       background. Aspect may not match 2:1 (real Earth) -- we accept
       the squash because the map's job here is mostly atmospheric. */
    int W = canvas_w;
    int H = canvas_h;
    g_sunmap_w = W;
    g_sunmap_h = H;
    if (g_sunmap_buf) { lv_mem_free(g_sunmap_buf); g_sunmap_buf = NULL; }
    g_sunmap_buf = (lv_color_t *)lv_mem_alloc((size_t)W * H * sizeof(lv_color_t));
    if (g_sunmap_buf) {
        g_sunmap_canvas = lv_canvas_create(parent);
        lv_canvas_set_buffer(g_sunmap_canvas, g_sunmap_buf, W, H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(g_sunmap_canvas, LV_ALIGN_CENTER, 0, 0);
        sunmap_redraw();
        if (!g_sunmap_timer) {
            g_sunmap_timer = lv_timer_create(sunmap_update_cb, SUNMAP_RECOMPUTE_MS, NULL);
        }
    }

    /* Date at the top center, transparent bar over the map. */
    g_clock_date_label = lv_label_create(parent);
    lv_label_set_text(g_clock_date_label, "----.--.--");
    lv_obj_set_style_text_color(g_clock_date_label, lv_color_make(0xd0, 0xd0, 0xd0), 0);
    lv_obj_set_style_text_font(g_clock_date_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(g_clock_date_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_date_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(g_clock_date_label, 6, 0);
    lv_obj_set_style_radius(g_clock_date_label, 3, 0);
    lv_obj_align(g_clock_date_label, LV_ALIGN_TOP_MID, 0, 4);

    /* Big time in JetBrains Mono Bold 96, dead center of the screen. */
    g_clock_time_label = lv_label_create(parent);
    lv_label_set_text(g_clock_time_label, "--:--:--");
    lv_obj_set_style_text_color(g_clock_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_clock_time_label, &font_jbmono_96, 0);
    lv_obj_set_style_bg_opa(g_clock_time_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_clock_time_label, 0, 0);
    lv_obj_align(g_clock_time_label, LV_ALIGN_CENTER, 0, 0);

    /* Timezone hint, bottom right. */
    lv_obj_t *tz = lv_label_create(parent);
    lv_label_set_text_fmt(tz, "UTC%+d", TZ_OFFSET_HOURS);
    lv_obj_set_style_text_color(tz, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(tz, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(tz, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tz, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(tz, 4, 0);
    lv_obj_align(tz, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

    if (!g_clock_timer) {
        g_clock_timer = lv_timer_create(clock_update_cb, 500, NULL);
    }
    clock_update_cb(NULL);
}

/* ---------------------- Sunmap tile ---------------------- */

/* Approximate continent outlines as polylines drawn into the canvas.
   Coordinates are normalized lon[-180..180], lat[-90..90] and converted
   to canvas pixels at draw time. Detail level chosen for ~360x120 maps. */
typedef struct { float lon, lat; } sun_pt_t;
typedef struct {
    const sun_pt_t *pts;
    int             n;
} sun_poly_t;

/* Denser hand-tuned continent outlines. Coordinates are (lon, lat) in
   degrees. Each polygon is closed (first vertex repeated at end). Not
   cartographically precise but capture enough features (UK, Iberia,
   Italy, Scandinavia, Arabia, India, Korea, Indochina, Florida, Gulf
   of Mexico, Hudson Bay, Greenland, Madagascar, Japan, NZ, Indonesia)
   to be recognizable at ~580x130 pixels. */

/* Eurasia main mass (without UK, Japan, Indonesia, etc.). */
static const sun_pt_t k_eurasia[] = {
    /* Iberia */
    { -9, 43},{ -9, 38},{ -7, 37},{ -3, 36},{  0, 38},{  3, 42},
    /* France/Italy */
    {  4, 43},{  7, 44},{  9, 44},{ 12, 45},{ 14, 41},{ 18, 40},
    { 17, 38},{ 15, 37},{ 13, 38},{ 12, 44},{ 14, 45},{ 16, 46},
    /* Balkans/Black Sea */
    { 19, 42},{ 23, 41},{ 28, 41},{ 30, 41},{ 31, 45},{ 36, 45},
    { 37, 42},{ 36, 36},{ 35, 35},
    /* Levant/Arabia (part of Asia) */
    { 36, 32},{ 35, 31},{ 34, 30},{ 32, 30},{ 33, 28},{ 35, 25},
    { 38, 23},{ 42, 18},{ 44, 14},{ 49, 12},{ 52, 16},{ 54, 17},
    { 56, 25},{ 50, 27},{ 48, 30},{ 50, 30},{ 56, 27},{ 60, 25},
    /* Iran/Pakistan */
    { 62, 25},{ 67, 25},{ 67, 24},{ 68, 23},
    /* India */
    { 72, 23},{ 73, 17},{ 78, 11},{ 78,  8},{ 80,  9},{ 80, 13},
    { 82, 17},{ 87, 21},{ 89, 22},{ 92, 22},{ 91, 25},
    /* Indochina (mainland) */
    { 95, 22},{ 99, 16},{102, 10},{105, 10},{108, 10},{109, 14},
    {107, 17},{109, 21},
    /* China east coast */
    {110, 22},{114, 23},{117, 24},{121, 28},{121, 32},{122, 37},
    {120, 39},{122, 41},
    /* Korea */
    {126, 35},{127, 38},{129, 39},{128, 42},{130, 43},
    /* NE Asia/Russia far east */
    {131, 45},{135, 49},{140, 53},{144, 57},{152, 60},{160, 62},
    {170, 65},{180, 68},{180, 72},
    /* Russia north coast (Arctic) */
    {165, 72},{140, 75},{105, 78},{ 80, 78},{ 60, 76},{ 40, 73},
    {  20, 72},
    /* Scandinavia */
    { 18, 70},{ 14, 68},{ 12, 65},{  6, 62},{  5, 58},{ 10, 56},
    /* Baltic / NW Europe */
    { 12, 54},{  9, 53},{  6, 53},{  4, 51},{  0, 50},{ -1, 49},
    { -4, 48},{ -5, 44},{ -9, 43}
};

/* British Isles, simplified. */
static const sun_pt_t k_uk[] = {
    { -5, 58},{ -3, 59},{ -1, 58},{  1, 56},{  1, 53},{  0, 51},
    { -2, 50},{ -5, 50},{ -5, 53},{ -3, 55},{ -5, 58}
};

/* Japan, four-island silhouette as a single polygon. */
static const sun_pt_t k_japan[] = {
    {130, 31},{132, 33},{134, 34},{137, 35},{139, 35},{141, 39},
    {142, 42},{145, 44},{144, 41},{141, 38},{138, 36},{135, 34},
    {133, 32},{131, 31},{130, 31}
};

/* Indonesia: a simplified blob covering Sumatra+Java+Borneo+Sulawesi. */
static const sun_pt_t k_indonesia[] = {
    { 95,  5},{ 99,  3},{105, -2},{110, -7},{114, -8},{120, -8},
    {125, -8},{130, -5},{132, -2},{128,  0},{124,  3},{118,  5},
    {115,  4},{112,  2},{108, -3},{104, -1},{100,  3},{ 95,  5}
};

/* New Guinea + nearby. */
static const sun_pt_t k_newguinea[] = {
    {130, -2},{135, -3},{140, -3},{146, -6},{151, -8},{148, -10},
    {142, -10},{137, -8},{132, -5},{130, -2}
};

/* Africa main mass. */
static const sun_pt_t k_africa[] = {
    {-17, 33},{-12, 28},{ -8, 22},{ -3, 18},{  0, 14},{  3,  8},
    {  8,  5},{  9,  4},{  9,  2},{ 10,  0},{ 12, -3},{ 14, -8},
    { 13,-13},{ 12,-17},{ 16,-23},{ 19,-28},{ 23,-32},{ 25,-34},
    { 30,-32},{ 32,-29},{ 32,-25},{ 35,-22},{ 39,-15},{ 41,-11},
    { 41, -2},{ 42,  3},{ 45,  9},{ 48, 11},{ 51, 12},{ 50,  9},
    { 44,  4},{ 42, -3},{ 36, 13},{ 33, 22},{ 30, 26},{ 34, 31},
    { 32, 32},{ 25, 32},{ 18, 31},{ 11, 33},{  3, 35},{ -3, 35},
    {-10, 34},{-17, 33}
};

/* Madagascar. */
static const sun_pt_t k_madagascar[] = {
    { 46, -16},{ 49, -13},{ 50, -16},{ 50, -22},{ 47, -25},
    { 44, -22},{ 44, -19},{ 46, -16}
};

/* North America main mass. */
static const sun_pt_t k_n_america[] = {
    /* Aleutian/Alaska south */
    {-170, 53},{-155, 56},{-150, 60},{-141, 60},
    /* Alaska north */
    {-141, 70},{-156, 71},{-165, 68},
    /* Canada arctic */
    {-160, 72},{-140, 73},{-120, 73},{-100, 75},{ -80, 76},{ -65, 78},
    /* Hudson Bay / Labrador */
    { -65, 70},{ -75, 65},{ -85, 60},{ -82, 55},{ -78, 60},{ -76, 58},
    { -68, 55},{ -55, 53},{ -52, 47},
    /* East coast / New England */
    { -60, 47},{ -68, 44},{ -73, 41},{ -75, 38},{ -76, 36},{ -78, 33},
    /* Florida */
    { -81, 31},{ -81, 25},{ -83, 27},{ -85, 30},
    /* Gulf of Mexico */
    { -90, 29},{ -94, 28},{ -97, 27},{ -97, 26},{ -95, 22},{ -91, 19},
    /* Mexico east */
    { -88, 18},{ -88, 16},
    /* Central America */
    { -83, 12},{ -82,  8},{ -77,  9},
    /* Mexico Pacific */
    { -85, 14},{ -94, 16},{-100, 18},{-105, 22},{-110, 24},{-110, 30},
    /* US Pacific */
    {-117, 33},{-122, 38},{-124, 42},{-124, 48},{-127, 50},{-132, 54},
    {-135, 56},{-145, 60},{-155, 60},{-160, 56},{-170, 53}
};

/* Greenland. */
static const sun_pt_t k_greenland[] = {
    { -55, 60},{ -50, 64},{ -42, 65},{ -34, 67},{ -22, 70},{ -20, 75},
    { -25, 80},{ -45, 82},{ -55, 80},{ -60, 75},{ -55, 70},{ -55, 60}
};

/* South America. */
static const sun_pt_t k_s_america[] = {
    /* North coast */
    { -77,  8},{ -72, 11},{ -68, 11},{ -62, 10},{ -56,  8},{ -52,  4},
    { -50,  1},{ -48, -1},
    /* East coast (Brazil) */
    { -42, -3},{ -35, -8},{ -38,-13},{ -40,-22},{ -47,-25},{ -52,-31},
    { -58,-37},{ -63,-41},{ -68,-46},{ -71,-50},{ -68,-54},
    /* Patagonia south tip */
    { -65,-55},{ -71,-53},
    /* West coast */
    { -73,-50},{ -74,-44},{ -73,-38},{ -71,-30},{ -71,-20},{ -77,-12},
    { -80, -4},{ -80,  0},{ -78,  4},{ -77,  8}
};

/* Australia main mass. */
static const sun_pt_t k_australia[] = {
    {114, -22},{114, -27},{116, -32},{120, -34},{125, -33},{130, -32},
    {137, -35},{140, -38},{144, -38},{149, -37},{152, -32},{153, -28},
    {153, -25},{151, -22},{146, -18},{142, -11},{138, -12},{135, -15},
    {130, -12},{124, -14},{120, -18},{114, -22}
};

/* New Zealand pair. */
static const sun_pt_t k_nz[] = {
    {172, -34},{174, -38},{176, -41},{172, -42},{169, -45},{167, -47},
    {171, -46},{173, -41},{175, -39},{173, -36},{172, -34}
};

/* Antarctica (very rough). */
static const sun_pt_t k_antarctica[] = {
    {-180,-72},{-160,-78},{-130,-74},{-100,-72},{ -75,-72},{ -55,-78},
    { -30,-72},{   0,-70},{  30,-69},{  60,-66},{  90,-66},{ 120,-65},
    { 150,-72},{ 170,-72},{ 180,-78},
    { 180,-90},{-180,-90},{-180,-72}
};

static const sun_poly_t k_continents[] = {
    { k_eurasia,    sizeof(k_eurasia)   /sizeof(sun_pt_t) },
    { k_uk,         sizeof(k_uk)        /sizeof(sun_pt_t) },
    { k_japan,      sizeof(k_japan)     /sizeof(sun_pt_t) },
    { k_indonesia,  sizeof(k_indonesia) /sizeof(sun_pt_t) },
    { k_newguinea,  sizeof(k_newguinea) /sizeof(sun_pt_t) },
    { k_africa,     sizeof(k_africa)    /sizeof(sun_pt_t) },
    { k_madagascar, sizeof(k_madagascar)/sizeof(sun_pt_t) },
    { k_n_america,  sizeof(k_n_america) /sizeof(sun_pt_t) },
    { k_greenland,  sizeof(k_greenland) /sizeof(sun_pt_t) },
    { k_s_america,  sizeof(k_s_america) /sizeof(sun_pt_t) },
    { k_australia,  sizeof(k_australia) /sizeof(sun_pt_t) },
    { k_nz,         sizeof(k_nz)        /sizeof(sun_pt_t) },
    { k_antarctica, sizeof(k_antarctica)/sizeof(sun_pt_t) },
};

/* Even-odd polygon fill into the canvas buffer. */
static void sunmap_fill_poly(lv_color_t *buf, int W, int H,
                              const sun_pt_t *pts, int n,
                              lv_color_t color)
{
    /* Build per-row x-intersection lists. */
    for (int y = 0; y < H; y++) {
        float lat = 90.0f - (y + 0.5f) * (180.0f / H);
        float xs[64];
        int   nx = 0;
        for (int i = 0; i < n - 1; i++) {
            float la = pts[i].lat,     lo_a = pts[i].lon;
            float lb = pts[i+1].lat,   lo_b = pts[i+1].lon;
            if ((la > lat) != (lb > lat)) {
                float t = (lat - la) / (lb - la);
                float lon = lo_a + t * (lo_b - lo_a);
                if (nx < (int)(sizeof(xs)/sizeof(xs[0]))) xs[nx++] = lon;
            }
        }
        /* sort xs */
        for (int i = 1; i < nx; i++) {
            float v = xs[i]; int j = i - 1;
            while (j >= 0 && xs[j] > v) { xs[j+1] = xs[j]; j--; }
            xs[j+1] = v;
        }
        for (int i = 0; i + 1 < nx; i += 2) {
            int x0 = (int)((xs[i]   + 180.0f) / 360.0f * W);
            int x1 = (int)((xs[i+1] + 180.0f) / 360.0f * W);
            if (x0 < 0) x0 = 0;
            if (x1 > W) x1 = W;
            for (int x = x0; x < x1; x++) buf[y * W + x] = color;
        }
    }
}

static void sunmap_redraw(void)
{
    if (!g_sunmap_buf || !g_sunmap_canvas) return;
    const int W = g_sunmap_w;
    const int H = g_sunmap_h;
    const lv_color_t c_night     = lv_color_black();
    const lv_color_t c_land_n    = lv_color_make(0x40, 0x40, 0x40);
    const lv_color_t c_land_d    = lv_color_make(0x90, 0x90, 0x90);
    const lv_color_t c_water_d   = lv_color_make(0x20, 0x20, 0x20);

    /* 1. Fill night background. */
    for (int i = 0; i < W * H; i++) g_sunmap_buf[i] = c_night;

    /* 2. Compute subsolar point in radians. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm tm;
    gmtime_r(&now, &tm);
    int doy = tm.tm_yday;  /* 0..365 */
    float utc_hours = tm.tm_hour + tm.tm_min / 60.0f + tm.tm_sec / 3600.0f;
    float sun_lon_deg = -15.0f * (utc_hours - 12.0f);
    float sun_lat_deg = 23.44f * sinf(2.0f * (float)M_PI * (doy - 81) / 365.0f);
    float sl = sun_lat_deg * (float)M_PI / 180.0f;
    float so = sun_lon_deg * (float)M_PI / 180.0f;

    /* 3. Light water (day side) where sun above horizon. */
    for (int y = 0; y < H; y++) {
        float lat = (90.0f - (y + 0.5f) * (180.0f / H)) * (float)M_PI / 180.0f;
        float sin_lat_p = sinf(lat);
        float cos_lat_p = cosf(lat);
        for (int x = 0; x < W; x++) {
            float lon = (-180.0f + (x + 0.5f) * (360.0f / W)) * (float)M_PI / 180.0f;
            float c = sinf(sl) * sin_lat_p + cosf(sl) * cos_lat_p * cosf(lon - so);
            if (c > 0) g_sunmap_buf[y * W + x] = c_water_d;
        }
    }

    /* 4. Draw continents: night = mid-grey, day = lighter. We do it per
       continent in two passes: first fill polygon over the buffer with a
       sentinel marker, then re-evaluate the day/night for those pixels. */
    /* Simpler: fill with mid-grey, then re-light the day pixels above. */
    for (size_t i = 0; i < sizeof(k_continents)/sizeof(k_continents[0]); i++) {
        sunmap_fill_poly(g_sunmap_buf, W, H,
                         k_continents[i].pts, k_continents[i].n, c_land_n);
    }
    /* Re-light day-side land pixels. */
    for (int y = 0; y < H; y++) {
        float lat = (90.0f - (y + 0.5f) * (180.0f / H)) * (float)M_PI / 180.0f;
        float sin_lat_p = sinf(lat);
        float cos_lat_p = cosf(lat);
        for (int x = 0; x < W; x++) {
            lv_color_t *p = &g_sunmap_buf[y * W + x];
            if (p->full == c_land_n.full) {
                float lon = (-180.0f + (x + 0.5f) * (360.0f / W)) * (float)M_PI / 180.0f;
                float c = sinf(sl) * sin_lat_p + cosf(sl) * cos_lat_p * cosf(lon - so);
                if (c > 0) *p = c_land_d;
            }
        }
    }

    lv_obj_invalidate(g_sunmap_canvas);
}

static void sunmap_update_cb(lv_timer_t *t)
{
    (void)t;
    sunmap_redraw();
}

/* ---------------------- Hello tile (legacy demo) ---------------------- */

static void build_hello_tile(lv_obj_t *parent, const char *status_text)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hello = lv_label_create(parent);
    lv_label_set_long_mode(hello, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(hello, canvas_w / 3);
    lv_label_set_text(hello, "Hello World  *  Hello World  *  Hello World  *  ");
    lv_obj_set_style_text_color(hello, lv_color_white(), 0);
    lv_obj_set_style_text_font(hello, &lv_font_montserrat_16, 0);
    lv_obj_set_style_anim_speed(hello, 40, 0);
    lv_obj_set_style_text_align(hello, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(hello, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *status = lv_label_create(parent);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status, canvas_w - 20);
    lv_label_set_text(status, status_text);
    lv_obj_set_style_text_color(status, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 0);

    /* Play / Stop button */
    lv_obj_t *play_btn = lv_btn_create(parent);
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
    lv_obj_t *rot_btn = lv_btn_create(parent);
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

}

/* ---------------------- Top-level UI builder ---------------------- */

static void build_main_ui(const char *status_text)
{
    static bool fps_timer_created = false;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    g_tileview = lv_tileview_create(scr);
    lv_obj_set_size(g_tileview, canvas_w, canvas_h);
    lv_obj_set_style_bg_color(g_tileview, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_tileview, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(g_tileview, LV_SCROLLBAR_MODE_OFF);

    /* Tile 0: clock with the world daylight map as its background.
       Tile 1: legacy hello-world demo screen (last). */
    lv_obj_t *t_clock = lv_tileview_add_tile(g_tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *t_hello = lv_tileview_add_tile(g_tileview, 1, 0, LV_DIR_LEFT);

    build_clock_tile(t_clock);
    build_hello_tile(t_hello, status_text);

    /* FPS overlay: parented to the screen (not the tileview) so it
       floats above every tile. */
    fps_label = lv_label_create(scr);
    lv_label_set_text(fps_label, "FPS --");
    lv_obj_set_style_text_color(fps_label, lv_color_make(0x00, 0xff, 0x80), 0);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(fps_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fps_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(fps_label, 3, 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_clear_flag(fps_label, LV_OBJ_FLAG_CLICKABLE);

    if (!fps_timer_created) {
        lv_timer_create(fps_timer_cb, 500, NULL);
        fps_timer_created = true;
    }

    /* Start on the clock. */
    lv_obj_set_tile_id(g_tileview, 0, 0, LV_ANIM_OFF);
}

static void show_main_ui(const char *status_text)
{
    /* Cache for redraw on rotation. */
    if (status_text != g_status_text) {
        strncpy(g_status_text, status_text, sizeof(g_status_text) - 1);
        g_status_text[sizeof(g_status_text) - 1] = '\0';
    }
    bool need_lock = (xSemaphoreGetMutexHolder(lvgl_mux) != xTaskGetCurrentTaskHandle());
    if (need_lock && !lvgl_lock(-1)) return;
    build_main_ui(g_status_text);
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

    /* If the RTC is older than the firmware's build time, write the
       build time into the RTC. Each rebuild therefore reseeds the
       clock to within a few seconds of wall time (treated as UTC),
       which avoids the year-2000 default and means the user does
       not have to set the clock manually after a flash. A real-time
       source (NTP, GPS) can later overwrite this. */
    setenv("TZ", "UTC0", 1);
    tzset();
    {
        /* Parse __DATE__ ("Mmm dd yyyy") and __TIME__ ("hh:mm:ss"). */
        const char *bdate = __DATE__;
        const char *btime = __TIME__;
        static const char k_months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        int b_y = 0, b_d = 0, b_h = 0, b_mi = 0, b_s = 0;
        char mon3[4] = { bdate[0], bdate[1], bdate[2], 0 };
        const char *p = strstr(k_months, mon3);
        int b_mo = p ? ((int)(p - k_months) / 3 + 1) : 1;
        b_d = atoi(bdate + 4);          /* day, possibly leading space */
        b_y = atoi(bdate + 7);          /* 4-digit year */
        b_h  = (btime[0]-'0')*10 + (btime[1]-'0');
        b_mi = (btime[3]-'0')*10 + (btime[4]-'0');
        b_s  = (btime[6]-'0')*10 + (btime[7]-'0');

        struct tm bt = {};
        bt.tm_year = b_y - 1900;
        bt.tm_mon  = b_mo - 1;
        bt.tm_mday = b_d;
        bt.tm_hour = b_h;
        bt.tm_min  = b_mi;
        bt.tm_sec  = b_s;
        time_t build_epoch = mktime(&bt);

        RtcDateTime_t r = i2c_rtc_get();
        struct tm rt = {};
        rt.tm_year = (int)r.year - 1900;
        rt.tm_mon  = (int)r.month - 1;
        rt.tm_mday = r.day;
        rt.tm_hour = r.hour;
        rt.tm_min  = r.minute;
        rt.tm_sec  = r.second;
        time_t rtc_epoch = mktime(&rt);

        if (rtc_epoch < build_epoch) {
            ESP_LOGI(TAG, "RTC (%04d-%02d-%02d %02d:%02d:%02d) older than build "
                          "(%04d-%02d-%02d %02d:%02d:%02d), reseeding",
                     r.year, r.month, r.day, r.hour, r.minute, r.second,
                     b_y, b_mo, b_d, b_h, b_mi, b_s);
            i2c_rtc_setTime((uint16_t)b_y, (uint8_t)b_mo, (uint8_t)b_d,
                            (uint8_t)b_h, (uint8_t)b_mi, (uint8_t)b_s);
            r = i2c_rtc_get();
        }

        struct tm tm = {};
        tm.tm_year = (int)r.year - 1900;
        tm.tm_mon  = (int)r.month - 1;
        tm.tm_mday = r.day;
        tm.tm_hour = r.hour;
        tm.tm_min  = r.minute;
        tm.tm_sec  = r.second;
        time_t t = mktime(&tm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "RTC seed: %04d-%02d-%02d %02d:%02d:%02d UTC -> epoch %lld",
                 r.year, r.month, r.day, r.hour, r.minute, r.second, (long long)t);
    }

    show_main_ui(status);
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
