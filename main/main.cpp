#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"

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
#include "radio.h"
#include "recorder.h"
#include "webui.h"
#include "cli.h"
#include "i18n.h"
#include "landmask.h"
#include "tz_cities.h"

#include "ui_common.h"
#include "ui_main.h"
#include "ui_clock.h"   /* g_clock_time_label, used by app_cfg_set_clock_text */
#include "ui_radio.h"   /* radio_engine_warm_at_boot */

static const char *TAG = "skeleton";

/* Custom JetBrains Mono Bold fonts, generated via lv_font_conv. */
extern "C" const lv_font_t font_jbmono_24;
extern "C" const lv_font_t font_jbmono_48;
extern "C" const lv_font_t font_jbmono_64;
extern "C" const lv_font_t font_jbmono_96;

#define LCD_BIT_PER_PIXEL (16)

static SemaphoreHandle_t lvgl_mux        = NULL;
static SemaphoreHandle_t lvgl_flush_semap = NULL;
/* Two DMA-capable strip buffers so the next strip can be transformed
   while the prior strip's SPI DMA is still draining. */
static uint16_t         *lvgl_dma_bufs[2] = { NULL, NULL };

volatile uint32_t fps_frame_count = 0;
lv_obj_t         *fps_label       = NULL;
static esp_lcd_panel_io_handle_t g_panel_io = NULL;
static esp_lcd_panel_handle_t    g_panel    = NULL;
/* rot_state: 0=0deg, 1=90deg CW, 2=180deg, 3=270deg CW.
   At 0/180 the canvas is portrait (172w x 640h);
   at 90/270 the canvas is landscape (640w x 172h). */
int               rot_state       = 1;
int               canvas_w        = UI_CANVAS_W;  /* 640 */
int               canvas_h        = UI_CANVAS_H;  /* 172 */
static lv_disp_drv_t    *g_disp_drv      = NULL;

/* Tiny IP-address tag pinned to the bottom-left corner whenever Wi-Fi
   is connected. Lives on lv_layer_top so it floats above every tile.
   Hidden when disconnected. */
static lv_obj_t  *g_ip_label = NULL;

/* ---------------------- Settings (NVS-backed) ---------------------- */

app_cfg_t g_cfg = {
    .tz_idx     = TZ_DEFAULT_CITY_INDEX,
    .brightness = 255,
    .dim_s      = 8 * 3600,    /* 8 hours; 0 = never */
    .off_s      = 8 * 3600,    /* 8 hours; 0 = never */
    .last_ssid  = {0},
    .hour24     = 1,
    .date_fmt   = 0,
    .show_seconds = 1,
    .show_ms    = 1,
    .audio_enable = 1,
    .audio_volume = 70,
    .theme      = 0,
    .show_fps   = 1,
    .wifi_autoconnect = 1,
    .lang             = 0,
    .clock_x          = -52,    /* historical default in build_clock_tile */
    .clock_y          = 0,
    .clock_size       = 3,      /* L = jbmono_96 */
    .clock_rgba       = 0xFFFFFFFF,  /* white opaque */
    .show_clock       = 1,
    .clock_text       = {0},
    .bg_mode          = 0,
    .bg_refresh_s     = 0,
    .bg_url           = {0},
    .bg_color         = 0x202020FFu,    /* dark gray, opaque */
    .quotes_sym_l     = "xauusd",
    .quotes_sym_r     = "xagusd",
    .quotes_refresh_s = 60,
    .quotes_up_rgba   = 0x33DD66FFu,    /* green */
    .quotes_down_rgba = 0xFF4040FFu,    /* red */
};

static void cfg_load(void);

/* tz_apply_current / tz_current_city_name are defined in ui_clock.c. */
extern "C" void tz_apply_current(void);
extern "C" const char *tz_current_city_name(void);

/* ---------------------- Wi-Fi ---------------------- */

wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];
uint16_t       g_wifi_scan_n = 0;
bool           g_wifi_scanning = false;
static bool           g_wifi_inited = false;
bool           g_wifi_connected = false;
char           g_wifi_curr_ssid[33] = {0};
uint8_t        g_wifi_last_reason = 0;     /* esp_wifi disconnect reason */
int8_t         g_wifi_last_rssi   = 0;
uint32_t       g_wifi_connect_started_ms = 0;
static bool           g_sntp_started     = false;
static time_t         g_last_sntp_sync   = 0;

static void wifi_init_once(void);
static void sntp_start_once(void);

/* ---------------------- Backlight + auto-dim ---------------------- */

uint32_t g_last_activity_ms = 0;
int      g_dim_state        = 0;  /* 0=full, 1=dim, 2=off */

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 100},
    {0x29, (uint8_t []){0x00}, 0, 100},
};


bool lvgl_lock(int timeout_ms)
{
    const TickType_t to = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, to) == pdTRUE;
}

void lvgl_unlock(void)
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

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    /* Tiny dead-zone filter to suppress controller jitter while pressed.
       Held across calls so a stationary finger reports a stable coord. */
    static bool s_was_pressed = false;
    static int  s_hold_x = 0, s_hold_y = 0;

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
        /* While pressed, snap samples within 2 px of the last reported
           point back to that point. Kills the controller's 1-2 px jitter
           so taps don't drift into scroll territory. */
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
    io_config.pclk_hz             = 60 * 1000 * 1000;
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
    /* Hand the framebuffer to the web UI so /screen.bmp can read it. */
    webui_set_framebuffer(fb1, UI_CANVAS_W, UI_CANVAS_H);
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
    /* Touch tuning for iPhone/Android-feel:
       - The 2-px dead-zone in lvgl_touch_cb already kills the
         capacitive controller's stationary jitter, so a tap reports
         a stable point.
       - scroll_limit kept low (8 px) so a deliberate scroll cancels
         the click quickly. LVGL automatically suppresses CLICK on
         release when the press caused any scroll to start.
       - gesture_limit raised so an accidental sideways drift on a
         tap doesn't trigger the tileview swipe gesture.
       - long_press_time slightly raised so a finger that stays pressed
         a beat too long doesn't trigger auto-repeat. */
    indev_drv.scroll_limit            = 18;
    indev_drv.scroll_throw            = 25;
    indev_drv.gesture_limit           = 80;
    indev_drv.long_press_time         = 500;
    indev_drv.long_press_repeat_time  = 100;
    lv_indev_drv_register(&indev_drv);

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", 8192, NULL, 4, NULL, 0);
}

/* Called by ui_main.c after a rotation: push the new canvas dimensions
   into the active display driver so LVGL re-rasters at the new size. */
extern "C" void disp_driver_update_resolution(void)
{
    if (g_disp_drv) {
        g_disp_drv->hor_res = canvas_w;
        g_disp_drv->ver_res = canvas_h;
        lv_disp_drv_update(lv_disp_get_default(), g_disp_drv);
    }
}

/* ---------------------- Settings: NVS impl ---------------------- */

/* Bump when defaults change so existing devices pick up the new
   values on the next flash instead of keeping stale NVS data. */
#define CFG_VERSION  7u

/* Optional compiled-in default Wi-Fi credential. The committed source
   defines empty defaults; if main/wifi_secret.h exists locally
   (gitignored, see wifi_secret.example.h) its values override them so
   the board auto-connects without on-screen-keyboard input. */
#if __has_include("wifi_secret.h")
#  include "wifi_secret.h"
#endif
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID  ""
#endif
#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS  ""
#endif

static void cfg_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t  ver = 0;
    uint16_t tzi = g_cfg.tz_idx;
    int8_t   tz_h_legacy = 0;
    bool     have_legacy_tz_h = false;
    uint8_t  br  = g_cfg.brightness;
    uint16_t ds  = g_cfg.dim_s;
    uint16_t os  = g_cfg.off_s;
    uint8_t  h24 = g_cfg.hour24;
    uint8_t  df  = g_cfg.date_fmt;
    uint8_t  ss  = g_cfg.show_seconds;
    uint8_t  sm  = g_cfg.show_ms;
    uint8_t  ae  = g_cfg.audio_enable;
    uint8_t  av  = g_cfg.audio_volume;
    uint8_t  th  = g_cfg.theme;
    uint8_t  sf  = g_cfg.show_fps;
    uint8_t  wac = g_cfg.wifi_autoconnect;
    uint8_t  lang= g_cfg.lang;
    size_t   sl  = sizeof(g_cfg.last_ssid);
    nvs_get_u8 (h, "ver",       &ver);
    if (nvs_get_u16(h, "tz_idx", &tzi) != ESP_OK) {
        if (nvs_get_i8(h, "tz_h", &tz_h_legacy) == ESP_OK) have_legacy_tz_h = true;
    }
    nvs_get_u8 (h, "bri",       &br);
    nvs_get_u16(h, "dim_s",     &ds);
    nvs_get_u16(h, "off_s",     &os);
    nvs_get_u8 (h, "h24",       &h24);
    nvs_get_u8 (h, "date_fmt",  &df);
    nvs_get_u8 (h, "show_sec",  &ss);
    nvs_get_u8 (h, "show_ms",   &sm);
    nvs_get_u8 (h, "aud_en",    &ae);
    nvs_get_u8 (h, "aud_vol",   &av);
    nvs_get_u8 (h, "theme",     &th);
    nvs_get_u8 (h, "show_fps",  &sf);
    nvs_get_u8 (h, "wifi_ac",   &wac);
    nvs_get_u8 (h, "lang",      &lang);
    /* Clock customization (added later -- missing = use defaults). */
    int16_t  cx = g_cfg.clock_x, cy = g_cfg.clock_y;
    uint8_t  cs = g_cfg.clock_size;
    uint32_t crgba = g_cfg.clock_rgba;
    nvs_get_i16(h, "clk_x",   &cx);
    nvs_get_i16(h, "clk_y",   &cy);
    nvs_get_u8 (h, "clk_sz",  &cs);
    nvs_get_u32(h, "clk_rgba",&crgba);
    uint8_t shc = g_cfg.show_clock;
    nvs_get_u8 (h, "clk_show",&shc);
    size_t ctl = sizeof(g_cfg.clock_text);
    nvs_get_str(h, "clk_text",g_cfg.clock_text, &ctl);
    uint8_t  bgm = g_cfg.bg_mode;
    uint16_t bgr = g_cfg.bg_refresh_s;
    size_t   bgul = sizeof(g_cfg.bg_url);
    nvs_get_u8 (h, "bg_mode",  &bgm);
    nvs_get_u16(h, "bg_refr",  &bgr);
    nvs_get_str(h, "bg_url",   g_cfg.bg_url, &bgul);
    uint32_t bgc = g_cfg.bg_color;
    nvs_get_u32(h, "bg_color", &bgc);
    g_cfg.bg_color = bgc;
    /* Quotes (added in v7; missing keys = use the defaults already in g_cfg). */
    size_t qsll = sizeof(g_cfg.quotes_sym_l);
    size_t qsrl = sizeof(g_cfg.quotes_sym_r);
    nvs_get_str(h, "q_sl",    g_cfg.quotes_sym_l, &qsll);
    nvs_get_str(h, "q_sr",    g_cfg.quotes_sym_r, &qsrl);
    uint16_t qrs = g_cfg.quotes_refresh_s;
    nvs_get_u16(h, "q_refr",  &qrs);
    g_cfg.quotes_refresh_s = qrs;
    uint32_t qu = g_cfg.quotes_up_rgba;
    uint32_t qd = g_cfg.quotes_down_rgba;
    nvs_get_u32(h, "q_up",    &qu);
    nvs_get_u32(h, "q_dn",    &qd);
    g_cfg.quotes_up_rgba   = qu;
    g_cfg.quotes_down_rgba = qd;
    nvs_get_str(h, "last_ssid", g_cfg.last_ssid, &sl);
    nvs_close(h);
    g_cfg.show_clock = shc ? 1 : 0;
    g_cfg.bg_mode = bgm > 3 ? 0 : bgm;
    g_cfg.bg_refresh_s = bgr;
    if (tzi >= TZ_CITY_COUNT) tzi = TZ_DEFAULT_CITY_INDEX;
    if (df > 2)  df  = 0;
    if (th > 2)  th  = 0;
    if (av > 100) av = 100;
    if (ver < CFG_VERSION) {
        ESP_LOGI(TAG, "cfg: migrating from v%u -> v%u",
                 (unsigned)ver, (unsigned)CFG_VERSION);
        if (have_legacy_tz_h) {
            ESP_LOGI(TAG, "cfg: dropping legacy tz_h=%d, defaulting to city %s",
                     (int)tz_h_legacy, k_tz_cities[TZ_DEFAULT_CITY_INDEX].name);
            tzi = TZ_DEFAULT_CITY_INDEX;
        }
        g_cfg.tz_idx     = tzi;
        g_cfg.brightness = br;
        if (DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0]) {
            strncpy(g_cfg.last_ssid, DEFAULT_WIFI_SSID, sizeof(g_cfg.last_ssid) - 1);
            cfg_save_ssid_pass(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
        }
        cfg_save();
        return;
    }
    g_cfg.tz_idx     = tzi;
    g_cfg.brightness = br;
    g_cfg.dim_s      = ds;
    g_cfg.off_s      = os;
    g_cfg.hour24     = h24 ? 1 : 0;
    g_cfg.date_fmt   = df;
    g_cfg.show_seconds = ss ? 1 : 0;
    g_cfg.show_ms    = sm ? 1 : 0;
    g_cfg.audio_enable = ae ? 1 : 0;
    g_cfg.audio_volume = av;
    g_cfg.theme      = th;
    g_cfg.show_fps   = sf ? 1 : 0;
    g_cfg.wifi_autoconnect = wac ? 1 : 0;
    g_cfg.lang = lang;
    if (cs > 3) cs = 3;
    g_cfg.clock_x    = cx;
    g_cfg.clock_y    = cy;
    g_cfg.clock_size = cs;
    /* If alpha is 0, treat as "use default white" rather than fully
       transparent (a never-saved cfg defaults to 0). */
    g_cfg.clock_rgba = ((crgba & 0xFF) == 0) ? 0xFFFFFFFFu : crgba;
}

void cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8 (h, "ver",       CFG_VERSION);
    nvs_set_u16(h, "tz_idx",    g_cfg.tz_idx);
    nvs_set_u8 (h, "bri",       g_cfg.brightness);
    nvs_set_u8 (h, "h24",       g_cfg.hour24);
    nvs_set_u8 (h, "date_fmt",  g_cfg.date_fmt);
    nvs_set_u8 (h, "show_sec",  g_cfg.show_seconds);
    nvs_set_u8 (h, "show_ms",   g_cfg.show_ms);
    nvs_set_u8 (h, "aud_en",    g_cfg.audio_enable);
    nvs_set_u8 (h, "aud_vol",   g_cfg.audio_volume);
    nvs_set_u8 (h, "theme",     g_cfg.theme);
    nvs_set_u8 (h, "show_fps",  g_cfg.show_fps);
    nvs_set_u8 (h, "wifi_ac",   g_cfg.wifi_autoconnect);
    nvs_set_u8 (h, "lang",      g_cfg.lang);
    nvs_set_u16(h, "dim_s",     g_cfg.dim_s);
    nvs_set_u16(h, "off_s",     g_cfg.off_s);
    nvs_set_i16(h, "clk_x",     g_cfg.clock_x);
    nvs_set_i16(h, "clk_y",     g_cfg.clock_y);
    nvs_set_u8 (h, "clk_sz",    g_cfg.clock_size);
    nvs_set_u32(h, "clk_rgba",  g_cfg.clock_rgba);
    nvs_set_u8 (h, "clk_show",  g_cfg.show_clock);
    nvs_set_str(h, "clk_text",  g_cfg.clock_text);
    nvs_set_u8 (h, "bg_mode",   g_cfg.bg_mode);
    nvs_set_u16(h, "bg_refr",   g_cfg.bg_refresh_s);
    nvs_set_str(h, "bg_url",    g_cfg.bg_url);
    nvs_set_u32(h, "bg_color",  g_cfg.bg_color);
    nvs_set_str(h, "q_sl",      g_cfg.quotes_sym_l);
    nvs_set_str(h, "q_sr",      g_cfg.quotes_sym_r);
    nvs_set_u16(h, "q_refr",    g_cfg.quotes_refresh_s);
    nvs_set_u32(h, "q_up",      g_cfg.quotes_up_rgba);
    nvs_set_u32(h, "q_dn",      g_cfg.quotes_down_rgba);
    nvs_set_str(h, "last_ssid", g_cfg.last_ssid);
    nvs_commit(h);
    nvs_close(h);
}

void cfg_save_ssid_pass(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    /* Use first 15 chars of SSID as the key (NVS key max 15 bytes). */
    char key[16] = {0};
    strncpy(key, ssid, 15);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
}

bool cfg_get_ssid_pass(const char *ssid, char *pass, size_t pass_len)
{
    if (!ssid || !*ssid || !pass) return false;
    char key[16] = {0};
    strncpy(key, ssid, 15);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = pass_len;
    esp_err_t er = nvs_get_str(h, key, pass, &l);
    nvs_close(h);
    return er == ESP_OK;
}

/* iPhone-style roaming. After repeated disconnects (or on demand),
   scan the airwaves, look up every visible SSID in the wifi NVS
   namespace, and pick the strongest one we have credentials for. */
static uint8_t  g_wifi_fail_count       = 0;
static bool     g_wifi_roaming_scan     = false;   /* scan was kicked by roam logic */
#define WIFI_FAILS_BEFORE_ROAM 3

/* Look up `ssid` in NVS_NS_WIFI. The key is the SSID prefix (15 chars
   max), same scheme as cfg_save_ssid_pass. Returns true if a
   credential entry exists (password may be empty for open APs). */
static bool wifi_has_remembered(const char *ssid)
{
    if (!ssid || !*ssid) return false;
    char key[16] = {0};
    strncpy(key, ssid, 15);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = 0;
    esp_err_t er = nvs_get_str(h, key, NULL, &l);
    nvs_close(h);
    return er == ESP_OK;
}

/* Kick a roaming scan if we're not already mid-scan and we have at
   least one remembered network configured. */
static void wifi_kick_roam_scan(void)
{
    if (g_wifi_scanning) return;
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    g_wifi_roaming_scan = true;
    g_wifi_scanning = true;
    esp_err_t er = esp_wifi_scan_start(&sc, false);
    ESP_LOGI(TAG, "wifi: roaming scan -> %s", esp_err_to_name(er));
    if (er != ESP_OK) {
        g_wifi_scanning = false;
        g_wifi_roaming_scan = false;
    }
}

/* ---------------------- IP overlay tag ---------------------- */

/* Lazy-create the small IP label on lv_layer_top. Caller must hold
   the lvgl mutex. Safe to call repeatedly. */
static void ip_label_ensure(void)
{
    if (g_ip_label) return;
    g_ip_label = lv_label_create(lv_layer_top());
    lv_label_set_text(g_ip_label, "");
    lv_obj_set_style_text_color(g_ip_label, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(g_ip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(g_ip_label, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(g_ip_label, LV_OPA_50, 0);
    lv_obj_set_style_pad_left(g_ip_label, 4, 0);
    lv_obj_set_style_pad_right(g_ip_label, 4, 0);
    lv_obj_set_style_pad_top(g_ip_label, 1, 0);
    lv_obj_set_style_pad_bottom(g_ip_label, 1, 0);
    lv_obj_set_style_radius(g_ip_label, 3, 0);
    lv_obj_align(g_ip_label, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
}

static void ip_label_set(const char *text)
{
    if (!lvgl_lock(50)) return;
    ip_label_ensure();
    if (text && *text) {
        lv_label_set_text(g_ip_label, text);
        lv_obj_clear_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
}

/* ---------------------- Wi-Fi impl ---------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "wifi: STA_START");
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *d =
                    (wifi_event_sta_disconnected_t *)data;
                g_wifi_last_reason = d ? d->reason : 0;
                ESP_LOGW(TAG, "wifi: disconnected reason=%u",
                         (unsigned)g_wifi_last_reason);
                g_wifi_connected = false;
                ip_label_set(NULL);
                /* Roaming: count consecutive disconnects. After
                   WIFI_FAILS_BEFORE_ROAM, kick a scan and let the
                   SCAN_DONE handler pick a known SSID -- iPhone-style
                   "find any remembered network and connect". On every
                   successful connect g_wifi_fail_count resets. */
                g_wifi_fail_count++;
                if (g_wifi_curr_ssid[0] && !g_wifi_scanning &&
                    g_wifi_fail_count < WIFI_FAILS_BEFORE_ROAM) {
                    esp_wifi_connect();
                } else if (g_cfg.wifi_autoconnect && !g_wifi_scanning) {
                    wifi_kick_roam_scan();
                }
                break;
            }
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "wifi: connected to %s", g_wifi_curr_ssid);
                g_wifi_connected = true;
                g_wifi_last_reason = 0;
                g_wifi_fail_count = 0;
                /* Persist this SSID as the auto-connect target. */
                strncpy(g_cfg.last_ssid, g_wifi_curr_ssid, sizeof(g_cfg.last_ssid) - 1);
                cfg_save();
                break;
            case WIFI_EVENT_SCAN_DONE: {
                /* Keep recs[] off the sys_evt task stack -- it's only
                   ~2 KB and a wifi_ap_record_t array of 16 entries is
                   ~1.7 KB on its own, blowing the stack. */
                static wifi_ap_record_t recs[WIFI_MAX_SCAN_AP];
                uint16_t apc = WIFI_MAX_SCAN_AP;
                if (esp_wifi_scan_get_ap_records(&apc, recs) == ESP_OK) {
                    g_wifi_scan_n = apc;
                    for (int i = 0; i < apc; i++) {
                        strncpy(g_wifi_scan[i].ssid, (const char *)recs[i].ssid,
                                sizeof(g_wifi_scan[i].ssid) - 1);
                        g_wifi_scan[i].ssid[sizeof(g_wifi_scan[i].ssid) - 1] = 0;
                        g_wifi_scan[i].rssi = recs[i].rssi;
                        g_wifi_scan[i].auth = (uint8_t)recs[i].authmode;
                    }
                } else {
                    g_wifi_scan_n = 0;
                }
                ESP_LOGI(TAG, "wifi: scan done, n=%u", (unsigned)g_wifi_scan_n);
                g_wifi_scanning = false;
                /* Roaming pass: pick the strongest visible AP we have
                   credentials for. Resets fail_count to 0 so the next
                   disconnect goes through the normal single-retry path. */
                if (g_wifi_roaming_scan) {
                    g_wifi_roaming_scan = false;
                    int best_i = -1;
                    int best_rssi = -127;
                    for (int i = 0; i < g_wifi_scan_n; i++) {
                        if (!wifi_has_remembered(g_wifi_scan[i].ssid)) continue;
                        if (g_wifi_scan[i].rssi > best_rssi) {
                            best_rssi = g_wifi_scan[i].rssi;
                            best_i = i;
                        }
                    }
                    if (best_i >= 0) {
                        const char *ssid = g_wifi_scan[best_i].ssid;
                        char pass[65] = {0};
                        cfg_get_ssid_pass(ssid, pass, sizeof(pass));
                        ESP_LOGI(TAG, "wifi: roaming to known %s rssi=%d",
                                 ssid, best_rssi);
                        g_wifi_fail_count = 0;   /* fresh attempt */
                        wifi_connect(ssid, pass);
                        break;
                    } else {
                        /* No remembered network visible. Reset the
                           failure counter so we don't pin in scan-loop;
                           let the disconnect handler retry the current
                           SSID periodically. */
                        ESP_LOGI(TAG, "wifi: no remembered AP visible (%u seen)",
                                 (unsigned)g_wifi_scan_n);
                        g_wifi_fail_count = 0;
                    }
                }
                /* Normal user-initiated scan path: resume reconnect to
                   the configured SSID if one is set. */
                if (g_wifi_curr_ssid[0] && !g_wifi_connected) {
                    esp_wifi_connect();
                }
                break;
            }
            default: break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "wifi: got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        /* Start SNTP once we actually have routable network. */
        sntp_start_once();
        /* Pin the IP at the bottom-left so the user knows the webui URL
           without grabbing the CLI. */
        char buf[40];
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ev->ip_info.ip));
        ip_label_set(buf);
    }
}

static void wifi_init_once(void)
{
    if (g_wifi_inited) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());
    g_wifi_inited = true;
}

void wifi_start_scan(void)
{
    wifi_init_once();
    /* Disconnect any in-flight association first; the radio can't scan
       while it's repeatedly retrying a failed connect (NO_AP_FOUND etc).
       g_wifi_scanning suppresses the disconnect-handler's auto-reconnect
       so we don't fight ourselves. */
    g_wifi_scanning = true;
    esp_wifi_disconnect();
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    sc.scan_type   = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t er = esp_wifi_scan_start(&sc, false);
    if (er != ESP_OK) {
        ESP_LOGW(TAG, "wifi: scan_start=%s", esp_err_to_name(er));
        g_wifi_scanning = false;  /* failed to start; allow reconnects */
    }
}

void wifi_connect(const char *ssid, const char *pass)
{
    wifi_init_once();
    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass ? pass : "", sizeof(wc.sta.password) - 1);
    /* If a password was supplied accept anything WPA2-and-above; if
       none, allow open APs only. The hard-coded OPEN threshold from
       earlier silently rejected every protected AP. */
    wc.sta.threshold.authmode = (pass && pass[0])
                                    ? WIFI_AUTH_WPA2_PSK
                                    : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable    = true;
    wc.sta.pmf_cfg.required   = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    strncpy(g_wifi_curr_ssid, ssid, sizeof(g_wifi_curr_ssid) - 1);
    g_wifi_curr_ssid[sizeof(g_wifi_curr_ssid) - 1] = 0;
    g_wifi_connect_started_ms = lv_tick_get();
    g_wifi_last_reason = 0;
    esp_wifi_disconnect();
    esp_err_t er = esp_wifi_connect();
    ESP_LOGI(TAG, "wifi: connect %s pass_len=%u auth=%d -> %s",
             ssid, (unsigned)(pass ? strlen(pass) : 0),
             (int)wc.sta.threshold.authmode, esp_err_to_name(er));
}

/* Public language getters/setters used by i18n.c. Defined here because
   g_cfg is otherwise file-static. */
/* For the webui /screen.bmp endpoint: snapshot the current framebuffer
   into the caller's buffer under the lvgl mutex, so the encoder reads
   a stable copy instead of fighting the panel flush mid-write. */
extern "C" int webui_snapshot_fb(void *out, size_t cap);
extern "C" int webui_snapshot_fb(void *out, size_t cap)
{
    extern uint16_t *lvgl_dma_bufs[2];   /* unused but documents intent */
    (void)lvgl_dma_bufs;
    /* The framebuffer is the same buffer LVGL hands to the panel via
       flush_cb. Reading it under the mutex blocks LVGL from rendering
       a new frame for ~6 ms (one memcpy of 220 KB at PSRAM speed). */
    if (!out) return -1;
    size_t need = (size_t)UI_CANVAS_W * UI_CANVAS_H * 2;
    if (cap < need) return -1;
    /* The fb1 pointer was captured by webui_set_framebuffer at init.
       We re-grab it here from the LVGL display driver. */
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp || !disp->driver || !disp->driver->draw_buf) return -1;
    const void *src = disp->driver->draw_buf->buf1;
    if (!src) return -1;
    if (!lvgl_lock(50)) return -1;
    memcpy(out, src, need);
    lvgl_unlock();
    return (int)need;
}

extern "C" int  app_cfg_get_lang(void) { return g_cfg.lang; }
extern "C" int  app_cfg_get_brightness(void) { return g_cfg.brightness; }
extern "C" int  app_cfg_get_dim_s(void)      { return g_cfg.dim_s; }
extern "C" int  app_cfg_get_off_s(void)      { return g_cfg.off_s; }

/* Clock face: getters return the live cfg, setters apply + persist
   and re-run clock_apply_layout so the change is visible immediately.
   Bounds-check x/y to keep the label on-screen (the screen is 640x172
   so these are generous to allow off-edge positioning if the user
   really wants it). */
extern "C" int  app_cfg_get_clock_x(void)    { return g_cfg.clock_x; }
extern "C" int  app_cfg_get_clock_y(void)    { return g_cfg.clock_y; }
extern "C" int  app_cfg_get_clock_size(void) { return g_cfg.clock_size; }
extern "C" uint32_t app_cfg_get_clock_rgba(void) { return g_cfg.clock_rgba; }
extern "C" int  app_cfg_get_show_ms(void)    { return g_cfg.show_ms; }
extern "C" int  app_cfg_get_show_seconds(void) { return g_cfg.show_seconds; }
extern "C" void app_cfg_set_show_seconds(int show)
{
    g_cfg.show_seconds = show ? 1 : 0;
    cfg_save();
    /* The clock_update_cb reads show_seconds each tick; no relayout
       needed because the time label width changes naturally. */
}
extern "C" int  app_cfg_get_show_clock(void) { return g_cfg.show_clock; }
extern "C" void app_cfg_set_show_clock(int show)
{
    g_cfg.show_clock = show ? 1 : 0;
    if (lvgl_lock(50)) { clock_apply_layout(); lvgl_unlock(); }
    cfg_save();
}
extern "C" const char *app_cfg_get_clock_text(void) { return g_cfg.clock_text; }

/* Background mode: 0=sunmap, 1=custom upload, 2=URL fetch. */
extern "C" int  app_cfg_get_bg_mode(void)        { return g_cfg.bg_mode; }
extern "C" int  app_cfg_get_bg_refresh_s(void)   { return g_cfg.bg_refresh_s; }
extern "C" const char *app_cfg_get_bg_url(void)  { return g_cfg.bg_url; }
extern "C" int  app_cfg_get_canvas_w(void)       { return canvas_w; }
extern "C" int  app_cfg_get_canvas_h(void)       { return canvas_h; }
extern "C" void app_cfg_set_bg_mode(int m)
{
    if (m < 0) m = 0;
    if (m > 3) m = 3;
    g_cfg.bg_mode = (uint8_t)m;
    if (lvgl_lock(50)) { clock_bg_apply(); lvgl_unlock(); }
    cfg_save();
    if (m == 2) bg_fetcher_ensure();
}
extern "C" void app_cfg_set_bg_url(const char *url)
{
    if (!url) url = "";
    strncpy(g_cfg.bg_url, url, sizeof(g_cfg.bg_url) - 1);
    g_cfg.bg_url[sizeof(g_cfg.bg_url) - 1] = 0;
    cfg_save();
    if (g_cfg.bg_mode == 2) bg_fetcher_ensure();
}
extern "C" uint32_t app_cfg_get_bg_color(void) { return g_cfg.bg_color; }
extern "C" void app_cfg_set_bg_color(uint32_t rgba)
{
    g_cfg.bg_color = rgba ? rgba : 0x202020FFu;
    if (g_cfg.bg_mode == 3) {
        if (lvgl_lock(50)) { clock_bg_apply(); lvgl_unlock(); }
    }
    cfg_save();
}
extern "C" void app_cfg_set_bg_refresh_s(int s)
{
    if (s < 0) s = 0;
    if (s > 24 * 3600) s = 24 * 3600;
    g_cfg.bg_refresh_s = (uint16_t)s;
    cfg_save();
}
/* webui calls this after writing the uploaded raw RGB565 to
   /sdcard/clock_bg.bin so the next clock_bg_apply picks it up. */
extern "C" void app_cfg_clock_bg_reload(void)
{
    if (lvgl_lock(50)) { clock_bg_apply(); lvgl_unlock(); }
}
/* Manual "fetch now" trigger for the URL background mode. If a fetcher
   task is already running we just kick it; otherwise we spawn one,
   which now fetches before sleeping. */
extern "C" void app_cfg_bg_fetch_now(void)
{
    if (g_cfg.bg_mode != 2 || !g_cfg.bg_url[0]) return;
    bg_fetcher_ensure();
}

/* --- Quotes tile config accessors. The poll task reads g_cfg.* directly;
       these setters just persist and (for the symbol/refresh changes) wake
       the task so the next fetch picks up the new values immediately. */
extern "C" const char *app_cfg_get_quotes_sym_l(void)  { return g_cfg.quotes_sym_l; }
extern "C" const char *app_cfg_get_quotes_sym_r(void)  { return g_cfg.quotes_sym_r; }
extern "C" int  app_cfg_get_quotes_refresh_s(void)     { return g_cfg.quotes_refresh_s; }
extern "C" uint32_t app_cfg_get_quotes_up_rgba(void)   { return g_cfg.quotes_up_rgba; }
extern "C" uint32_t app_cfg_get_quotes_down_rgba(void) { return g_cfg.quotes_down_rgba; }
extern "C" void app_cfg_set_quotes_sym_l(const char *s)
{
    if (!s) s = "";
    strncpy(g_cfg.quotes_sym_l, s, sizeof(g_cfg.quotes_sym_l) - 1);
    g_cfg.quotes_sym_l[sizeof(g_cfg.quotes_sym_l) - 1] = 0;
    cfg_save();
    quotes_kick();
}
extern "C" void app_cfg_set_quotes_sym_r(const char *s)
{
    if (!s) s = "";
    strncpy(g_cfg.quotes_sym_r, s, sizeof(g_cfg.quotes_sym_r) - 1);
    g_cfg.quotes_sym_r[sizeof(g_cfg.quotes_sym_r) - 1] = 0;
    cfg_save();
    quotes_kick();
}
extern "C" void app_cfg_set_quotes_refresh_s(int s)
{
    if (s < 5)    s = 5;        /* avoid hammering the server */
    if (s > 3600) s = 3600;
    g_cfg.quotes_refresh_s = (uint16_t)s;
    cfg_save();
}
extern "C" void app_cfg_set_quotes_up_rgba(uint32_t v)
{
    g_cfg.quotes_up_rgba = v;
    cfg_save();
}
extern "C" void app_cfg_set_quotes_down_rgba(uint32_t v)
{
    g_cfg.quotes_down_rgba = v;
    cfg_save();
}

/* Set the active tileview tile (0..4). Used by webui /api/goto so tests
   and remote control can flip between Clock/Quotes/Settings/Radio/Recorder. */
extern "C" void app_cfg_set_active_tile(int idx)
{
    if (!g_tileview) return;
    if (idx < 0) idx = 0;
    if (idx > 4) idx = 4;
    if (lvgl_lock(200)) {
        lv_obj_set_tile_id(g_tileview, idx, 0, LV_ANIM_OFF);
        lvgl_unlock();
    }
}
extern "C" void app_cfg_set_clock_text(const char *s)
{
    if (!s) s = "";
    strncpy(g_cfg.clock_text, s, sizeof(g_cfg.clock_text) - 1);
    g_cfg.clock_text[sizeof(g_cfg.clock_text) - 1] = 0;
    /* Setting a non-empty text means "show this on the clock face"
       -- auto-enable show_clock so the user doesn't have to also
       toggle the visibility checkbox. */
    if (g_cfg.clock_text[0]) g_cfg.show_clock = 1;
    if (lvgl_lock(50)) {
        if (g_cfg.clock_text[0] && g_clock_time_label) {
            lv_label_set_text(g_clock_time_label, g_cfg.clock_text);
        }
        clock_apply_layout();
        lvgl_unlock();
    }
    cfg_save();
}
extern "C" void app_cfg_set_clock_pos(int x, int y)
{
    if (x < -512) x = -512;
    if (x >  512) x =  512;
    if (y < -256) y = -256;
    if (y >  256) y =  256;
    g_cfg.clock_x = (int16_t)x;
    g_cfg.clock_y = (int16_t)y;
    if (lvgl_lock(50)) { clock_apply_layout(); lvgl_unlock(); }
    cfg_save();
}
extern "C" void app_cfg_set_clock_size(int sz)
{
    if (sz < 0) sz = 0;
    if (sz > 3) sz = 3;
    g_cfg.clock_size = (uint8_t)sz;
    if (lvgl_lock(50)) { clock_apply_layout(); lvgl_unlock(); }
    cfg_save();
}
extern "C" void app_cfg_set_clock_rgba(uint32_t rgba)
{
    g_cfg.clock_rgba = rgba ? rgba : 0xFFFFFFFFu;
    if (lvgl_lock(50)) { clock_apply_layout(); lvgl_unlock(); }
    cfg_save();
}
extern "C" void app_cfg_set_show_ms(int show)
{
    g_cfg.show_ms = show ? 1 : 0;
    if (lvgl_lock(50)) { clock_apply_layout(); lvgl_unlock(); }
    cfg_save();
}
extern "C" void app_cfg_set_lang(int lang)
{
    if (lang < 0) lang = 0;
    if (lang >= I18N_LANG_COUNT) lang = 0;
    g_cfg.lang = (uint8_t)lang;
    cfg_save();
}

extern "C" void app_cfg_set_brightness(int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_cfg.brightness = (uint8_t)v;
    g_dim_state = 0;
    g_last_activity_ms = lv_tick_get();
    backlight_apply(g_cfg.brightness);
    cfg_save();
}

extern "C" void app_cfg_set_dim_off(int dim_s, int off_s)
{
    if (dim_s < 0) dim_s = 0;
    if (off_s < 0) off_s = 0;
    g_cfg.dim_s = (uint16_t)dim_s;
    g_cfg.off_s = (uint16_t)off_s;
    g_dim_state = 0;
    g_last_activity_ms = lv_tick_get();
    backlight_apply(g_cfg.brightness);
    cfg_save();
}

/* Public helper for cli.c: persist creds and kick a connect attempt. */
extern "C" void app_wifi_connect_save(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    cfg_save_ssid_pass(ssid, pass ? pass : "");
    strncpy(g_cfg.last_ssid, ssid, sizeof(g_cfg.last_ssid) - 1);
    g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
    cfg_save();
    wifi_connect(ssid, pass ? pass : "");
}

/* ---------------------- SNTP ---------------------- */

/* Re-sync interval (ms). 4 hours by default. */
#define SNTP_SYNC_INTERVAL_MS  (4UL * 3600UL * 1000UL)

static void sntp_sync_notification_cb(struct timeval *tv)
{
    if (!tv) return;
    g_last_sntp_sync = tv->tv_sec;
    struct tm tm;
    gmtime_r(&g_last_sntp_sync, &tm);
    ESP_LOGI(TAG, "sntp: synced to %04d-%02d-%02d %02d:%02d:%02d UTC",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    /* Write the corrected time back to the PCF85063 RTC so the
       battery-backed clock survives power cycles in sync. */
    i2c_rtc_setTime((uint16_t)(tm.tm_year + 1900),
                    (uint8_t)(tm.tm_mon + 1),
                    (uint8_t)tm.tm_mday,
                    (uint8_t)tm.tm_hour,
                    (uint8_t)tm.tm_min,
                    (uint8_t)tm.tm_sec);
}

static void sntp_start_once(void)
{
    if (g_sntp_started) return;
    g_sntp_started = true;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    sntp_set_sync_interval(SNTP_SYNC_INTERVAL_MS);
    esp_sntp_init();
    ESP_LOGI(TAG, "sntp: started, server=pool.ntp.org, interval=%lus",
             (unsigned long)(SNTP_SYNC_INTERVAL_MS / 1000));
}

/* ---------------------- Backlight + auto-dim impl ---------------------- */

void backlight_apply(uint8_t bri)
{
    /* setUpduty takes the inverted PWM value: 0xFF = off, 0x00 = full.
       g_cfg.brightness uses 0..255 with 255 = full, so invert. */
    setUpduty((uint16_t)(0xFF - bri));
}

/* ---------------------- Theme palette ---------------------- */

theme_palette_t theme_get(void)
{
    theme_palette_t p;
    switch (g_cfg.theme) {
    case 1:  /* light */
        p.bg             = lv_color_make(0xf0, 0xf0, 0xf4);
        p.text           = lv_color_make(0x10, 0x10, 0x18);
        p.menu_surf      = lv_color_make(0xe8, 0xe8, 0xee);
        p.menu_hdr       = lv_color_make(0xc0, 0xc0, 0xcc);
        p.menu_btn       = lv_color_make(0x90, 0x90, 0xa0);
        p.sunmap_water_n = lv_color_make(0xb0, 0xb8, 0xc8);
        p.sunmap_water_d = lv_color_make(0xe0, 0xe4, 0xf0);
        p.sunmap_land_n  = lv_color_make(0x60, 0x70, 0x80);
        p.sunmap_land_d  = lv_color_make(0x20, 0x30, 0x40);
        break;
    case 2:  /* high contrast */
        p.bg             = lv_color_black();
        p.text           = lv_color_make(0xff, 0xff, 0x00);
        p.menu_surf      = lv_color_black();
        p.menu_hdr       = lv_color_make(0xff, 0xff, 0x00);
        p.menu_btn       = lv_color_white();
        p.sunmap_water_n = lv_color_black();
        p.sunmap_water_d = lv_color_make(0x40, 0x40, 0x00);
        p.sunmap_land_n  = lv_color_make(0x80, 0x80, 0x00);
        p.sunmap_land_d  = lv_color_make(0xff, 0xff, 0x00);
        break;
    default: /* dark */
        p.bg             = lv_color_black();
        p.text           = lv_color_white();
        p.menu_surf      = lv_color_make(0x20, 0x20, 0x28);
        p.menu_hdr       = lv_color_make(0x30, 0x30, 0x3c);
        p.menu_btn       = lv_color_make(0x50, 0x50, 0x60);
        p.sunmap_water_n = lv_color_black();
        p.sunmap_water_d = lv_color_make(0x20, 0x20, 0x20);
        p.sunmap_land_n  = lv_color_make(0x40, 0x40, 0x40);
        p.sunmap_land_d  = lv_color_make(0x90, 0x90, 0x90);
        break;
    }
    return p;
}

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);

    /* Persisted user settings: load early so brightness/timezone are
       applied before the UI builds. NVS init must happen before
       Wi-Fi (esp_wifi requires it) and before any cfg_load(). */
    {
        esp_err_t er = nvs_flash_init();
        if (er == ESP_ERR_NVS_NO_FREE_PAGES || er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            er = nvs_flash_init();
        }
        ESP_ERROR_CHECK(er);
    }
    cfg_load();
    ESP_LOGI(TAG, "cfg: tz=%s (%s) bri=%u dim=%us off=%us last_ssid=%s",
             tz_current_city_name(), k_tz_cities[g_cfg.tz_idx].posix_tz,
             g_cfg.brightness, g_cfg.dim_s, g_cfg.off_s,
             g_cfg.last_ssid[0] ? g_cfg.last_ssid : "(none)");
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

    ESP_LOGI(TAG, "[3/9] LCD backlight PWM (from cfg)");
    lcd_bl_pwm_bsp_init((uint16_t)(0xFF - g_cfg.brightness));
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
        audio_min_set_volume(g_cfg.audio_volume);
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
        /* BUILD_EPOCH_UTC is injected by CMake (string(TIMESTAMP ... UTC)) so
           the seed is real UTC regardless of the build machine's timezone. */
        time_t build_epoch = (time_t)BUILD_EPOCH_UTC;
        struct tm bt = {};
        gmtime_r(&build_epoch, &bt);
        int b_y  = bt.tm_year + 1900;
        int b_mo = bt.tm_mon + 1;
        int b_d  = bt.tm_mday;
        int b_h  = bt.tm_hour;
        int b_mi = bt.tm_min;
        int b_s  = bt.tm_sec;

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

    /* Switch from the UTC0 used during RTC seeding to the user's selected
       city; localtime_r() now returns local wall-clock for the clock face. */
    tz_apply_current();

    /* Bring up Wi-Fi *before* the LVGL widget tree is built. esp_wifi_init
       needs ~5 DMA-capable rx buffers from internal RAM; with ~250 KiB total
       and the menu's many sub-pages it ran out of buffers if we initialised
       LVGL first. The connect itself still happens later. */
    wifi_init_once();

    /* Start the web control panel BEFORE LVGL builds the widget tree.
       After LVGL is up the internal heap is fragmented and httpd
       fails to allocate its task stack (ESP_ERR_HTTPD_TASK). The
       server doesn't need an IP to start -- it just listens on
       socket(80) and routes when traffic arrives. */
    if (webui_start() != ESP_OK) {
        ESP_LOGW(TAG, "webui_start failed");
    }

    show_main_ui(status);
    ESP_LOGI(TAG, "===== All drivers initialized =====");
    cli_start();

    /* Warm up the radio engine in the background so first-tap latency is
       just HTTP+decode, not also codec/I2S setup. */
    radio_engine_warm_at_boot();

    /* If user picked URL background mode, spawn the fetcher. It
       waits for Wi-Fi internally (esp_http_client just fails until
       we associate, then succeeds on the next interval). */
    bg_fetcher_ensure();

    /* Auto-connect at boot using whatever NVS has stored, unless the
       user disabled it in Settings. */
    if (!g_cfg.wifi_autoconnect) {
        ESP_LOGI(TAG, "auto-connect: disabled in settings");
    } else {
        if (!g_cfg.last_ssid[0] && DEFAULT_WIFI_SSID[0]) {
            strncpy(g_cfg.last_ssid, DEFAULT_WIFI_SSID,
                    sizeof(g_cfg.last_ssid) - 1);
        }
        char pass[65] = {0};
        bool have_pass = cfg_get_ssid_pass(g_cfg.last_ssid, pass, sizeof(pass));
        if (!have_pass && DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0] &&
            strcmp(g_cfg.last_ssid, DEFAULT_WIFI_SSID) == 0) {
            strncpy(pass, DEFAULT_WIFI_PASS, sizeof(pass) - 1);
            cfg_save_ssid_pass(g_cfg.last_ssid, pass);
            have_pass = true;
        }
        if (g_cfg.last_ssid[0] && have_pass) {
            ESP_LOGI(TAG, "auto-connect: %s (pass_len=%u)",
                     g_cfg.last_ssid, (unsigned)strlen(pass));
            wifi_connect(g_cfg.last_ssid, pass);
        } else {
            ESP_LOGI(TAG, "auto-connect: no credentials yet (use Settings -> Wi-Fi)");
        }
    }



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
